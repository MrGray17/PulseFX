import { test, expect } from '@playwright/test';

async function installDesktopMock(page) {
  await page.addInitScript(() => {
    const calls = [];
    const listeners = { event: [], host: [], quick: [] };
    const push = (kind, payload = {}) => { calls.push({ kind, ...payload }); };

    window.__pulsefxCalls = calls;
    window.__emitEvent = (message) => listeners.event.forEach((callback) => callback(message));
    window.__emitHostState = (message) => listeners.host.forEach((callback) => callback(message));
    window.__emitQuickAction = (message) => listeners.quick.forEach((callback) => callback(message));

    window.pulsefx = {
      async command(name, ...args) {
        push('command', { name, args });
        if (name === 'status') {
          return {
            type: 'status', ok: true, running: true, routingActive: true, error: '',
            sourceId: 'pulsefx-output', destinationId: 'speakers-1',
            stats: { underruns: 0, overruns: 0, capturedFrames: 4096, renderedFrames: 4096, bufferedFrames: 256, controlRevision: 10, clockCorrectionPpm: 0 },
          };
        }
        if (name === 'devices') {
          return {
            type: 'devices', ok: true,
            devices: [
              { id: 'pulsefx-output', name: 'PulseFX Output', default: true, pulsefx: true },
              { id: 'speakers-1', name: 'Studio Speakers', default: false, pulsefx: false },
              { id: 'headphones-1', name: 'USB Headphones', default: false, pulsefx: false },
            ],
          };
        }
        if (name === 'apps') {
          return {
            type: 'apps', ok: true,
            apps: [{ pid: 4242, name: 'Spotify', volume: 0.65, muted: false }],
          };
        }
        if (name === 'output') {
          return {
            type: 'status', ok: true, running: true, routingActive: true, error: '',
            sourceId: 'pulsefx-output', destinationId: args[0] === 'auto' ? 'speakers-1' : args[0],
            stats: { underruns: 0, overruns: 0, bufferedFrames: 256, clockCorrectionPpm: 0 },
          };
        }
        return { type: 'ack', ok: true, command: name };
      },
      async loadSettings() {
        push('loadSettings');
        return {};
      },
      async saveSettings(settings) {
        push('saveSettings', { settings });
        return true;
      },
      async listHeadphones() {
        push('listHeadphones');
        return {
          revision: '7ae0f56d-test-revision',
          models: [
            { path: 'headphones/acme-studio-one', name: 'Acme Studio One' },
            { path: 'headphones/acme-travel-two', name: 'Acme Travel Two' },
          ],
        };
      },
      async applyHeadphoneProfile(modelPath) {
        push('applyHeadphoneProfile', { modelPath });
        const name = modelPath.endsWith('studio-one') ? 'Acme Studio One' : 'Acme Travel Two';
        return { ok: true, revision: '7ae0f56d-test-revision', model: { path: modelPath, name }, preampDb: -4.2, filters: 10 };
      },
      async openAudioFiles() {
        push('openAudioFiles');
        return [
          { id: 'C:/Music/Night Drive.flac', name: 'Night Drive', fileUrl: 'file:///C:/Music/Night%20Drive.flac' },
          { id: 'C:/Music/Sunrise.wav', name: 'Sunrise', fileUrl: 'file:///C:/Music/Sunrise.wav' },
        ];
      },
      async searchRadio(query = '') {
        push('searchRadio', { query });
        return [
          { stationuuid: 'station-1', name: query ? `Jazz ${query}` : 'Pulse Radio', country: 'Morocco', countrycode: 'MA', tags: 'music,chill', codec: 'MP3', bitrate: 192, streamUrl: 'https://radio.invalid/stream.mp3' },
          { stationuuid: 'station-2', name: 'Focus FM', country: 'France', countrycode: 'FR', tags: 'focus', codec: 'AAC', bitrate: 128, streamUrl: 'https://radio.invalid/focus.aac' },
        ];
      },
      async recordRadioClick(stationuuid) {
        push('recordRadioClick', { stationuuid });
        return true;
      },
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

async function setRange(locator, value) {
  await locator.evaluate((element, nextValue) => {
    const descriptor = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value');
    descriptor.set.call(element, String(nextValue));
    element.dispatchEvent(new Event('input', { bubbles: true }));
    element.dispatchEvent(new Event('change', { bubbles: true }));
  }, value);
}

async function expectNativeCommand(page, name, argsPredicate = () => true) {
  await expect.poll(async () => {
    const calls = await page.evaluate((commandName) => window.__pulsefxCalls
      .filter((call) => call.kind === 'command' && call.name === commandName)
      .map((call) => call.args), name);
    return calls.some(argsPredicate);
  }).toBe(true);
}

async function getCalls(page, kind) {
  return page.evaluate((requestedKind) => window.__pulsefxCalls.filter((call) => call.kind === requestedKind), kind);
}

async function openReadyApp(page, pageErrors) {
  page.on('pageerror', (error) => pageErrors.push(error.message));
  await installDesktopMock(page);
  await page.goto('/');
  await expect(page.getByText('Engine live')).toBeVisible();
  await expect(page.locator('.app-shell')).toBeVisible();
}

test('all primary controls, tabs and desktop bridge actions work', async ({ page }) => {
  const pageErrors = [];
  await openReadyApp(page, pageErrors);

  const master = page.locator('button.master');
  await expect(master).toContainText('On');
  await master.click();
  await expect(master).toContainText('Off');
  await expectNativeCommand(page, 'enabled', (args) => args[0] === false);
  await master.click();
  await expect(master).toContainText('On');
  await expectNativeCommand(page, 'enabled', (args) => args[0] === true);

  await page.getByLabel('Physical audio output').selectOption('headphones-1');
  await expectNativeCommand(page, 'output', (args) => args[0] === 'headphones-1');

  await page.getByRole('button', { name: '3D Surround' }).click();
  await expect(page.getByRole('heading', { name: '3D Surround' })).toBeVisible();
  await expectNativeCommand(page, 'surround', (args) => Number(args[0]) > 0);
  await setRange(page.getByLabel('3D Surround intensity'), 71);
  await expectNativeCommand(page, 'surround', (args) => Math.abs(Number(args[0]) - 0.71) < 0.001);

  await setRange(page.getByLabel('Pitch semitones'), 2.5);
  await expectNativeCommand(page, 'pitch', (args) => Number(args[0]) === 2.5);
  await setRange(page.getByLabel('Preamp'), 3);
  await expectNativeCommand(page, 'preamp', (args) => Number(args[0]) === 3);

  await page.getByRole('button', { name: 'Equalizer' }).click();
  await expect(page.getByRole('heading', { name: '31-band equalizer' })).toBeVisible();
  await page.getByRole('button', { name: 'Flat' }).click();
  await expectNativeCommand(page, 'eq', (args) => Number(args[0]) === 17 && Number(args[1]) === 0);
  await setRange(page.getByLabel('1k Hz'), 5);
  await expect(page.getByRole('button', { name: 'Custom' })).toBeVisible();
  await expectNativeCommand(page, 'eq', (args) => Number(args[0]) === 17 && Number(args[1]) === 5);

  await page.getByRole('button', { name: 'Headphones' }).click();
  await expect(page.getByRole('heading', { name: 'Headphone EQ' })).toBeVisible();
  await expect(page.getByRole('option', { name: 'Acme Studio One' })).toBeVisible();
  await page.getByRole('option', { name: 'Acme Studio One' }).click();
  await expect(page.getByText('Correction active')).toBeVisible();
  await expect.poll(async () => (await getCalls(page, 'applyHeadphoneProfile')).some((call) => call.modelPath === 'headphones/acme-studio-one')).toBe(true);
  await expectNativeCommand(page, 'headphone_enable', (args) => args[0] === true);
  await page.getByRole('button', { name: 'Enabled' }).click();
  await expectNativeCommand(page, 'headphone_enable', (args) => args[0] === false);

  await page.getByRole('button', { name: 'Apps' }).click();
  await expect(page.getByRole('heading', { name: 'Applications' })).toBeVisible();
  await expect(page.getByText('Spotify')).toBeVisible();
  await setRange(page.getByLabel('Spotify volume'), 0.32);
  await expectNativeCommand(page, 'app_volume', (args) => Number(args[0]) === 4242 && Math.abs(Number(args[1]) - 0.32) < 0.001);
  await page.getByRole('button', { name: 'Mute Spotify' }).click();
  await expectNativeCommand(page, 'app_mute', (args) => Number(args[0]) === 4242 && args[1] === true);
  await page.getByRole('button', { name: 'Refresh' }).click();
  await expect.poll(async () => (await getCalls(page, 'command')).filter((call) => call.name === 'apps').length).toBeGreaterThan(1);

  await page.getByRole('button', { name: 'Player' }).click();
  await expect(page.getByRole('heading', { name: 'Your music' })).toBeVisible();
  await page.getByRole('button', { name: /Add audio/ }).click();
  await expect(page.getByRole('button', { name: 'Night Drive', exact: true })).toBeVisible();
  await expect(page.locator('.transport-card.active')).toBeVisible();
  await expect(page.locator('.transport-meta strong')).toHaveText('Night Drive');
  await page.getByRole('button', { name: 'Pause' }).click();
  await setRange(page.getByLabel('Player volume'), 0.44);
  await expect(page.getByLabel('Player volume')).toHaveValue('0.44');

  await page.getByLabel('New playlist name').fill('Focus');
  await page.getByLabel('New playlist name').press('Enter');
  await expect(page.locator('.playlist-tab.active')).toContainText('Focus');
  await page.getByRole('button', { name: 'Delete active playlist' }).click();
  await expect(page.locator('.playlist-tab.active')).toContainText('Library');

  await page.getByRole('button', { name: 'Radio' }).click();
  await expect(page.getByRole('heading', { name: 'Live stations' })).toBeVisible();
  await expect(page.getByRole('button', { name: /Pulse Radio/ })).toBeVisible();
  await page.getByRole('button', { name: /Pulse Radio/ }).click();
  await expect(page.locator('.live-badge')).toContainText('LIVE');
  await expect.poll(async () => (await getCalls(page, 'recordRadioClick')).some((call) => call.stationuuid === 'station-1')).toBe(true);
  await page.getByLabel('Search radio stations').fill('lofi');
  await page.getByLabel('Search radio stations').press('Enter');
  await expect.poll(async () => (await getCalls(page, 'searchRadio')).some((call) => call.query === 'lofi')).toBe(true);
  await expect(page.getByRole('button', { name: /Jazz lofi/ })).toBeVisible();

  await page.getByRole('button', { name: 'Settings' }).click();
  await expect(page.getByRole('heading', { name: 'Global hotkeys' })).toBeVisible();
  const firstShortcut = page.getByLabel('Toggle processing shortcut');
  await firstShortcut.press('Control+Shift+9');
  await expect(firstShortcut).toHaveValue('Control+Shift+9');
  await page.getByRole('button', { name: 'Reset defaults' }).click();
  await expect(firstShortcut).toHaveValue('Control+Alt+B');

  await page.evaluate(() => window.__emitQuickAction({ action: 'tab', tab: 'equalizer' }));
  await expect(page.getByRole('heading', { name: '31-band equalizer' })).toBeVisible();
  await page.evaluate(() => window.__emitQuickAction({ action: 'preset', preset: 'Movie' }));
  await expect(page.getByRole('button', { name: 'Movie' })).toHaveClass(/active/);

  expect(pageErrors).toEqual([]);
});

test('every enhancement mode, preset and EQ band remains wired', async ({ page }) => {
  const pageErrors = [];
  await openReadyApp(page, pageErrors);

  // Default Spatial is on. Enabling Surround must disable it and every other
  // incompatible effect rather than allowing an unsupported combination.
  await page.getByRole('button', { name: '3D Surround' }).click();
  await expectNativeCommand(page, 'surround', (args) => Number(args[0]) > 0);
  await expectNativeCommand(page, 'spatial', (args) => Number(args[0]) === 0);

  await page.getByRole('button', { name: 'Ambience' }).click();
  await expectNativeCommand(page, 'ambience', (args) => Number(args[0]) > 0);
  await expectNativeCommand(page, 'surround', (args) => Number(args[0]) === 0);

  await page.getByRole('button', { name: 'Night Mode' }).click();
  await expectNativeCommand(page, 'night', (args) => args[0] === true);
  await expectNativeCommand(page, 'ambience', (args) => Number(args[0]) === 0);

  await page.getByRole('button', { name: 'Fidelity' }).click();
  await expectNativeCommand(page, 'fidelity', (args) => Number(args[0]) === 0);
  await page.getByRole('button', { name: 'Fidelity' }).click();
  await expectNativeCommand(page, 'fidelity', (args) => Number(args[0]) > 0);

  await page.getByRole('button', { name: 'Spatial' }).click();
  await expectNativeCommand(page, 'spatial', (args) => Number(args[0]) > 0);

  await page.getByRole('button', { name: 'Equalizer' }).click();
  const presets = ['Flat','Pop','Loud','Classical','Party','Reggae','Movie','Hip-hop','Jazz','Deep','Dubstep','Trap'];
  for (const preset of presets) {
    const button = page.getByRole('button', { name: preset, exact: true });
    await button.click();
    await expect(button).toHaveClass(/active/);
  }

  const bands = ['20','25','31','40','50','63','80','100','125','160','200','250','315','400','500','630','800','1k','1.25k','1.6k','2k','2.5k','3.15k','4k','5k','6.3k','8k','10k','12.5k','16k','20k'];
  for (let index = 0; index < bands.length; index += 1) {
    const value = index % 2 === 0 ? 1 : -1;
    await setRange(page.getByRole('slider', { name: `${bands[index]} Hz`, exact: true }), value);
  }
  const eqCalls = (await getCalls(page, 'command')).filter((call) => call.name === 'eq');
  for (let index = 0; index < bands.length; index += 1) {
    const expected = index % 2 === 0 ? 1 : -1;
    expect(eqCalls.some((call) => Number(call.args[0]) === index && Number(call.args[1]) === expected)).toBe(true);
  }

  expect(pageErrors).toEqual([]);
});

test('player edge cases, output exclusion and health errors are handled', async ({ page }) => {
  const pageErrors = [];
  await openReadyApp(page, pageErrors);

  // The virtual PulseFX endpoint must never be offered as its own physical sink.
  const output = page.getByLabel('Physical audio output');
  const values = await output.locator('option').evaluateAll((options) => options.map((option) => option.value));
  expect(values).not.toContain('pulsefx-output');
  expect(values).toContain('speakers-1');
  expect(values).toContain('headphones-1');

  // Enabling headphone correction without a selected model must redirect to the
  // model picker and must never activate an empty correction bank.
  const trueHeadphoneEnablesBefore = (await getCalls(page, 'command'))
    .filter((call) => call.name === 'headphone_enable' && call.args[0] === true).length;
  await page.locator('.headphone-toggle').click();
  await expect(page.getByRole('heading', { name: 'Headphone EQ' })).toBeVisible();
  await expect(page.getByRole('option', { name: 'Acme Studio One' })).toBeVisible();
  const trueHeadphoneEnablesAfter = (await getCalls(page, 'command'))
    .filter((call) => call.name === 'headphone_enable' && call.args[0] === true).length;
  expect(trueHeadphoneEnablesAfter).toBe(trueHeadphoneEnablesBefore);

  await page.getByRole('button', { name: 'Player' }).click();
  await page.getByRole('button', { name: /Add audio/ }).click();
  await expect(page.getByRole('button', { name: 'Night Drive', exact: true })).toHaveCount(1);
  await expect(page.getByRole('button', { name: 'Sunrise', exact: true })).toHaveCount(1);

  // Re-importing the exact same file set is de-duplicated by stable file id.
  await page.getByRole('button', { name: /Add audio/ }).click();
  await expect(page.getByRole('button', { name: 'Night Drive', exact: true })).toHaveCount(1);
  await expect(page.getByRole('button', { name: 'Sunrise', exact: true })).toHaveCount(1);

  await expect(page.locator('.transport-meta strong')).toHaveText('Night Drive');
  await page.getByRole('button', { name: 'Next track' }).click();
  await expect(page.locator('.transport-meta strong')).toHaveText('Sunrise');
  await page.getByRole('button', { name: 'Previous track' }).click();
  await expect(page.locator('.transport-meta strong')).toHaveText('Night Drive');

  await page.evaluate(() => window.__emitHostState({ running: false, error: 'Simulated native host failure' }));
  await expect(page.getByText('Simulated native host failure')).toBeVisible();
  await expect(page.getByText('Audio issue')).toBeVisible();
  await page.evaluate(() => window.__emitHostState({ running: true, error: '' }));
  await expect(page.getByText('Simulated native host failure')).toHaveCount(0);

  await page.evaluate(() => window.__emitEvent({
    type: 'status', ok: true, running: true, routingActive: false,
    error: 'PulseFX Output is not the Windows default playback device; system audio may bypass processing',
    stats: { underruns: 0, overruns: 0, bufferedFrames: 128, clockCorrectionPpm: 0 },
  }));
  await expect(page.getByText(/system audio may bypass processing/)).toBeVisible();

  expect(pageErrors).toEqual([]);
});

test('critical UI remains usable at the minimum supported window size', async ({ page }) => {
  const pageErrors = [];
  await page.setViewportSize({ width: 1000, height: 700 });
  await openReadyApp(page, pageErrors);

  for (const tab of ['Enhance', 'Equalizer', 'Headphones', 'Apps', 'Player', 'Radio', 'Settings']) {
    await page.getByRole('button', { name: tab }).click();
    await expect(page.locator('.content')).toBeVisible();
    const geometry = await page.locator('.content').evaluate((element) => {
      const rect = element.getBoundingClientRect();
      return { left: rect.left, right: rect.right, viewport: window.innerWidth };
    });
    expect(geometry.left).toBeGreaterThanOrEqual(79);
    expect(geometry.right).toBeLessThanOrEqual(geometry.viewport + 1);
  }

  await expect(page.locator('.sidebar')).toBeVisible();
  await expect(page.locator('button.master')).toBeVisible();
  expect(pageErrors).toEqual([]);
});