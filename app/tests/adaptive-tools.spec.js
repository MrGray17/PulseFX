import { test, expect } from '@playwright/test';

async function installAdaptiveMock(page) {
  await page.addInitScript(() => {
    const calls = [];
    const listeners = { event: [], host: [], quick: [] };
    window.__adaptiveCalls = calls;
    window.pulsefx = {
      async command(name, ...args) {
        calls.push({ name, args });
        if (name === 'status') {
          return {
            type: 'status', ok: true, running: true, routingActive: true, error: '',
            controls: { mode: 'signature', content: 'music', lowLatency: false, signatureStrength: 1 },
            scene: { automationEnabled: true, matched: true, processName: 'spotify.exe', pid: 42, content: 'music', lowLatency: false, priority: 60, ruleCount: 1 },
            stats: {
              underruns: 0, overruns: 0, capturedFrames: 48000, renderedFrames: 48000,
              bufferedFrames: 960, controlRevision: 3, clockCorrectionPpm: 0,
              sampleRate: 48000, inputChannels: 2, processorLatencyFrames: 240,
              processorLatencyMs: 5, bufferedLatencyMs: 20, internalLatencyMs: 25,
              limiterGainReductionDb: 0.42, headroomStress: 0.18, masterWetMix: 1,
            },
          };
        }
        if (name === 'devices') {
          return { type: 'devices', ok: true, devices: [
            { id: 'pulsefx-output', name: 'PulseFX Output', default: true, pulsefx: true },
            { id: 'speakers-1', name: 'Speakers', default: false, pulsefx: false },
          ] };
        }
        if (name === 'apps') {
          return { type: 'apps', ok: true, apps: [
            { pid: 42, processName: 'spotify.exe', name: 'Spotify', active: true, muted: false, volume: 0.8 },
            { pid: 77, processName: 'game.exe', name: 'Game', active: true, muted: false, volume: 1 },
          ] };
        }
        return { type: 'ack', ok: true, command: name };
      },
      async loadSettings() {
        return { headphoneModelPath: 'headphones/acme-one', headphoneModelName: 'Acme One' };
      },
      async saveSettings() { return true; },
      async listHeadphones() { return { revision: '', models: [] }; },
      async applyHeadphoneProfile() { return { ok: false }; },
      async openAudioFiles() { return []; },
      async searchRadio() { return []; },
      async browseRadio() { return []; },
      async listRadioCountries() { return []; },
      async getLocaleCountryCode() { return 'MA'; },
      async openDefaultApps() { return true; },
      async recordRadioClick() { return true; },
      onEvent(callback) { listeners.event.push(callback); return () => {}; },
      onHostState(callback) { listeners.host.push(callback); return () => {}; },
      onQuickAction(callback) { listeners.quick.push(callback); return () => {}; },
    };
  });
}

async function calls(page) {
  return page.evaluate(() => window.__adaptiveCalls ?? []);
}

test('Adaptive Tools maps real audio apps to native scenes and policy controls', async ({ page }) => {
  await installAdaptiveMock(page);
  await page.goto('/');
  await page.getByRole('button', { name: 'Open Adaptive Tools' }).click();
  await expect(page.getByRole('dialog', { name: 'Adaptive Tools' })).toBeVisible();

  await expect(page.getByText('Spotify', { exact: true })).toBeVisible();
  await page.getByLabel('Scene for Game').selectOption('game');

  await expect.poll(async () => (await calls(page)).some((call) =>
    call.name === 'scene_set' && call.args[0] === 'game.exe' && call.args[1] === 'game' && call.args[2] === true
  )).toBe(true);

  await page.getByLabel('Low latency policy').check();
  await expect.poll(async () => (await calls(page)).some((call) => call.name === 'low_latency' && call.args[0] === true)).toBe(true);

  await page.getByLabel('Signature strength').evaluate((element) => {
    const descriptor = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value');
    descriptor.set.call(element, '1.2');
    element.dispatchEvent(new Event('input', { bubbles: true }));
    element.dispatchEvent(new Event('change', { bubbles: true }));
  });
  await expect.poll(async () => (await calls(page)).some((call) => call.name === 'signature_strength' && Number(call.args[0]) === 1.2)).toBe(true);
});

test('Calibration is keyed to the selected headphone and auditions always restore saved state', async ({ page }) => {
  await installAdaptiveMock(page);
  await page.addInitScript(() => {
    class FakeAudioContext {
      constructor() {
        this.sampleRate = 8000;
        this.destination = {};
      }
      async resume() {}
      createBuffer(channels, frames) {
        const data = Array.from({ length: channels }, () => new Float32Array(frames));
        return { getChannelData: (channel) => data[channel] };
      }
      createBufferSource() {
        const source = {
          buffer: null,
          onended: null,
          connect() {},
          start() { setTimeout(() => source.onended?.(), 0); },
        };
        return source;
      }
      async close() {}
    }
    Object.defineProperty(window, 'AudioContext', { configurable: true, value: FakeAudioContext });
  });
  await page.goto('/');
  await page.getByRole('button', { name: 'Open Adaptive Tools' }).click();
  await page.getByRole('button', { name: 'Calibration' }).click();
  await expect(page.getByText('Acme One', { exact: true })).toBeVisible();

  const itd = page.getByLabel('ITD scale');
  await itd.evaluate((element) => {
    const descriptor = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value');
    descriptor.set.call(element, '1.08');
    element.dispatchEvent(new Event('input', { bubbles: true }));
    element.dispatchEvent(new Event('change', { bubbles: true }));
  });
  await page.getByRole('button', { name: 'Save personalized' }).click();

  await expect.poll(async () => (await calls(page)).some((call) =>
    call.name === 'spatial_calibration' && call.args[0] === true && Math.abs(Number(call.args[1]) - 1.08) < 0.001
  )).toBe(true);

  const stored = await page.evaluate(() => JSON.parse(localStorage.getItem('pulsefx.adaptiveTools.v1') || '{}'));
  expect(stored.calibrations['headphones/acme-one'].enabled).toBe(true);
  expect(stored.calibrations['headphones/acme-one'].itdScale).toBeCloseTo(1.08, 2);

  const beforeAudition = (await calls(page)).length;
  await page.getByRole('button', { name: 'Play A · Default' }).click();
  await expect.poll(async () => (await calls(page)).slice(beforeAudition)
    .filter((call) => call.name === 'spatial_calibration')
    .map((call) => Boolean(call.args[0]))).toEqual([false, true]);

  const guessA = page.getByRole('button', { name: 'X = A' });
  const guessB = page.getByRole('button', { name: 'X = B' });
  await page.getByRole('button', { name: 'New trial' }).click();
  await expect(guessA).toBeDisabled();
  await expect(guessB).toBeDisabled();
  await page.getByRole('button', { name: 'Play X' }).click();
  await expect(guessA).toBeEnabled();
  await expect(guessB).toBeEnabled();
  await guessA.click();
  await expect(guessA).toBeDisabled();
  await expect(guessB).toBeDisabled();

  await page.getByRole('button', { name: 'Use default' }).click();
  await expect.poll(async () => (await calls(page)).some((call) => call.name === 'spatial_calibration' && call.args[0] === false)).toBe(true);
});

test('Diagnostics renders native stream, latency, limiter, governor and dropout truth', async ({ page }) => {
  await installAdaptiveMock(page);
  await page.goto('/');
  await page.getByRole('button', { name: 'Open Adaptive Tools' }).click();
  await page.getByRole('button', { name: 'Diagnostics' }).click();

  await expect(page.getByText('48,000 Hz', { exact: true })).toBeVisible();
  await expect(page.getByText('5.0 ms', { exact: true })).toBeVisible();
  await expect(page.getByText('25.0 ms', { exact: true })).toBeVisible();
  await expect(page.getByText('0.42 dB', { exact: true })).toBeVisible();
  await expect(page.getByText('18%', { exact: true })).toBeVisible();
  await expect(page.getByText('100% wet', { exact: true })).toBeVisible();
  await expect(page.getByText('0 under · 0 over', { exact: true })).toBeVisible();
});
