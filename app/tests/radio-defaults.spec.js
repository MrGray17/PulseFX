import { test, expect } from '@playwright/test';

async function installDesktopMock(page) {
  await page.addInitScript(() => {
    const calls = [];
    const listeners = { event: [], host: [], quick: [] };
    window.__pulsefxCalls = calls;

    const station = (id, name, countrycode) => ({
      stationuuid: id,
      name,
      country: countrycode === 'MA' ? 'Morocco' : countrycode === 'FR' ? 'France' : 'Global',
      countrycode,
      tags: 'music',
      codec: 'MP3',
      bitrate: 192,
      streamUrl: `https://radio.invalid/${id}.mp3`,
    });

    window.pulsefx = {
      async command(name) {
        if (name === 'status') return { type: 'status', ok: true, running: true, routingActive: true, error: '', destinationId: 'speakers-1', stats: {} };
        if (name === 'devices') return { type: 'devices', ok: true, devices: [{ id: 'pulsefx-output', name: 'PulseFX Output', pulsefx: true }, { id: 'speakers-1', name: 'Speakers', pulsefx: false }] };
        if (name === 'apps') return { type: 'apps', ok: true, apps: [] };
        return { type: 'ack', ok: true, command: name };
      },
      async loadSettings() { return {}; },
      async saveSettings() { return true; },
      async listHeadphones() { return { revision: 'test', models: [] }; },
      async applyHeadphoneProfile() { return { ok: false }; },
      async openAudioFiles() { return []; },
      async searchRadio(query) {
        calls.push({ kind: 'searchRadio', query });
        return [station('search-0001', `Search ${query || 'All'}`, 'MA')];
      },
      async browseRadio(mode, countryCode) {
        calls.push({ kind: 'browseRadio', mode, countryCode });
        if (mode === 'popular') return [station('popular-01', 'Popular One', 'FR')];
        if (mode === 'local') return [station('local-0001', 'Local Morocco', 'MA')];
        if (mode === 'country' && countryCode === 'FR') return [station('country-fr1', 'France One', 'FR')];
        return [];
      },
      async listRadioCountries() {
        calls.push({ kind: 'listRadioCountries' });
        return [{ code: 'MA', stationcount: 120 }, { code: 'FR', stationcount: 240 }];
      },
      async getLocaleCountryCode() {
        calls.push({ kind: 'getLocaleCountryCode' });
        return 'MA';
      },
      async openDefaultApps() {
        calls.push({ kind: 'openDefaultApps' });
        return true;
      },
      async recordRadioClick(stationuuid) { calls.push({ kind: 'recordRadioClick', stationuuid }); return true; },
      onEvent(callback) { listeners.event.push(callback); return () => {}; },
      onHostState(callback) { listeners.host.push(callback); return () => {}; },
      onQuickAction(callback) { listeners.quick.push(callback); return () => {}; },
    };

    Object.defineProperty(HTMLMediaElement.prototype, 'paused', {
      configurable: true,
      get() { return this.__pulsefxPaused !== false; },
    });
    HTMLMediaElement.prototype.load = function load() {};
    HTMLMediaElement.prototype.play = function play() { this.__pulsefxPaused = false; this.dispatchEvent(new Event('play')); return Promise.resolve(); };
    HTMLMediaElement.prototype.pause = function pause() { this.__pulsefxPaused = true; this.dispatchEvent(new Event('pause')); };
  });
}

test('radio supports popular, local and country browsing and Player opens Windows Default Apps', async ({ page }) => {
  await installDesktopMock(page);
  await page.goto('/');
  await expect(page.getByText('Engine live')).toBeVisible();

  await page.getByRole('button', { name: 'Radio' }).click();
  await expect(page.getByRole('heading', { name: 'Live stations' })).toBeVisible();
  await expect(page.getByRole('button', { name: /Popular One/ })).toBeVisible();
  await expect(page.getByRole('button', { name: /Local · MA/ })).toBeVisible();
  await expect.poll(() => page.evaluate(() => window.__pulsefxCalls.some((call) => call.kind === 'browseRadio' && call.mode === 'popular'))).toBe(true);

  await page.getByRole('button', { name: /Local · MA/ }).click();
  await expect(page.getByRole('button', { name: /Local Morocco/ })).toBeVisible();
  await expect.poll(() => page.evaluate(() => window.__pulsefxCalls.some((call) => call.kind === 'browseRadio' && call.mode === 'local'))).toBe(true);

  await page.getByLabel('Radio country').selectOption('FR');
  await expect(page.getByRole('button', { name: /France One/ })).toBeVisible();
  await expect.poll(() => page.evaluate(() => window.__pulsefxCalls.some((call) => call.kind === 'browseRadio' && call.mode === 'country' && call.countryCode === 'FR'))).toBe(true);

  await page.getByLabel('Search radio stations').fill('jazz');
  await page.getByLabel('Search radio stations').press('Enter');
  await expect(page.getByRole('button', { name: /Search jazz/ })).toBeVisible();

  await page.getByRole('button', { name: 'Player' }).click();
  await page.getByRole('button', { name: 'Windows Default Apps' }).click();
  await expect.poll(() => page.evaluate(() => window.__pulsefxCalls.some((call) => call.kind === 'openDefaultApps'))).toBe(true);
});
