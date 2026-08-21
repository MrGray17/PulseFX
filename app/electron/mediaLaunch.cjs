const fs = require('node:fs');
const path = require('node:path');
const { pathToFileURL } = require('node:url');

const AUDIO_EXTENSIONS = new Set(['.mp3', '.wav', '.flac', '.m4a', '.aac', '.ogg', '.opus', '.webm']);
const MAX_LAUNCH_FILES = 100;

function normalizeCandidate(value, workingDirectory = process.cwd()) {
  if (typeof value !== 'string' || value.length === 0 || value.length > 32767) return '';
  if (value.startsWith('--')) return '';

  const candidate = path.isAbsolute(value) ? path.normalize(value) : path.resolve(workingDirectory, value);
  if (!AUDIO_EXTENSIONS.has(path.extname(candidate).toLowerCase())) return '';

  try {
    const stat = fs.statSync(candidate);
    return stat.isFile() ? candidate : '';
  } catch {
    return '';
  }
}

function mediaItemFromPath(file) {
  return {
    id: file,
    name: path.basename(file, path.extname(file)),
    fileUrl: pathToFileURL(file).href,
  };
}

function mediaItemsFromArgv(argv, workingDirectory = process.cwd()) {
  if (!Array.isArray(argv)) return [];
  const seen = new Set();
  const files = [];

  for (const value of argv) {
    const file = normalizeCandidate(value, workingDirectory);
    if (!file) continue;
    const key = process.platform === 'win32' ? file.toLocaleLowerCase() : file;
    if (seen.has(key)) continue;
    seen.add(key);
    files.push(mediaItemFromPath(file));
    if (files.length >= MAX_LAUNCH_FILES) break;
  }
  return files;
}

function openFilesAction(argv, workingDirectory = process.cwd()) {
  const files = mediaItemsFromArgv(argv, workingDirectory);
  return files.length > 0 ? { action: 'open-files', files } : null;
}

module.exports = {
  AUDIO_EXTENSIONS,
  MAX_LAUNCH_FILES,
  mediaItemFromPath,
  mediaItemsFromArgv,
  normalizeCandidate,
  openFilesAction,
};
