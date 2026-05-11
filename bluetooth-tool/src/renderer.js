const state = {
  logCount: 0,
  csvTransferActive: false,
  bt05ConfigActive: false,
};

function formatTime(timestamp) {
  const date = new Date(timestamp);
  return date.toLocaleTimeString([], {
    hour12: false,
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  }) + `.${String(date.getMilliseconds()).padStart(3, '0')}`;
}

function basename(filePath) {
  return String(filePath).split(/[\\/]/).pop() || filePath;
}

function parseHexToken(token) {
  const trimmed = token.trim();
  if (!trimmed) {
    return null;
  }

  const normalized = trimmed.replace(/^0x/i, '');
  if (!/^[0-9a-fA-F]{1,2}$/.test(normalized)) {
    throw new Error(`Invalid hex byte: ${token}`);
  }

  return Number.parseInt(normalized, 16);
}

function parseHexByteSequence(text) {
  const tokens = String(text)
    .split(/[\s,;]+/)
    .map((token) => token.trim())
    .filter(Boolean);

  if (tokens.length === 0) {
    throw new Error('No hex bytes were provided.');
  }

  return tokens.map(parseHexToken);
}

function parseRepeatCount(value) {
  const repeatCount = Number.parseInt(String(value).trim(), 10);

  if (!Number.isInteger(repeatCount) || repeatCount < 0) {
    throw new Error('Repeat count must be a whole number 0 or greater.');
  }

  return repeatCount;
}

function setCsvTransferActive(active) {
  state.csvTransferActive = active;

  document.querySelector('#sendCsvButton').disabled = active;
  document.querySelector('#stopTransferButton').disabled = !active;
  document.querySelector('#loadCsvButton').disabled = active;
  document.querySelector('#csvRepeatCount').disabled = active;
}

function setBt05ConfigActive(active) {
  state.bt05ConfigActive = active;
  document.querySelector('#moduleBaudSelect').disabled = active;
  document.querySelector('#setModuleBaudButton').disabled = active;
}

function setStatusMessage(message, kind = 'info') {
  const statusLine = document.querySelector('#statusLine');
  statusLine.textContent = message;
  statusLine.dataset.kind = kind;
}

function updateConnectionStatus(status) {
  const connectionBadge = document.querySelector('#connectionBadge');
  const adapterBadge = document.querySelector('#adapterBadge');
  const deviceValue = document.querySelector('#deviceValue');
  const moduleBaudValue = document.querySelector('#moduleBaudValue');
  const notifyValue = document.querySelector('#notifyValue');
  const writeValue = document.querySelector('#writeValue');

  adapterBadge.textContent = `Adapter ${status.adapterState}`;
  adapterBadge.dataset.state = status.adapterState;

  if (status.connected) {
    connectionBadge.textContent = `Connected to ${status.deviceName || 'BT05'}`;
    connectionBadge.dataset.state = 'connected';
  } else if (status.connecting) {
    connectionBadge.textContent = `Connecting to ${status.deviceName || 'BT05'}...`;
    connectionBadge.dataset.state = 'connecting';
  } else if (status.scanning) {
    connectionBadge.textContent = 'Scanning for BT05...';
    connectionBadge.dataset.state = 'scanning';
  } else {
    connectionBadge.textContent = 'Idle';
    connectionBadge.dataset.state = 'idle';
  }

  deviceValue.textContent = status.deviceName
    ? `${status.deviceName}${status.deviceId ? ` (${status.deviceId})` : ''}`
    : 'No device selected';
  moduleBaudValue.textContent = status.moduleUartBaudRate ? String(status.moduleUartBaudRate) : '9600';
  notifyValue.textContent = status.notifyUuid || '—';
  writeValue.textContent = status.writeUuid || '—';
}

function appendLog(entry) {
  const consolePane = document.querySelector('#consoleOutput');
  const line = document.createElement('div');
  line.className = `log-row log-${entry.direction}`;

  const previousScrollTop = consolePane.scrollTop;
  const previousScrollHeight = consolePane.scrollHeight;
  const shouldStickToTop = consolePane.scrollTop <= 16;

  if (entry.direction === 'rx' || entry.direction === 'tx') {
    const label = entry.label ? ` ${entry.label}` : '';
    line.innerHTML = `
      <span class="log-time">${formatTime(entry.timestamp)}</span>
      <span class="log-tag">${entry.direction.toUpperCase()}${label}</span>
      <span class="log-hex">${entry.hex}</span>
      <span class="log-ascii">${entry.ascii}</span>
    `;
  } else {
    line.innerHTML = `
      <span class="log-time">${formatTime(entry.timestamp)}</span>
      <span class="log-tag">${entry.direction.toUpperCase()}</span>
      <span class="log-message">${entry.message}</span>
    `;
  }

  consolePane.prepend(line);
  state.logCount += 1;

  while (consolePane.children.length > 800) {
    consolePane.removeChild(consolePane.lastElementChild);
  }

  if (shouldStickToTop) {
    consolePane.scrollTop = 0;
  } else {
    consolePane.scrollTop = previousScrollTop + (consolePane.scrollHeight - previousScrollHeight);
  }
}

async function handleRawSend() {
  const input = document.querySelector('#rawBytesInput');
  const bytes = parseHexByteSequence(input.value);
  await window.bt05Api.sendBytes(bytes, 'Raw bytes');
  setStatusMessage(`Sent ${bytes.length} byte(s) from raw input.`);
}

async function handleCsvSend() {
  const input = document.querySelector('#csvBytesInput');
  const repeatInput = document.querySelector('#csvRepeatCount');
  const bytes = parseHexByteSequence(input.value);
  const repeatCount = parseRepeatCount(repeatInput.value);

  setCsvTransferActive(true);
  setStatusMessage(
    repeatCount === 0
      ? 'Continuous CSV transfer started. Press Stop Transfer to end it.'
      : `Running CSV transfer ${repeatCount} time(s). A silent audio packet will be sent after it finishes.`
  );

  try {
    const result = await window.bt05Api.sendBytes(bytes, 'CSV bytes', {
      repeatCount,
      sendEmptyAudioPacketAtEnd: true,
    });

    if (result.stoppedByUser) {
      setStatusMessage(
        result.emptyAudioPacketSent
          ? `Transfer stopped after ${result.repeatsCompleted} complete loop(s). Silent audio packet sent.`
          : `Transfer stopped after ${result.repeatsCompleted} complete loop(s).`
      );
      return;
    }

    setStatusMessage(
      repeatCount === 1
        ? 'Sent CSV once and then sent a silent audio packet.'
        : `Sent CSV ${result.repeatsCompleted} time(s) and then sent a silent audio packet.`
    );
  } finally {
    setCsvTransferActive(false);
  }
}

async function handleStopTransfer() {
  const result = await window.bt05Api.stopTransfer();

  if (result.stopped) {
    setStatusMessage('Stop requested. Waiting for the current BLE chunk to finish...', 'info');
    return;
  }

  setStatusMessage('No CSV transfer is currently running.', 'error');
}

async function handleCsvFileLoad() {
  const file = await window.bt05Api.pickCsvFile();
  if (!file) {
    return;
  }

  document.querySelector('#csvBytesInput').value = file.content;
  document.querySelector('#csvFileLabel').textContent = basename(file.fileName || file.filePath);
  setStatusMessage(`Loaded ${basename(file.fileName || file.filePath)}.`);
}

async function handleModuleBaudChange() {
  const baudRate = Number.parseInt(document.querySelector('#moduleBaudSelect').value, 10);

  setBt05ConfigActive(true);
  setStatusMessage(`Requesting BT05 UART baud change to ${baudRate}...`);

  try {
    const result = await window.bt05Api.configureModuleBaud(baudRate);
    setStatusMessage(result.message, result.success ? 'info' : 'error');
  } finally {
    setBt05ConfigActive(false);
  }
}

async function refreshStatus() {
  const status = await window.bt05Api.getStatus();
  updateConnectionStatus(status);
}

function wireActions() {
  document.querySelector('#clearConsoleButton').addEventListener('click', () => {
    document.querySelector('#consoleOutput').replaceChildren();
    state.logCount = 0;
    setStatusMessage('Console cleared.');
  });

  document.querySelector('#rescanButton').addEventListener('click', async () => {
    try {
      const status = await window.bt05Api.rescan();
      updateConnectionStatus(status);
      setStatusMessage('Manual rescan started.');
    } catch (error) {
      setStatusMessage(error.message, 'error');
    }
  });

  document.querySelector('#disconnectButton').addEventListener('click', async () => {
    try {
      const status = await window.bt05Api.disconnect();
      updateConnectionStatus(status);
      setStatusMessage('Disconnected from BT05.');
    } catch (error) {
      setStatusMessage(error.message, 'error');
    }
  });

  document.querySelector('#sendRawButton').addEventListener('click', async () => {
    try {
      await handleRawSend();
    } catch (error) {
      setStatusMessage(error.message, 'error');
    }
  });

  document.querySelector('#sendCsvButton').addEventListener('click', async () => {
    try {
      await handleCsvSend();
    } catch (error) {
      setStatusMessage(error.message, 'error');
      setCsvTransferActive(false);
    }
  });

  document.querySelector('#stopTransferButton').addEventListener('click', async () => {
    try {
      await handleStopTransfer();
    } catch (error) {
      setStatusMessage(error.message, 'error');
    }
  });

  document.querySelector('#loadCsvButton').addEventListener('click', async () => {
    try {
      await handleCsvFileLoad();
    } catch (error) {
      setStatusMessage(error.message, 'error');
    }
  });

  document.querySelector('#setModuleBaudButton').addEventListener('click', async () => {
    try {
      await handleModuleBaudChange();
    } catch (error) {
      setStatusMessage(error.message, 'error');
      setBt05ConfigActive(false);
    }
  });
}

window.addEventListener('DOMContentLoaded', async () => {
  wireActions();
  setCsvTransferActive(false);
  setBt05ConfigActive(false);

  window.bt05Api.onStatus((status) => {
    updateConnectionStatus(status);
  });

  window.bt05Api.onLog((entry) => {
    appendLog(entry);
  });

  await refreshStatus();
  setStatusMessage('BT05 monitor ready. It will auto-scan for any device named BT05.');
});
