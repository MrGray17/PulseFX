import { test, expect } from '@playwright/test';

async function installModeMock(page, savedSettings = {}) {
  await page.addInitScript((saved) => {
    const calls = [];
    const listeners = { event: [], host: [], quick: [] };
    window.__pulsefxModeCalls = calls;

    window.pulsefx = {
      async command(name, ...args) {
        calls.push({ kind: 'command', name, args });
        if (name === 'status') {
          return {
            type: 'status', ok: true, running: true, routingActive: true, error: '',
            sourceId: 'pulsefx-output', destinationId: 'speakers-1',
            controls: { mode: 'signature', enabled: true, pitchSemitones: 0 },
            stats: { underruns: 0, overruns: 0, capturedFrames: 4096, renderedFrames: 4096, bufferedFrames: 256, controlRevision: 10, clockCorrectionPpm: 0 },
          };
        }
        if (name === 'devices') {
          return {
            type: 'devices', ok: true,
            devices: [
              { id: 'pulsefx-output', name: 'PulseFX Output', default: true, pulsefx: true },
              { id: 'speakers-1', name: 'Studio Speakers', default: false, pulsefx: false },
            ],
          };
        }
        if (name === 'apps') return { type: 'apps', ok: true, apps: [] };
        return { type: 'ack', ok: true, command: name };
      },
      async loadSettings() {
        calls.push({ kind: 'loadSettings' });
        return saved;
      },
      async saveSettings(settings) {
        calls.push({ kind: 'saveSettings', settings });
        return true;
      },
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
  }, savedSettings);
}

async function calls(page) {
  return page.evaluate(() => window.__pulsefxModeCalls ?? []);
}

test('fresh install finishes startup in Signature mode with a flat EQ baseline', async ({ page }) => {
  await installModeMock(page, {});
  await page.goto('/');
  await expect(page.getByRole('button', { name: 'Toggle PulseFX processing' })).toBeVisible();

  await expect.poll(async () => {
    const history = await calls(page);
    return history.some((entry) => entry.kind === 'command' && entry.name === 'mode' && entry.args[0] === 'signature');
  }).toBe(true);

  const history = await calls(page);
  const startupEq = history.filter((entry) => entry.kind === 'command' && entry.name === 'eq');
  expect(startupEq).toHaveLength(31);
  expect(startupEq.every((entry) => Number(entry.args[1]) === 0)).toBe(true);

  await expect.poll(async () => {
    const saves = (await calls(page)).filter((entry) => entry.kind === 'saveSettings');
    return saves.at(-1)?.settings?.mode;
  }).toBe('signature');
});

test('legacy saved sound settings migrate to Manual mode without changing their EQ', async ({ page }) => {
  const legacyEq = Array.from({ length: 31 }, (_, index) => index % 3 - 1);
  await installModeMock(page, {
    enabled: true,
    preamp: 1.5,
    pitch: 0,
    preset: 'Custom',
    eq: legacyEq,
    effectEnabled: { fidelity: true, spatial: true },
    effectAmounts: { fidelity: 42, spatial: 34 },
  });
  await page.goto('/');

  await expect.poll(async () => {
    const history = await calls(page);
    return history.some((entry) => entry.kind === 'command' && entry.name === 'mode' && entry.args[0] === 'manual');
  }).toBe(true);

  const history = await calls(page);
  const startupEq = history.filter((entry) => entry.kind === 'command' && entry.name === 'eq');
  expect(startupEq).toHaveLength(31);
  expect(startupEq.map((entry) => Number(entry.args[1]))).toEqual(legacyEq);
});

test('post-hydration manual sound changes persist Manual mode', async ({ page }) => {
  await installModeMock(page, {});
  await page.goto('/');

  await expect.poll(async () => {
    const history = await calls(page);
    return history.some((entry) => entry.kind === 'command' && entry.name === 'mode' && entry.args[0] === 'signature');
  }).toBe(true);

  const preamp = page.getByLabel('Preamp');
  await preamp.evaluate((element) => {
    const descriptor = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value');
    descriptor.set.call(element, '2');
    element.dispatchEvent(new Event('input', { bubbles: true }));
    element.dispatchEvent(new Event('change', { bubbles: true }));
  });

  await expect.poll(async () => {
    const saves = (await calls(page)).filter((entry) => entry.kind === 'saveSettings');
    return saves.at(-1)?.settings?.mode;
  }).toBe('manual');
});

test('titlebar mode control follows automatic and explicit mode changes', async ({ page }) => {
  await installModeMock(page, {});
  await page.goto('/');

  const group = page.getByRole('group', { name: 'PulseFX processing mode' });
  const signature = group.getByRole('button', { name: /Signature/ });
  const manual = group.getByRole('button', { name: /Manual/ });
  await expect(group).toBeVisible();
  await expect(signature).toHaveAttribute('aria-pressed', 'true');
  await expect(manual).toHaveAttribute('aria-pressed', 'false');

  await manual.click();
  await expect(manual).toHaveAttribute('aria-pressed', 'true');
  await expect.poll(async () => {
    const history = await calls(page);
    return history.filter((entry) => entry.kind === 'command' && entry.name === 'mode').at(-1)?.args?.[0];
  }).toBe('manual');

  await signature.click();
  await expect(signature).toHaveAttribute('aria-pressed', 'true');
  await expect.poll(async () => {
    const history = await calls(page);
    return history.filter((entry) => entry.kind === 'command' && entry.name === 'mode').at(-1)?.args?.[0];
  }).toBe('signature');

  const preamp = page.getByLabel('Preamp');
  await preamp.evaluate((element) => {
    const descriptor = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value');
    descriptor.set.call(element, '1');
    element.dispatchEvent(new Event('input', { bubbles: true }));
    element.dispatchEvent(new Event('change', { bubbles: true }));
  });
  await expect(manual).toHaveAttribute('aria-pressed', 'true');
});
