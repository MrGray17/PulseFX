import { ProcessingModeCoordinator } from './signatureMode.js';

const desktop = typeof window !== 'undefined' ? window.pulsefx : undefined;
const processingMode = new ProcessingModeCoordinator();

export const pulsefxApi = {
  available: Boolean(desktop),
  async command(name, ...args) {
    if (!desktop) return { ok: false, type: 'offline', error: 'Native PulseFX host is available only in the desktop app.' };
    const result = await desktop.command(name, ...args);
    const startupMode = processingMode.observeCommand(name, args);
    if (startupMode) {
      const modeResult = await desktop.command('mode', startupMode);
      if (modeResult?.ok === false) return modeResult;
    }
    return result;
  },
  async loadSettings() {
    if (!desktop) return processingMode.restore({});
    const saved = await desktop.loadSettings();
    return processingMode.restore(saved ?? {});
  },
  // Raw read for isolated auxiliary UI surfaces. Unlike loadSettings(), this
  // does not participate in the startup Signature/Manual migration transaction.
  async peekSettings() {
    if (!desktop) return {};
    const saved = await desktop.loadSettings();
    return saved && typeof saved === 'object' ? saved : {};
  },
  async saveSettings(settings) {
    if (!desktop) return false;
    return desktop.saveSettings(processingMode.savedSettings(settings));
  },
  getProcessingMode() {
    return processingMode.mode;
  },
  onProcessingMode(callback) {
    return processingMode.subscribe(callback);
  },
  async setProcessingMode(mode) {
    if (!desktop) return { ok: false, type: 'offline', error: 'Native PulseFX host is available only in the desktop app.' };
    if (mode !== 'signature' && mode !== 'manual') {
      return { ok: false, type: 'error', error: 'Unsupported processing mode.' };
    }
    const result = await desktop.command('mode', mode);
    if (result?.ok !== false) processingMode.observeCommand('mode', [mode]);
    return result;
  },
  async listHeadphones() {
    if (!desktop) return { revision: '', models: [] };
    return desktop.listHeadphones();
  },
  async applyHeadphoneProfile(modelPath) {
    if (!desktop) throw new Error('Headphone profiles are available only in the desktop app.');
    return desktop.applyHeadphoneProfile(modelPath);
  },
  async openAudioFiles() {
    if (!desktop) return [];
    return desktop.openAudioFiles();
  },
  async searchRadio(query = '') {
    if (!desktop) return [];
    return desktop.searchRadio(query);
  },
  async browseRadio(mode, countryCode = '') {
    if (!desktop) return [];
    if (typeof desktop.browseRadio === 'function') return desktop.browseRadio(mode, countryCode);
    return desktop.searchRadio('');
  },
  async listRadioCountries() {
    if (!desktop || typeof desktop.listRadioCountries !== 'function') return [];
    return desktop.listRadioCountries();
  },
  async getLocaleCountryCode() {
    if (!desktop || typeof desktop.getLocaleCountryCode !== 'function') return '';
    return desktop.getLocaleCountryCode();
  },
  async openDefaultApps() {
    if (!desktop || typeof desktop.openDefaultApps !== 'function') return false;
    return desktop.openDefaultApps();
  },
  async recordRadioClick(stationuuid) {
    if (!desktop) return false;
    return desktop.recordRadioClick(stationuuid);
  },
  onEvent(callback) {
    return desktop ? desktop.onEvent(callback) : () => {};
  },
  onHostState(callback) {
    return desktop ? desktop.onHostState(callback) : () => {};
  },
  onQuickAction(callback) {
    return desktop ? desktop.onQuickAction(callback) : () => {};
  },
};
