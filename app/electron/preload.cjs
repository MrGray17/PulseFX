const { contextBridge, ipcRenderer } = require('electron');

function bufferedChannel(channel, maxBuffered = 16, keepLatestOnly = false) {
  const subscribers = new Set();
  const buffered = [];

  ipcRenderer.on(channel, (_event, payload) => {
    if (subscribers.size > 0) {
      for (const subscriber of [...subscribers]) {
        try { subscriber(payload); } catch { /* renderer callback errors stay isolated */ }
      }
      return;
    }

    if (keepLatestOnly) buffered.length = 0;
    buffered.push(payload);
    while (buffered.length > maxBuffered) buffered.shift();
  });

  return (callback) => {
    if (typeof callback !== 'function') return () => {};
    subscribers.add(callback);

    // Preload runs before the renderer bundle. Main-process messages can arrive
    // during that gap, especially tray/global-hotkey actions on a cold window.
    // Drain them only after React has installed its subscriber so no requested
    // action disappears between did-finish-load and useEffect registration.
    while (buffered.length > 0) {
      const payload = buffered.shift();
      try { callback(payload); } catch { /* preserve the remaining IPC bridge */ }
    }

    return () => subscribers.delete(callback);
  };
}

const subscribeEvent = bufferedChannel('pulsefx:event', 8, true);
const subscribeHostState = bufferedChannel('pulsefx:host-state', 4, true);
const subscribeQuickAction = bufferedChannel('pulsefx:quick-action', 32, false);

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
    return subscribeEvent(callback);
  },
  onHostState(callback) {
    return subscribeHostState(callback);
  },
  onQuickAction(callback) {
    return subscribeQuickAction(callback);
  },
});
