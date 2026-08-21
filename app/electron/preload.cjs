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
  listHeadphones() {
    return ipcRenderer.invoke('pulsefx:autoeq:list');
  },
  applyHeadphoneProfile(modelPath) {
    return ipcRenderer.invoke('pulsefx:autoeq:apply', modelPath);
  },
  openAudioFiles() {
    return ipcRenderer.invoke('pulsefx:media:open');
  },
  searchRadio(query = '') {
    return ipcRenderer.invoke('pulsefx:radio:search', query);
  },
  recordRadioClick(stationuuid) {
    return ipcRenderer.invoke('pulsefx:radio:click', stationuuid);
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
  onQuickAction(callback) {
    if (typeof callback !== 'function') return () => {};
    const listener = (_event, payload) => callback(payload);
    ipcRenderer.on('pulsefx:quick-action', listener);
    return () => ipcRenderer.removeListener('pulsefx:quick-action', listener);
  },
});
