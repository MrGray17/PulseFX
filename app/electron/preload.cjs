const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('pulsefx', {
  command(name, ...args) {
    return ipcRenderer.invoke('pulsefx:command', { name, args });
  },
  loadSettings() {
    return ipcRenderer.invoke('pulsefx:settings:load');
  },
  saveSettings(settings) {
    return ipcRenderer.invoke('pulsefx:settings:save', settings);
  },
  onEvent(callback) {
    if (typeof callback !== 'function') return () => {};
    const listener = (_event, payload) => callback(payload);
    ipcRenderer.on('pulsefx:event', listener);
    return () => ipcRenderer.removeListener('pulsefx:event', listener);
  },
  onHostState(callback) {
    if (typeof callback !== 'function') return () => {};
    const listener = (_event, payload) => callback(payload);
    ipcRenderer.on('pulsefx:host-state', listener);
    return () => ipcRenderer.removeListener('pulsefx:host-state', listener);
  },
});
