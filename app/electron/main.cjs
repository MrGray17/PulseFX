const { app, BrowserWindow, ipcMain, dialog, globalShortcut, Menu, Tray } = require('electron');
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const readline = require('node:readline');
const { pathToFileURL } = require('node:url');
const autoeq = require('./autoeq.cjs');
const radio = require('./radio.cjs');

const allowedCommands = new Set([
  'ping', 'status', 'devices', 'apps', 'quit', 'output', 'enabled', 'night',
  'headphone_enable', 'headphone_profile', 'preamp', 'bass', 'clarity', 'fidelity', 'spatial',
  'surround', 'ambience', 'dynamics', 'pitch', 'eq', 'app_volume', 'app_mute',
]);
const audioExtensions = new Set(['.mp3', '.wav', '.flac', '.m4a', '.aac', '.ogg', '.opus', '.webm']);
const defaultShortcuts = {
  toggleProcessing: 'CommandOrControl+Alt+B',
  toggleSurround: 'CommandOrControl+Alt+3',
  showEqualizer: 'CommandOrControl+Alt+E',
  showPlayer: 'CommandOrControl+Alt+P',
};
const quickState = { enabled: true, surround: false };

let mainWindow = null;
let tray = null;
let hostProcess = null;
let hostReadline = null;
let hostStartup = null;
let restarting = null;
let appQuitting = false;
const pending = [];
const pendingQuickActions = [];

function settingsPath() {
  return path.join(app.getPath('userData'), 'settings.json');
}

function loadSettings() {
  try {
    const raw = fs.readFileSync(settingsPath(), 'utf8');
    const parsed = JSON.parse(raw);
    return parsed && typeof parsed === 'object' && !Array.isArray(parsed) ? parsed : {};
  } catch {
    return {};
  }
}

function normalizeShortcuts(value) {
  const source = value && typeof value === 'object' && !Array.isArray(value) ? value : {};
  const result = {};
  for (const [action, fallback] of Object.entries(defaultShortcuts)) {
    const candidate = typeof source[action] === 'string' ? source[action].trim() : '';
    result[action] = candidate.length > 0 && candidate.length <= 120 ? candidate : fallback;
  }
  return result;
}

function saveSettings(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    throw new Error('settings must be an object');
  }
  const persisted = { ...value, shortcuts: normalizeShortcuts(value.shortcuts) };
  const file = settingsPath();
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const temporary = `${file}.tmp`;
  fs.writeFileSync(temporary, JSON.stringify(persisted, null, 2), 'utf8');
  fs.renameSync(temporary, file);
  registerShortcuts(persisted.shortcuts);
  return true;
}

function findHostExecutable() {
  if (process.env.PULSEFX_AUDIO_HOST) return process.env.PULSEFX_AUDIO_HOST;
  if (app.isPackaged) {
    return path.join(process.resourcesPath, 'native', 'pulsefx_audio_host.exe');
  }

  const candidates = [
    path.resolve(__dirname, '../../build/Debug/pulsefx_audio_host.exe'),
    path.resolve(__dirname, '../../build/Release/pulsefx_audio_host.exe'),
    path.resolve(__dirname, '../../../build/Debug/pulsefx_audio_host.exe'),
    path.resolve(__dirname, '../../../build/Release/pulsefx_audio_host.exe'),
  ];
  return candidates.find((candidate) => fs.existsSync(candidate)) || candidates[0];
}

function rejectPending(error) {
  while (pending.length) {
    const request = pending.shift();
    clearTimeout(request.timer);
    request.reject(error);
  }
}

function rejectStartup(error) {
  if (!hostStartup) return;
  clearTimeout(hostStartup.timer);
  const reject = hostStartup.reject;
  hostStartup = null;
  reject(error);
}

function broadcast(channel, payload) {
  if (mainWindow && !mainWindow.isDestroyed()) mainWindow.webContents.send(channel, payload);
}

function rendererCanReceiveQuickActions() {
  return Boolean(
    mainWindow && !mainWindow.isDestroyed() &&
    mainWindow.webContents && !mainWindow.webContents.isDestroyed() &&
    !mainWindow.webContents.isLoadingMainFrame()
  );
}

function flushQuickActions() {
  if (!rendererCanReceiveQuickActions()) return;
  while (pendingQuickActions.length) {
    mainWindow.webContents.send('pulsefx:quick-action', pendingQuickActions.shift());
  }
}

function dispatchQuickAction(message) {
  if (!message || typeof message !== 'object') return;
  if (rendererCanReceiveQuickActions()) {
    mainWindow.webContents.send('pulsefx:quick-action', message);
    return;
  }
  pendingQuickActions.push(message);
}

function showWindow(tab) {
  if (!mainWindow || mainWindow.isDestroyed()) createWindow();
  if (tab) dispatchQuickAction({ action: 'tab', tab });
  mainWindow.show();
  mainWindow.restore();
  mainWindow.focus();
}

function updateQuickState(name, args) {
  if (name === 'enabled' && args.length === 1) quickState.enabled = String(args[0]) === 'true' || args[0] === true || args[0] === 1;
  if (name === 'surround' && args.length === 1) quickState.surround = Number(args[0]) > 0.001;
  refreshTrayMenu();
}

function scheduleRestart() {
  if (appQuitting || restarting) return;
  restarting = setTimeout(() => {
    restarting = null;
    startHost().catch((error) => {
      broadcast('pulsefx:host-state', { running: false, error: error.message });
      scheduleRestart();
    });
  }, 1000);
}

async function startHost() {
  if (hostProcess && !hostProcess.killed) {
    if (hostStartup) await hostStartup.promise;
    return;
  }

  const executable = findHostExecutable();
  if (!fs.existsSync(executable)) {
    throw new Error(`PulseFX native host was not found: ${executable}`);
  }

  let startupResolve;
  let startupReject;
  const startupPromise = new Promise((resolve, reject) => {
    startupResolve = resolve;
    startupReject = reject;
  });
  const startupTimer = setTimeout(() => {
    rejectStartup(new Error('PulseFX native host did not become ready in time'));
    hostProcess?.kill();
  }, 5000);
  hostStartup = { promise: startupPromise, resolve: startupResolve, reject: startupReject, timer: startupTimer };

  hostProcess = spawn(executable, [], {
    windowsHide: true,
    stdio: ['pipe', 'pipe', 'pipe'],
  });

  hostProcess.stdin.setDefaultEncoding('utf8');
  hostReadline = readline.createInterface({ input: hostProcess.stdout });
  hostReadline.on('line', (line) => {
    let message;
    try {
      message = JSON.parse(line);
    } catch {
      broadcast('pulsefx:host-log', { stream: 'stdout', message: line });
      return;
    }

    if (hostStartup && message?.type === 'status') {
      clearTimeout(hostStartup.timer);
      const resolve = hostStartup.resolve;
      hostStartup = null;
      broadcast('pulsefx:event', message);
      resolve();
      return;
    }

    if (pending.length) {
      const request = pending.shift();
      clearTimeout(request.timer);
      if (message && message.ok === false) request.reject(new Error(message.error || 'native host command failed'));
      else {
        updateQuickState(request.name, request.args);
        request.resolve(message);
      }
    } else {
      broadcast('pulsefx:event', message);
    }
  });

  hostProcess.stderr.on('data', (chunk) => {
    broadcast('pulsefx:host-log', { stream: 'stderr', message: chunk.toString('utf8') });
  });

  hostProcess.once('error', (error) => {
    rejectStartup(error);
    rejectPending(error);
    broadcast('pulsefx:host-state', { running: false, error: error.message });
  });

  hostProcess.once('exit', (code, signal) => {
    const error = new Error(`PulseFX native host exited (${code ?? signal ?? 'unknown'})`);
    rejectStartup(error);
    rejectPending(error);
    hostReadline?.close();
    hostReadline = null;
    hostProcess = null;
    broadcast('pulsefx:host-state', { running: false, error: error.message });
    scheduleRestart();
  });

  await startupPromise;
  broadcast('pulsefx:host-state', { running: true, error: '' });
}

function quoteHostArg(value) {
  const text = String(value);
  return `"${text
    .replaceAll('\\', '\\\\')
    .replaceAll('"', '\\"')
    .replaceAll('\n', '\\n')
    .replaceAll('\r', '\\r')
    .replaceAll('\t', '\\t')}"`;
}

async function commandNative(name, args = []) {
  if (!allowedCommands.has(name)) throw new Error('unsupported native host command');
  if (!Array.isArray(args) || args.length > 52) throw new Error('invalid command arguments');
  for (const arg of args) {
    if (!['string', 'number', 'boolean'].includes(typeof arg)) throw new Error('invalid command argument type');
    if (String(arg).length > 4096) throw new Error('command argument is too long');
  }

  await startHost();
  if (!hostProcess?.stdin?.writable) throw new Error('PulseFX native host is unavailable');
  const line = [name, ...args.map(quoteHostArg)].join(' ');

  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      const index = pending.findIndex((entry) => entry.resolve === resolve);
      if (index >= 0) pending.splice(index, 1);
      reject(new Error(`native host timed out handling ${name}`));
    }, 4000);
    pending.push({ name, args, resolve, reject, timer });
    hostProcess.stdin.write(`${line}\n`, 'utf8', (error) => {
      if (!error) return;
      const index = pending.findIndex((entry) => entry.resolve === resolve);
      if (index >= 0) pending.splice(index, 1);
      clearTimeout(timer);
      reject(error);
    });
  });
}

function quickToggleProcessing() {
  commandNative('enabled', [!quickState.enabled]).catch((error) => broadcast('pulsefx:host-state', { running: false, error: error.message }));
}

function quickToggleSurround() {
  const enabled = !quickState.surround;
  const action = { action: 'effect', id: 'surround', enabled };
  if (mainWindow && !mainWindow.isDestroyed()) {
    dispatchQuickAction(action);
  } else {
    // Apply audio immediately without opening a window, and keep the explicit
    // state queued so a later renderer recreation cannot display stale controls.
    pendingQuickActions.push(action);
    commandNative('surround', [enabled ? 0.48 : 0]).catch((error) => broadcast('pulsefx:host-state', { running: false, error: error.message }));
  }
}

function registerShortcuts(value) {
  if (!app.isReady()) return;
  globalShortcut.unregisterAll();
  const shortcuts = normalizeShortcuts(value);
  const actions = {
    toggleProcessing: quickToggleProcessing,
    toggleSurround: quickToggleSurround,
    showEqualizer: () => showWindow('equalizer'),
    showPlayer: () => showWindow('player'),
  };
  for (const [action, accelerator] of Object.entries(shortcuts)) {
    try {
      globalShortcut.register(accelerator, actions[action]);
    } catch {
      // Invalid/OS-reserved accelerators fail closed. Settings UI can replace it.
    }
  }
}

function trayTemplate() {
  return [
    { label: 'Open PulseFX', click: () => showWindow() },
    { type: 'separator' },
    { label: 'Audio Processing', type: 'checkbox', checked: quickState.enabled, click: quickToggleProcessing },
    { label: '3D Surround', type: 'checkbox', checked: quickState.surround, click: quickToggleSurround },
    {
      label: 'Equalizer Presets', submenu: ['Flat','Pop','Loud','Classical','Party','Reggae','Movie','Hip-hop','Jazz','Deep','Dubstep','Trap']
        .map((preset) => ({
          label: preset,
          click: () => {
            showWindow('equalizer');
            dispatchQuickAction({ action: 'preset', preset });
          },
        })),
    },
    { type: 'separator' },
    { label: 'Apps Volume Controller', click: () => showWindow('apps') },
    { label: 'Audio Player', click: () => showWindow('player') },
    { label: 'Internet Radio', click: () => showWindow('radio') },
    { type: 'separator' },
    { label: 'Quit PulseFX', click: () => app.quit() },
  ];
}

function refreshTrayMenu() {
  if (tray) tray.setContextMenu(Menu.buildFromTemplate(trayTemplate()));
}

async function createTray() {
  if (tray) return;
  const icon = await app.getFileIcon(process.execPath, { size: 'small' });
  tray = new Tray(icon);
  tray.setToolTip('PulseFX');
  tray.on('double-click', () => showWindow());
  refreshTrayMenu();
}

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1240,
    height: 820,
    minWidth: 1000,
    minHeight: 700,
    show: false,
    backgroundColor: '#08090d',
    autoHideMenuBar: true,
    webPreferences: {
      preload: path.join(__dirname, 'preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
      devTools: !app.isPackaged,
    },
  });

  const createdWindow = mainWindow;
  createdWindow.webContents.once('did-finish-load', () => {
    if (mainWindow === createdWindow) flushQuickActions();
  });
  createdWindow.once('ready-to-show', () => createdWindow.show());
  if (process.argv.includes('--dev')) {
    createdWindow.loadURL('http://127.0.0.1:5173');
  } else {
    createdWindow.loadFile(path.join(__dirname, '..', 'dist', 'index.html'));
  }
  createdWindow.on('closed', () => {
    if (mainWindow === createdWindow) mainWindow = null;
  });
}

ipcMain.handle('pulsefx:command', (_event, request) => {
  if (!request || typeof request.name !== 'string') throw new Error('invalid command request');
  return commandNative(request.name, request.args || []);
});
ipcMain.handle('pulsefx:settings:load', () => ({ ...loadSettings(), shortcuts: normalizeShortcuts(loadSettings().shortcuts) }));
ipcMain.handle('pulsefx:settings:save', (_event, value) => saveSettings(value));
ipcMain.handle('pulsefx:autoeq:list', async () => ({
  revision: autoeq.REVISION,
  models: await autoeq.loadIndex(app.getPath('userData')),
}));
ipcMain.handle('pulsefx:autoeq:apply', async (_event, modelPath) => {
  if (typeof modelPath !== 'string' || modelPath.length === 0 || modelPath.length > 1024) {
    throw new Error('invalid AutoEq model path');
  }
  const profile = await autoeq.loadProfile(app.getPath('userData'), modelPath);
  await commandNative('headphone_profile', autoeq.nativeArgs(profile));
  return {
    ok: true,
    revision: autoeq.REVISION,
    model: profile.model,
    preampDb: profile.preampDb,
    filters: profile.filters.length,
  };
});
ipcMain.handle('pulsefx:media:open', async () => {
  const result = await dialog.showOpenDialog(mainWindow ?? undefined, {
    title: 'Add audio to PulseFX',
    properties: ['openFile', 'multiSelections'],
    filters: [
      { name: 'Audio', extensions: ['mp3','wav','flac','m4a','aac','ogg','opus','webm'] },
      { name: 'All files', extensions: ['*'] },
    ],
  });
  if (result.canceled) return [];
  return result.filePaths
    .filter((file) => audioExtensions.has(path.extname(file).toLowerCase()))
    .slice(0, 500)
    .map((file) => ({
      id: file,
      name: path.basename(file, path.extname(file)),
      fileUrl: pathToFileURL(file).href,
    }));
});
ipcMain.handle('pulsefx:radio:search', async (_event, query) => {
  if (typeof query !== 'string' || query.length > 100) throw new Error('invalid radio search');
  return radio.searchStations(query);
});
ipcMain.handle('pulsefx:radio:click', async (_event, stationuuid) => radio.recordClick(stationuuid));

app.whenReady().then(async () => {
  createWindow();
  await createTray();
  registerShortcuts(loadSettings().shortcuts);
  try { await startHost(); } catch (error) {
    broadcast('pulsefx:host-state', { running: false, error: error.message });
    scheduleRestart();
  }
  app.on('activate', () => { if (BrowserWindow.getAllWindows().length === 0) createWindow(); });
});

app.on('window-all-closed', () => {
  // Keep the system-wide engine and Quick Controls alive in the tray.
});

app.on('before-quit', () => {
  appQuitting = true;
  globalShortcut.unregisterAll();
  if (restarting) clearTimeout(restarting);
  rejectStartup(new Error('PulseFX is shutting down'));
  rejectPending(new Error('PulseFX is shutting down'));
  if (hostProcess?.stdin?.writable) hostProcess.stdin.write('quit\n');
  hostProcess?.kill();
  tray?.destroy();
  tray = null;
});