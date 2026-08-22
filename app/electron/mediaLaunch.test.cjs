const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { mediaItemsFromArgv, normalizeCandidate, openFilesAction } = require('./mediaLaunch.cjs');

const root = fs.mkdtempSync(path.join(os.tmpdir(), 'pulsefx-media-launch-'));
try {
  const song = path.join(root, 'Night Drive.flac');
  const second = path.join(root, 'Sunrise.WAV');
  const text = path.join(root, 'notes.txt');
  fs.writeFileSync(song, 'audio');
  fs.writeFileSync(second, 'audio');
  fs.writeFileSync(text, 'not audio');

  assert.equal(normalizeCandidate(song, root), path.normalize(song));
  assert.equal(normalizeCandidate('Night Drive.flac', root), path.normalize(song));
  assert.equal(normalizeCandidate(text, root), '');
  assert.equal(normalizeCandidate('--original-process-start-time=123', root), '');
  assert.equal(normalizeCandidate(path.join(root, 'missing.mp3'), root), '');

  const items = mediaItemsFromArgv([
    process.execPath,
    '--original-process-start-time=123',
    song,
    song,
    'Sunrise.WAV',
    text,
  ], root);
  assert.equal(items.length, 2);
  assert.equal(items[0].id, path.normalize(song));
  assert.equal(items[0].name, 'Night Drive');
  assert.ok(items[0].fileUrl.startsWith('file:'));
  assert.equal(items[1].id, path.normalize(second));
  assert.equal(items[1].name, 'Sunrise');

  const action = openFilesAction([song], root);
  assert.equal(action.action, 'open-files');
  assert.equal(action.files.length, 1);
  assert.equal(openFilesAction(['--flag', text], root), null);

  console.log('PulseFX associated-media launch parser tests passed');
} finally {
  fs.rmSync(root, { recursive: true, force: true });
}
