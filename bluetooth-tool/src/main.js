const { app, BrowserWindow, dialog, ipcMain } = require('electron');
const noble = require('@abandonware/noble');
const path = require('path');
const fs = require('fs/promises');
const { setTimeout: delay } = require('timers/promises');

const TARGET_DEVICE_NAME = 'BT05';
const WRITE_CHUNK_SIZE = 20;
const RECONNECT_DELAY_MS = 1000;
const BLUETOOTH_BASE_UUID_SUFFIX = '00001000800000805f9b34fb';
const AUDIO_PKT_START_BYTE = 0xAA;
const AUDIO_PKT_TOTAL_SIZE = 109;
const AUDIO_PKT_PAYLOAD_LEN = 106;
const DEFAULT_BT05_UART_BAUD_RATE = 9600;
const UART_FRAME_BITS = 10;
const UART_DRAIN_MARGIN_MS = 2;
const BT_PROTOCOL_SYNC0 = 0xA5;
const BT_PROTOCOL_SYNC1 = 0x5A;
const BT_PROTOCOL_MAX_PAYLOAD = 16;
const BT_PROTO_CMD_SET_BT05_BAUD = 0x04;
const BT_PROTO_EVT_BT05_BAUD_RESULT = 0x85;
const BT05_BAUD_RESULT_TIMEOUT_MS = 5000;
const BT05_BAUD_STATUS_MESSAGES = {
  0: 'BT05 UART baud updated successfully.',
  1: 'STM rejected the requested baud rate.',
  2: 'STM could not talk to the BT05 module at the current baud.',
  3: 'BT05 did not accept the baud-change AT command.',
  4: 'STM failed to reinitialize USART1 at the requested baud.',
  5: 'STM could not verify the new baud after reconfiguration.',
};

const UART_CANDIDATES = [
  {
    serviceUuid: 'ffe0',
    notifyUuid: 'ffe1',
    writeUuid: 'ffe1',
  },
  {
    serviceUuid: 'ffe5',
    notifyUuid: 'ffe9',
    writeUuid: 'ffe9',
  },
  {
    serviceUuid: 'fff0',
    notifyUuid: 'fff1',
    writeUuid: 'fff1',
  },
  {
    serviceUuid: '6e400001b5a3f393e0a9e50e24dcca9e',
    notifyUuid: '6e400003b5a3f393e0a9e50e24dcca9e',
    writeUuid: '6e400002b5a3f393e0a9e50e24dcca9e',
  },
];

const bleState = {
  adapterState: 'unknown',
  scanning: false,
  connecting: false,
  connected: false,
  shouldAutoReconnect: true,
  peripheral: null,
  notifyCharacteristic: null,
  writeCharacteristic: null,
  lastSeenName: '',
  lastSeenId: '',
  reconnectTimer: null,
};

const transferState = {
  active: false,
  stopRequested: false,
};

const protocolState = {
  state: 0,
  type: 0,
  length: 0,
  index: 0,
  payload: Buffer.alloc(BT_PROTOCOL_MAX_PAYLOAD),
};

let currentBt05ModuleBaudRate = DEFAULT_BT05_UART_BAUD_RATE;
let pendingBt05BaudRequest = null;

function normalizeUuid(uuid) {
  return String(uuid || '').replace(/-/g, '').toLowerCase();
}

function expandUuidVariants(uuid) {
  const normalized = normalizeUuid(uuid);
  const variants = new Set([normalized]);

  if (normalized.length === 4) {
    variants.add(`0000${normalized}${BLUETOOTH_BASE_UUID_SUFFIX}`);
  } else if (normalized.length === 8) {
    variants.add(`${normalized}${BLUETOOTH_BASE_UUID_SUFFIX}`);
  } else if (normalized.length === 32 && normalized.endsWith(BLUETOOTH_BASE_UUID_SUFFIX)) {
    if (normalized.startsWith('0000')) {
      variants.add(normalized.slice(4, 8));
    }
    variants.add(normalized.slice(0, 8));
  }

  return variants;
}

function uuidMatches(actualUuid, expectedUuid) {
  const actualVariants = expandUuidVariants(actualUuid);
  const expectedVariants = expandUuidVariants(expectedUuid);

  for (const variant of actualVariants) {
    if (expectedVariants.has(variant)) {
      return true;
    }
  }

  return false;
}

function getCharacteristicServiceUuid(characteristic) {
  return normalizeUuid(characteristic?._serviceUuid);
}

function bufferToHex(buffer) {
  return Array.from(buffer, (value) => value.toString(16).padStart(2, '0').toUpperCase()).join(' ');
}

function bufferToAscii(buffer) {
  return Array.from(buffer, (value) => {
    if (value >= 32 && value <= 126) {
      return String.fromCharCode(value);
    }
    return '.';
  }).join('');
}

function computeAudioPacketChecksum(buffer) {
  let checksum = 0;

  for (let i = 0; i < buffer.length - 1; i += 1) {
    checksum ^= buffer[i];
  }

  return checksum & 0xFF;
}

function buildEmptyAudioPacket() {
  const packet = Buffer.alloc(AUDIO_PKT_TOTAL_SIZE);
  packet[0] = AUDIO_PKT_START_BYTE;
  packet[1] = AUDIO_PKT_PAYLOAD_LEN & 0xFF;
  packet[2] = (AUDIO_PKT_PAYLOAD_LEN >> 8) & 0xFF;
  packet[AUDIO_PKT_TOTAL_SIZE - 1] = computeAudioPacketChecksum(packet);
  return packet;
}

function getChunkDrainDelayMs(byteCount) {
  return Math.ceil((byteCount * UART_FRAME_BITS * 1000) / currentBt05ModuleBaudRate) + UART_DRAIN_MARGIN_MS;
}

function resetProtocolParser() {
  protocolState.state = 0;
  protocolState.type = 0;
  protocolState.length = 0;
  protocolState.index = 0;
}

function buildProtocolFrame(type, payload = Buffer.alloc(0)) {
  if (payload.length > BT_PROTOCOL_MAX_PAYLOAD) {
    throw new Error(`Protocol payload too large: ${payload.length}`);
  }

  const frame = Buffer.alloc(5 + payload.length);
  let checksum = type ^ payload.length;

  frame[0] = BT_PROTOCOL_SYNC0;
  frame[1] = BT_PROTOCOL_SYNC1;
  frame[2] = type;
  frame[3] = payload.length;

  for (let i = 0; i < payload.length; i += 1) {
    frame[4 + i] = payload[i];
    checksum ^= payload[i];
  }

  frame[4 + payload.length] = checksum & 0xFF;
  return frame;
}

function readU32LE(buffer, offset = 0) {
  return (
    buffer[offset] |
    (buffer[offset + 1] << 8) |
    (buffer[offset + 2] << 16) |
    (buffer[offset + 3] << 24)
  ) >>> 0;
}

function finalizeBt05BaudRequest(result) {
  if (!pendingBt05BaudRequest) {
    return;
  }

  clearTimeout(pendingBt05BaudRequest.timeoutId);
  pendingBt05BaudRequest.resolve(result);
  pendingBt05BaudRequest = null;
}

function failBt05BaudRequest(error) {
  if (!pendingBt05BaudRequest) {
    return;
  }

  clearTimeout(pendingBt05BaudRequest.timeoutId);
  pendingBt05BaudRequest.reject(error);
  pendingBt05BaudRequest = null;
}

function handleProtocolFrame(type, payload) {
  if (type !== BT_PROTO_EVT_BT05_BAUD_RESULT || payload.length < 5) {
    return;
  }

  const status = payload[0];
  const baudRate = readU32LE(payload, 1);
  const success = status === 0;
  const message = BT05_BAUD_STATUS_MESSAGES[status] || `BT05 baud request returned status ${status}.`;

  if (success) {
    currentBt05ModuleBaudRate = baudRate;
    publishStatus();
  }

  logSystem(success ? 'system' : 'error', `${message} Target baud: ${baudRate}.`);
  finalizeBt05BaudRequest({ success, status, baudRate, message: `${message} Target baud: ${baudRate}.` });
}

function processProtocolByte(byte) {
  switch (protocolState.state) {
    case 0:
      if (byte === BT_PROTOCOL_SYNC0) {
        protocolState.state = 1;
      }
      return;

    case 1:
      if (byte === BT_PROTOCOL_SYNC1) {
        protocolState.state = 2;
      } else if (byte !== BT_PROTOCOL_SYNC0) {
        resetProtocolParser();
      }
      return;

    case 2:
      protocolState.type = byte;
      protocolState.state = 3;
      return;

    case 3:
      if (byte > BT_PROTOCOL_MAX_PAYLOAD) {
        resetProtocolParser();
        return;
      }

      protocolState.length = byte;
      protocolState.index = 0;
      protocolState.state = byte === 0 ? 5 : 4;
      return;

    case 4:
      protocolState.payload[protocolState.index++] = byte;
      if (protocolState.index >= protocolState.length) {
        protocolState.state = 5;
      }
      return;

    case 5: {
      let checksum = protocolState.type ^ protocolState.length;

      for (let i = 0; i < protocolState.length; i += 1) {
        checksum ^= protocolState.payload[i];
      }

      if ((checksum & 0xFF) === byte) {
        handleProtocolFrame(protocolState.type, protocolState.payload.subarray(0, protocolState.length));
      }

      resetProtocolParser();
      return;
    }

    default:
      resetProtocolParser();
  }
}

function getPeripheralName(peripheral) {
  return (
    peripheral?.advertisement?.localName ||
    peripheral?.name ||
    bleState.lastSeenName ||
    'Unknown device'
  );
}

function publish(channel, payload) {
  for (const window of BrowserWindow.getAllWindows()) {
    window.webContents.send(channel, payload);
  }
}

function logSystem(direction, message) {
  publish('ble:log', {
    timestamp: new Date().toISOString(),
    direction,
    message,
  });
}

function logTransport(direction, buffer, label) {
  publish('ble:log', {
    timestamp: new Date().toISOString(),
    direction,
    label,
    count: buffer.length,
    hex: bufferToHex(buffer),
    ascii: bufferToAscii(buffer),
  });
}

function statusPayload() {
  return {
    adapterState: bleState.adapterState,
    scanning: bleState.scanning,
    connecting: bleState.connecting,
    connected: bleState.connected,
    autoReconnect: bleState.shouldAutoReconnect,
    deviceName: bleState.lastSeenName,
    deviceId: bleState.lastSeenId,
    moduleUartBaudRate: currentBt05ModuleBaudRate,
    notifyUuid: bleState.notifyCharacteristic ? normalizeUuid(bleState.notifyCharacteristic.uuid) : '',
    writeUuid: bleState.writeCharacteristic ? normalizeUuid(bleState.writeCharacteristic.uuid) : '',
  };
}

function publishStatus() {
  publish('ble:status', statusPayload());
}

function clearReconnectTimer() {
  if (bleState.reconnectTimer) {
    clearTimeout(bleState.reconnectTimer);
    bleState.reconnectTimer = null;
  }
}

function scheduleReconnect(reason) {
  if (!bleState.shouldAutoReconnect || bleState.reconnectTimer) {
    return;
  }

  bleState.reconnectTimer = setTimeout(() => {
    bleState.reconnectTimer = null;
    startScanning(reason).catch((error) => {
      logSystem('error', `Rescan failed: ${error.message}`);
    });
  }, RECONNECT_DELAY_MS);
}

function cleanupConnectionState() {
  if (bleState.notifyCharacteristic) {
    bleState.notifyCharacteristic.removeAllListeners('data');
  }

  if (transferState.active) {
    transferState.stopRequested = true;
  }

  failBt05BaudRequest(new Error('BT05 connection closed before the baud configuration request completed.'));

  bleState.peripheral = null;
  bleState.notifyCharacteristic = null;
  bleState.writeCharacteristic = null;
  bleState.connecting = false;
  bleState.connected = false;
  publishStatus();
}

async function stopScanning() {
  if (!bleState.scanning) {
    return;
  }

  bleState.scanning = false;
  publishStatus();

  try {
    await noble.stopScanningAsync();
  } catch (_error) {
    // Ignore stop-scan failures during teardown.
  }
}

function matchesTarget(peripheral) {
  const name = getPeripheralName(peripheral).toUpperCase();
  return name.includes(TARGET_DEVICE_NAME);
}

function characteristicHasNotify(characteristic) {
  return characteristic.properties.includes('notify') || characteristic.properties.includes('indicate');
}

function characteristicHasWrite(characteristic) {
  return characteristic.properties.includes('write') || characteristic.properties.includes('writeWithoutResponse');
}

function characteristicsForServices(characteristics, serviceUuids) {
  return characteristics.filter((characteristic) =>
    serviceUuids.some((serviceUuid) => uuidMatches(getCharacteristicServiceUuid(characteristic), serviceUuid))
  );
}

function selectCharacteristics(services, characteristics) {
  for (const candidate of UART_CANDIDATES) {
    const matchingServiceUuids = services
      .filter((service) => uuidMatches(service.uuid, candidate.serviceUuid))
      .map((service) => normalizeUuid(service.uuid));

    if (matchingServiceUuids.length === 0) {
      continue;
    }

    const scopedCharacteristics = characteristicsForServices(characteristics, matchingServiceUuids);
    const notifyCharacteristic = scopedCharacteristics.find(
      (characteristic) =>
        uuidMatches(characteristic.uuid, candidate.notifyUuid) && characteristicHasNotify(characteristic)
    );
    const writeCharacteristic = scopedCharacteristics.find(
      (characteristic) =>
        uuidMatches(characteristic.uuid, candidate.writeUuid) && characteristicHasWrite(characteristic)
    );

    if (!notifyCharacteristic || !writeCharacteristic) {
      continue;
    }

    if (characteristicHasNotify(notifyCharacteristic) && characteristicHasWrite(writeCharacteristic)) {
      return { notifyCharacteristic, writeCharacteristic };
    }
  }

  const serviceGroups = new Map();

  for (const characteristic of characteristics) {
    const serviceUuid = getCharacteristicServiceUuid(characteristic);
    if (!serviceGroups.has(serviceUuid)) {
      serviceGroups.set(serviceUuid, []);
    }
    serviceGroups.get(serviceUuid).push(characteristic);
  }

  for (const scopedCharacteristics of serviceGroups.values()) {
    const notifyCharacteristic = scopedCharacteristics.find(characteristicHasNotify);
    const writeCharacteristic = scopedCharacteristics.find(characteristicHasWrite);

    if (notifyCharacteristic && writeCharacteristic) {
      return { notifyCharacteristic, writeCharacteristic };
    }
  }

  const notifyCharacteristic = characteristics.find(characteristicHasNotify);
  const writeCharacteristic = characteristics.find(characteristicHasWrite);

  if (notifyCharacteristic && writeCharacteristic) {
    return { notifyCharacteristic, writeCharacteristic };
  }

  return null;
}

async function subscribeToNotifications(characteristic) {
  if (!characteristicHasNotify(characteristic)) {
    throw new Error('No notifiable characteristic found on the BT05 device.');
  }

  characteristic.on('data', (buffer) => {
    logTransport('rx', buffer, 'BT05 notification');

    for (const byte of buffer) {
      processProtocolByte(byte);
    }
  });

  await characteristic.subscribeAsync();
}

async function connectPeripheral(peripheral) {
  if (bleState.connected || bleState.connecting) {
    return;
  }

  bleState.connecting = true;
  bleState.lastSeenName = getPeripheralName(peripheral);
  bleState.lastSeenId = peripheral.id || '';
  publishStatus();
  clearReconnectTimer();

  await stopScanning();
  logSystem('system', `Connecting to ${bleState.lastSeenName} (${bleState.lastSeenId})`);

  peripheral.removeAllListeners('disconnect');
  peripheral.once('disconnect', () => {
    logSystem('system', `${getPeripheralName(peripheral)} disconnected`);
    cleanupConnectionState();
    scheduleReconnect('disconnect');
  });

  try {
    await peripheral.connectAsync();
    const { services, characteristics } = await peripheral.discoverAllServicesAndCharacteristicsAsync();
    const selected = selectCharacteristics(services, characteristics);

    if (!selected) {
      throw new Error('Could not find a usable UART service/characteristic pair on the BT05 device.');
    }

    bleState.peripheral = peripheral;
    bleState.notifyCharacteristic = selected.notifyCharacteristic;
    bleState.writeCharacteristic = selected.writeCharacteristic;
    bleState.connected = true;
    bleState.connecting = false;
    publishStatus();

    await subscribeToNotifications(selected.notifyCharacteristic);

    logSystem(
      'system',
      `Connected to ${bleState.lastSeenName} (notify ${normalizeUuid(selected.notifyCharacteristic.uuid)}, write ${normalizeUuid(selected.writeCharacteristic.uuid)})`
    );
  } catch (error) {
    logSystem('error', `Connection failed: ${error.message}`);

    try {
      await peripheral.disconnectAsync();
    } catch (_disconnectError) {
      // Ignore disconnect cleanup failures.
    }

    cleanupConnectionState();
    scheduleReconnect('connect failure');
  }
}

async function startScanning(reason = 'auto') {
  if (bleState.adapterState !== 'poweredOn') {
    logSystem('error', `Bluetooth adapter is ${bleState.adapterState}`);
    return;
  }

  if (bleState.scanning || bleState.connected || bleState.connecting) {
    return;
  }

  bleState.scanning = true;
  publishStatus();
  logSystem('system', `Scanning for ${TARGET_DEVICE_NAME} (${reason})`);

  try {
    await noble.startScanningAsync([], false);
  } catch (error) {
    bleState.scanning = false;
    publishStatus();
    logSystem('error', `Scan failed: ${error.message}`);
    scheduleReconnect('scan failure');
  }
}

function getWriteModes() {
  const writeModes = [];

  if (bleState.writeCharacteristic.properties.includes('write')) {
    writeModes.push(false);
  }

  if (bleState.writeCharacteristic.properties.includes('writeWithoutResponse')) {
    writeModes.push(true);
  }

  if (writeModes.length === 0) {
    throw new Error('Connected BT05 characteristic is not writable.');
  }

  return writeModes;
}

async function writeBufferChunks(buffer, label, writeModes, options = {}) {
  const allowStop = options.allowStop === true;
  let sent = 0;

  for (let offset = 0; offset < buffer.length; offset += WRITE_CHUNK_SIZE) {
    if (allowStop && transferState.stopRequested) {
      return { sent, stopped: true };
    }

    const chunk = buffer.subarray(offset, offset + WRITE_CHUNK_SIZE);
    let writeError = null;

    for (const withoutResponse of writeModes) {
      try {
        await bleState.writeCharacteristic.writeAsync(chunk, withoutResponse);
        writeError = null;
        break;
      } catch (error) {
        writeError = error;
      }
    }

    if (writeError) {
      throw writeError;
    }

    sent += chunk.length;
    logTransport('tx', chunk, label);

    if (allowStop && transferState.stopRequested) {
      return { sent, stopped: true };
    }

    await delay(getChunkDrainDelayMs(chunk.length));
  }

  return { sent, stopped: false };
}

async function writeBytes(byteArray, label, options = {}) {
  if (!bleState.connected || !bleState.writeCharacteristic) {
    throw new Error('No BT05 device is connected.');
  }

  if (transferState.active) {
    throw new Error('A transfer is already in progress.');
  }

  const repeatCount = sanitizeRepeatCount(options);
  const sendEmptyAudioPacketAtEnd = options?.sendEmptyAudioPacketAtEnd === true;
  const buffer = Buffer.from(byteArray);
  const writeModes = getWriteModes();
  let totalSent = 0;
  let repeatsCompleted = 0;
  let stoppedByUser = false;
  let emptyAudioPacketSent = false;

  transferState.active = true;
  transferState.stopRequested = false;

  try {
    if (repeatCount === 0) {
      while (!transferState.stopRequested) {
        const result = await writeBufferChunks(
          buffer,
          `${label} loop ${repeatsCompleted + 1}`,
          writeModes,
          { allowStop: true }
        );

        totalSent += result.sent;
        if (result.stopped) {
          break;
        }

        repeatsCompleted += 1;
      }
    } else {
      for (let repeatIndex = 0; repeatIndex < repeatCount; repeatIndex += 1) {
        const result = await writeBufferChunks(
          buffer,
          repeatCount > 1 ? `${label} ${repeatIndex + 1}/${repeatCount}` : label,
          writeModes,
          { allowStop: true }
        );

        totalSent += result.sent;
        if (result.stopped) {
          break;
        }

        repeatsCompleted += 1;
      }
    }

    stoppedByUser = transferState.stopRequested;

    if (sendEmptyAudioPacketAtEnd) {
      const emptyAudioPacket = buildEmptyAudioPacket();
      const result = await writeBufferChunks(emptyAudioPacket, 'Empty audio packet', writeModes);
      totalSent += result.sent;
      emptyAudioPacketSent = true;
    }

    return {
      sent: totalSent,
      repeats: repeatCount,
      repeatsCompleted,
      stoppedByUser,
      emptyAudioPacketSent,
    };
  } finally {
    transferState.active = false;
    transferState.stopRequested = false;
  }
}

async function configureModuleBaud(baudRate) {
  if (!bleState.connected || !bleState.writeCharacteristic) {
    throw new Error('No BT05 device is connected.');
  }

  if (pendingBt05BaudRequest) {
    throw new Error('A BT05 baud request is already pending.');
  }

  const payload = Buffer.alloc(4);
  payload.writeUInt32LE(baudRate, 0);
  const frame = buildProtocolFrame(BT_PROTO_CMD_SET_BT05_BAUD, payload);

  const responsePromise = new Promise((resolve, reject) => {
    pendingBt05BaudRequest = {
      timeoutId: setTimeout(() => {
        pendingBt05BaudRequest = null;
        reject(new Error('Timed out waiting for the STM32 BT05 baud response.'));
      }, BT05_BAUD_RESULT_TIMEOUT_MS),
      resolve,
      reject,
    };
  });

  try {
    await writeBytes(Array.from(frame), 'BT05 baud config', { repeatCount: 1 });
  } catch (error) {
    failBt05BaudRequest(error);
    throw error;
  }

  return responsePromise;
}

function sanitizeRepeatCount(options) {
  const repeatCount = options?.repeatCount;

  if (repeatCount === undefined) {
    return 1;
  }

  const numericRepeatCount = Number(repeatCount);

  if (!Number.isInteger(numericRepeatCount) || numericRepeatCount < 0) {
    throw new Error(`Invalid repeat count: ${repeatCount}`);
  }

  return numericRepeatCount;
}

function sanitizeBytes(bytes) {
  if (!Array.isArray(bytes)) {
    throw new Error('Expected a byte array.');
  }

  return bytes.map((value) => {
    const byte = Number(value);
    if (!Number.isInteger(byte) || byte < 0 || byte > 255) {
      throw new Error(`Invalid byte value: ${value}`);
    }
    return byte;
  });
}

async function createWindow() {
  const window = new BrowserWindow({
    width: 1440,
    height: 920,
    minWidth: 1160,
    minHeight: 760,
    backgroundColor: '#17181c',
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });

  await window.loadFile(path.join(__dirname, 'index.html'));
}

function registerIpc() {
  ipcMain.handle('ble:get-status', () => statusPayload());

  ipcMain.handle('ble:pick-csv', async () => {
    const result = await dialog.showOpenDialog({
      properties: ['openFile'],
      filters: [
        { name: 'CSV or Text', extensions: ['csv', 'txt'] },
        { name: 'All Files', extensions: ['*'] },
      ],
    });

    if (result.canceled || result.filePaths.length === 0) {
      return null;
    }

    const filePath = result.filePaths[0];
    const content = await fs.readFile(filePath, 'utf8');
    return {
      filePath,
      fileName: path.basename(filePath),
      content,
    };
  });

  ipcMain.handle('ble:send-bytes', async (_event, bytes, label = 'Manual send', options = {}) => {
    return writeBytes(sanitizeBytes(bytes), label, {
      repeatCount: sanitizeRepeatCount(options),
      sendEmptyAudioPacketAtEnd: options?.sendEmptyAudioPacketAtEnd === true,
    });
  });

  ipcMain.handle('ble:configure-module-baud', async (_event, baudRate) => {
    const requestedBaud = Number(baudRate);
    if (!Number.isInteger(requestedBaud) || requestedBaud <= 0) {
      throw new Error(`Invalid baud rate: ${baudRate}`);
    }

    return configureModuleBaud(requestedBaud);
  });

  ipcMain.handle('ble:stop-transfer', async () => {
    if (!transferState.active) {
      return { stopped: false };
    }

    transferState.stopRequested = true;
    logSystem('system', 'Stop transfer requested');
    return { stopped: true };
  });

  ipcMain.handle('ble:rescan', async () => {
    bleState.shouldAutoReconnect = true;
    clearReconnectTimer();

    if (bleState.peripheral) {
      try {
        await bleState.peripheral.disconnectAsync();
      } catch (_disconnectError) {
        cleanupConnectionState();
      }
    }

    cleanupConnectionState();
    await stopScanning();
    await startScanning('manual rescan');
    return statusPayload();
  });

  ipcMain.handle('ble:disconnect', async () => {
    bleState.shouldAutoReconnect = false;
    clearReconnectTimer();

    if (bleState.peripheral) {
      try {
        await bleState.peripheral.disconnectAsync();
      } catch (_disconnectError) {
        // Ignore disconnect failures during manual teardown.
      }
    }

    cleanupConnectionState();
    await stopScanning();
    return statusPayload();
  });
}

noble.on('stateChange', async (state) => {
  bleState.adapterState = state;
  publishStatus();
  logSystem('system', `Bluetooth adapter state: ${state}`);

  if (state === 'poweredOn') {
    await startScanning('adapter ready');
    return;
  }

  await stopScanning();
  cleanupConnectionState();
});

noble.on('discover', async (peripheral) => {
  if (!matchesTarget(peripheral)) {
    return;
  }

  bleState.lastSeenName = getPeripheralName(peripheral);
  bleState.lastSeenId = peripheral.id || '';
  publishStatus();
  logSystem('system', `Found ${bleState.lastSeenName} (${bleState.lastSeenId})`);

  await connectPeripheral(peripheral);
});

app.whenReady().then(async () => {
  registerIpc();
  await createWindow();
  publishStatus();

  if (bleState.adapterState === 'poweredOn') {
    await startScanning('app ready');
  }

  app.on('activate', async () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      await createWindow();
      publishStatus();
    }
  });
});

app.on('before-quit', async () => {
  bleState.shouldAutoReconnect = false;
  clearReconnectTimer();
  await stopScanning();

  if (bleState.peripheral) {
    try {
      await bleState.peripheral.disconnectAsync();
    } catch (_disconnectError) {
      // Ignore disconnect failures on exit.
    }
  }
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});
