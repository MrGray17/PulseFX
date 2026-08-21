import { test, expect } from '@playwright/test';

async function installDesktopMock(page) {
  await page.addInitScript(() => {
    const listeners = { event: [], host: [], quick: [] };
    window.__emitQuickAction = (message) => listeners.quick.forEach((callback) => callback(message));

    window.pulsefx = {
      async command(name, ...args) {
        if (name === 'status') {
          return {
            type: 'status', ok: true, running: true, routingActive: true, error: '',
            sourceId: 'pulsefx-output', destinationId: 'speakers-1',
            stats: { underruns: 0, overruns: 0, capturedFrames: 4096, renderedFrames: 4096, bufferedFrames: 256, controlRevision: 1, clockCorrectionPpm: 0 },
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
        return { type: 'ack', ok: true, command: name, args };
      },
      async loadSettings() { return {}; },
      async saveSettings() { return true; },
      async listHeadphones() { return { revision: 'test', models: [] }; },
      async applyHeadphoneProfile() { return { ok: false }; },
      async openAudioFiles() { return []; },
      async searchRadio() { return []; },
      async recordRadioClick() { return true; },
      onEvent(callback) { listeners.event.push(callback); return () => { listeners.event = listeners.event.filter((item) => item !== callback); }; },
      onHostState(callback) { listeners.host.push(callback); return () => { listeners.host = listeners.host.filter((item) => item !== callback); }; },
      onQuickAction(callback) { listeners.quick.push(callback); return () => { listeners.quick = listeners.quick.filter((item) => item !== callback); }; },
    };

    Object.defineProperty(HTMLMediaElement.prototype, 'paused', {
      configurable: true,
      get() { return this.__pulsefxPaused !== false; },
    });
    HTMLMediaElement.prototype.load = function load() {};
    HTMLMediaElement.prototype.play = function play() {
      this.__pulsefxPaused = false;
      this.dispatchEvent(new Event('play'));
      return Promise.resolve();
    };
    HTMLMediaElement.prototype.pause = function pause() {
      this.__pulsefxPaused = true;
      this.dispatchEvent(new Event('pause'));
    };
  });
}

test('associated audio launch opens Player, deduplicates Library and autoplays', async ({ page }) => {
  await installDesktopMock(page);
  await page.goto('/');
  await expect(page.getByText('Engine live')).toBeVisible();

  const explorerFile = {
    id: 'C:/Music/Explorer Song.flac',
    name: 'Explorer Song',
    fileUrl: 'file:///C:/Music/Explorer%20Song.flac',
  };

  await page.evaluate((file) => window.__emitQuickAction({ action: 'open-files', files: [file] }), explorerFile);
  await expect(page.getByRole('heading', { name: 'Your music' })).toBeVisible();
  await expect(page.getByRole('button', { name: 'Explorer Song', exact: true })).toHaveCount(1);
  await expect(page.locator('.transport-meta strong')).toHaveText('Explorer Song');
  await expect(page.getByRole('button', { name: 'Pause' })).toBeVisible();

  // Reopening the same associated file promotes it to the front but never
  // duplicates the Library entry.
  await page.evaluate((file) => window.__emitQuickAction({ action: 'open-files', files: [file] }), explorerFile);
  await expect(page.getByRole('button', { name: 'Explorer Song', exact: true })).toHaveCount(1);
  await expect(page.locator('.transport-meta strong')).toHaveText('Explorer Song');

  // Renderer-side validation must reject anything that did not survive the
  // trusted main-process file validation contract.
  await page.evaluate(() => window.__emitQuickAction({
    action: 'open-files',
    files: [{ id: 'https://example.invalid/not-local.mp3', name: 'Remote', fileUrl: 'https://example.invalid/not-local.mp3' }],
  }));
  await expect(page.getByRole('button', { name: 'Remote', exact: true })).toHaveCount(0);
});
