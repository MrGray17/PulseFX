const desktop = typeof window !== 'undefined' ? window.pulsefx : undefined;

export const pulsefxApi = {
  available: Boolean(desktop),
  async command(name, ...args) {
    if (!desktop) return { ok: false, type: 'offline', error: 'Native PulseFX host is available only in the desktop app.' };
    return desktop.command(name, ...args);
  },
  async loadSettings() {
    if (!desktop) return {};
    return desktop.loadSettings();
  },
  async saveSettings(settings) {
    if (!desktop) return false;
    return desktop.saveSettings(settings);
  },
  async listHeadphones() {
    if (!desktop) return { revision: '', models: [] };
    return desktop.listHeadphones();
  },
  async applyHeadphoneProfile(modelPath) {
    if (!desktop) throw new Error('Headphone profiles are available only in the desktop app.');
    return desktop.applyHeadphoneProfile(modelPath);
  },
  onEvent(callback) {
    return desktop ? desktop.onEvent(callback) : () => {};
  },
  onHostState(callback) {
    return desktop ? desktop.onHostState(callback) : () => {};
  },
};
