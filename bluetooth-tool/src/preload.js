const { contextBridge, ipcRenderer } = require('electron');

function subscribe(channel, callback) {
  const listener = (_event, payload) => callback(payload);
  ipcRenderer.on(channel, listener);
  return () => ipcRenderer.removeListener(channel, listener);
}

contextBridge.exposeInMainWorld('bt05Api', {
  getStatus: () => ipcRenderer.invoke('ble:get-status'),
  pickCsvFile: () => ipcRenderer.invoke('ble:pick-csv'),
  sendBytes: (bytes, label, options) => ipcRenderer.invoke('ble:send-bytes', bytes, label, options),
  stopTransfer: () => ipcRenderer.invoke('ble:stop-transfer'),
  rescan: () => ipcRenderer.invoke('ble:rescan'),
  disconnect: () => ipcRenderer.invoke('ble:disconnect'),
  onStatus: (callback) => subscribe('ble:status', callback),
  onLog: (callback) => subscribe('ble:log', callback),
});
