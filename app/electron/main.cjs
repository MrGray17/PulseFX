const { app, BrowserWindow, ipcMain } = require('electron');
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const readline = require('node:readline');

const allowedCommands = new Set([
  'ping', 'status', 'devices', 'apps', 'quit', 'output', 'enabled', 'night',
  'headphone_enable', 'preamp', 'bass', 'clarity', 'fidelity', 'spatial',
  'surround', 'ambience', 'dynamics', 'eq', 'app_volume', 'app_mute',
]);

let mainWindow = null;
let hostProcess = null;
let hostReadline = null;
let hostStartup = null;
let restarting = null;
let appQuitting = false;
const pending = [];

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

function saveSettings(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    throw new Error('settings must be an object');
  }
  const file = settingsPath();
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, JSON.stringify(value, null, 2), 'utf8');
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

    // The native host emits exactly one startup status before it reads stdin.
    // Consume that handshake before resolving any request/response command.
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
      else request.resolve(message);
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
  if (!Array.isArray(args) || args.length > 40) throw new Error('invalid command arguments');
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
    pending.push({ resolve, reject, timer });
    hostProcess.stdin.write(`${line}\n`, 'utf8', (error) => {
      if (!error) return;
      const index = pending.findIndex((entry) => entry.resolve === resolve);
      if (index >= 0) pending.splice(index, 1);
      clearTimeout(timer);
      reject(error);
    });
  });
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

  mainWindow.once('ready-to-show', () => mainWindow.show());
  if (process.argv.includes('--dev')) {
    mainWindow.loadURL('http://127.0.0.1:5173');
  } else {
    mainWindow.loadFile(path.join(__dirname, '..', 'dist', 'index.html'));
  }
  mainWindow.on('closed', () => { mainWindow = null; });
}

ipcMain.handle('pulsefx:command', (_event, request) => {
  if (!request || typeof request.name !== 'string') throw new Error('invalid command request');
  return commandNative(request.name, request.args || []);
});
ipcMain.handle('pulsefx:settings:load', () => loadSettings());
ipcMain.handle('pulsefx:settings:save', (_event, value) => saveSettings(value));

app.whenReady().then(async () => {
  createWindow();
  try { await startHost(); } catch (error) {
    broadcast('pulsefx:host-state', { running: false, error: error.message });
    scheduleRestart();
  }
  app.on('activate', () => { if (BrowserWindow.getAllWindows().length === 0) createWindow(); });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit();
});

app.on('before-quit', () => {
  appQuitting = true;
  if (restarting) clearTimeout(restarting);
  rejectStartup(new Error('PulseFX is shutting down'));
  rejectPending(new Error('PulseFX is shutting down'));
  if (hostProcess?.stdin?.writable) hostProcess.stdin.write('quit\n');
  hostProcess?.kill();
});
