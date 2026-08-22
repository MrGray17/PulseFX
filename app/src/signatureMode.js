const VALID_MODES = new Set(['signature', 'manual']);
const LEGACY_SOUND_KEYS = new Set([
  'enabled', 'preamp', 'pitch', 'headphoneEq', 'headphoneModelPath',
  'effectEnabled', 'effectAmounts', 'eq', 'preset', 'outputId',
]);
const MANUAL_SOUND_COMMANDS = new Set([
  'preamp', 'bass', 'clarity', 'fidelity', 'spatial', 'surround',
  'ambience', 'dynamics', 'night', 'eq',
]);

export function hasLegacySoundState(settings) {
  if (!settings || typeof settings !== 'object') return false;
  return Object.keys(settings).some((key) => LEGACY_SOUND_KEYS.has(key));
}

export function resolveProcessingMode(settings) {
  if (VALID_MODES.has(settings?.mode)) return settings.mode;
  return hasLegacySoundState(settings) ? 'manual' : 'signature';
}

export function applyFirstRunSignatureDefaults(settings, mode) {
  const source = settings && typeof settings === 'object' ? settings : {};
  if (mode !== 'signature' || hasLegacySoundState(source)) return source;
  return {
    ...source,
    preset: 'Flat',
    eq: Array.from({ length: 31 }, () => 0),
  };
}

export function isManualSoundCommand(name) {
  return MANUAL_SOUND_COMMANDS.has(name);
}

// Coordinates the existing renderer's startup restore transaction with the new
// native processing mode. The renderer always restores 31 EQ bands; band 30 is
// therefore a deterministic end-of-core-audio-settings boundary, not a timer.
export class ProcessingModeCoordinator {
  constructor() {
    this.mode = 'signature';
    this.startupSync = false;
    this.listeners = new Set();
  }

  setMode(mode) {
    if (!VALID_MODES.has(mode) || mode === this.mode) return;
    this.mode = mode;
    for (const listener of this.listeners) {
      try { listener(this.mode); } catch { /* UI listeners never own audio state. */ }
    }
  }

  subscribe(listener) {
    if (typeof listener !== 'function') return () => {};
    this.listeners.add(listener);
    listener(this.mode);
    return () => this.listeners.delete(listener);
  }

  restore(settings) {
    this.setMode(resolveProcessingMode(settings));
    this.startupSync = true;
    return applyFirstRunSignatureDefaults(settings, this.mode);
  }

  observeCommand(name, args = []) {
    if (name === 'mode' && VALID_MODES.has(args[0])) {
      this.setMode(args[0]);
      this.startupSync = false;
      return null;
    }

    if (this.startupSync) {
      if (name === 'eq' && Number(args[0]) === 30) {
        this.startupSync = false;
        return this.mode;
      }
      return null;
    }

    if (isManualSoundCommand(name)) this.setMode('manual');
    return null;
  }

  savedSettings(settings) {
    return { ...settings, mode: this.mode };
  }
}
