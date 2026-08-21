const fs = require('node:fs');
const https = require('node:https');
const path = require('node:path');

const REVISION = '7ae0f56d53074872b028649617a22bbb4232feb7';
const RAW_HOST = 'raw.githubusercontent.com';
const BASE_PATH = `/jaakkopasanen/AutoEq/${REVISION}/results/`;
const INDEX_URL = `https://${RAW_HOST}${BASE_PATH}README.md`;
const MAX_INDEX_BYTES = 2 * 1024 * 1024;
const MAX_PROFILE_BYTES = 64 * 1024;
const MAX_FILTERS = 12;

function ensureDirectory(directory) {
  fs.mkdirSync(directory, { recursive: true });
}

function cacheDirectory(userData) {
  const directory = path.join(userData, 'autoeq', REVISION);
  ensureDirectory(directory);
  return directory;
}

function atomicWrite(file, value) {
  const temporary = `${file}.tmp`;
  fs.writeFileSync(temporary, value, 'utf8');
  fs.renameSync(temporary, file);
}

function requestText(url, maxBytes, redirectsRemaining = 2) {
  return new Promise((resolve, reject) => {
    let parsed;
    try {
      parsed = new URL(url);
    } catch {
      reject(new Error('invalid AutoEq URL'));
      return;
    }
    if (parsed.protocol !== 'https:' || parsed.hostname !== RAW_HOST || !parsed.pathname.startsWith(BASE_PATH)) {
      reject(new Error('AutoEq request escaped the pinned repository path'));
      return;
    }

    const request = https.get(parsed, {
      headers: {
        'User-Agent': 'PulseFX-AutoEq/1.0',
        Accept: 'text/plain',
      },
    }, (response) => {
      const status = response.statusCode ?? 0;
      if (status >= 300 && status < 400 && response.headers.location && redirectsRemaining > 0) {
        response.resume();
        let redirected;
        try { redirected = new URL(response.headers.location, parsed).toString(); } catch {
          reject(new Error('AutoEq returned an invalid redirect'));
          return;
        }
        requestText(redirected, maxBytes, redirectsRemaining - 1).then(resolve, reject);
        return;
      }
      if (status !== 200) {
        response.resume();
        reject(new Error(`AutoEq request failed with HTTP ${status}`));
        return;
      }

      const chunks = [];
      let bytes = 0;
      response.on('data', (chunk) => {
        bytes += chunk.length;
        if (bytes > maxBytes) {
          request.destroy(new Error('AutoEq response exceeded size limit'));
          return;
        }
        chunks.push(chunk);
      });
      response.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
      response.on('error', reject);
    });
    request.setTimeout(8000, () => request.destroy(new Error('AutoEq request timed out')));
    request.on('error', reject);
  });
}

function decodeCatalogPath(encoded) {
  try {
    const value = decodeURIComponent(encoded);
    if (!value || value.includes('..') || value.startsWith('/') || value.includes('\\')) return null;
    return value;
  } catch {
    return null;
  }
}

function parseRecommendedIndex(markdown) {
  const models = [];
  const seen = new Set();
  for (const line of markdown.split(/\r?\n/)) {
    const match = line.match(/^- \[([^\]]+)]\(\.\/([^)]+)\)\s*$/);
    if (!match) continue;
    const modelPath = decodeCatalogPath(match[2]);
    if (!modelPath || seen.has(modelPath)) continue;
    seen.add(modelPath);
    models.push({ name: match[1].trim(), path: modelPath });
  }
  if (models.length < 5000) throw new Error(`AutoEq recommended index was unexpectedly small (${models.length})`);
  return models;
}

function encodePath(value) {
  return value.split('/').map((part) => encodeURIComponent(part)).join('/');
}

function profileUrl(modelPath) {
  const clean = decodeCatalogPath(encodeURIComponent(modelPath).replaceAll('%2F', '/'));
  if (!clean) throw new Error('invalid AutoEq model path');
  const name = clean.split('/').at(-1);
  return `https://${RAW_HOST}${BASE_PATH}${encodePath(clean)}/${encodeURIComponent(`${name} ParametricEQ.txt`)}`;
}

function profileCacheFile(userData, modelPath) {
  const safe = Buffer.from(modelPath, 'utf8').toString('base64url');
  return path.join(cacheDirectory(userData), `profile-${safe}.txt`);
}

async function loadIndex(userData) {
  const file = path.join(cacheDirectory(userData), 'recommended-index.json');
  try {
    const parsed = JSON.parse(fs.readFileSync(file, 'utf8'));
    if (parsed?.revision === REVISION && Array.isArray(parsed.models) && parsed.models.length >= 5000) {
      return parsed.models;
    }
  } catch {}

  const markdown = await requestText(INDEX_URL, MAX_INDEX_BYTES);
  const models = parseRecommendedIndex(markdown);
  atomicWrite(file, JSON.stringify({ revision: REVISION, models }));
  return models;
}

function parseProfile(text) {
  let preampDb = 0;
  const filters = [];
  let sawPreamp = false;

  for (const rawLine of text.split(/\r?\n/)) {
    const line = rawLine.trim();
    if (!line) continue;
    const preamp = line.match(/^Preamp:\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+))\s*dB$/i);
    if (preamp) {
      preampDb = Number(preamp[1]);
      sawPreamp = true;
      continue;
    }
    const filter = line.match(/^Filter\s+\d+:\s+ON\s+(PK|LSC|HSC)\s+Fc\s+([+-]?(?:\d+(?:\.\d+)?|\.\d+))\s+Hz\s+Gain\s+([+-]?(?:\d+(?:\.\d+)?|\.\d+))\s+dB\s+Q\s+([+-]?(?:\d+(?:\.\d+)?|\.\d+))$/i);
    if (!filter) continue;
    filters.push({
      type: filter[1].toUpperCase(),
      frequency: Number(filter[2]),
      gainDb: Number(filter[3]),
      q: Number(filter[4]),
    });
  }

  if (!sawPreamp || !Number.isFinite(preampDb) || preampDb < -18 || preampDb > 6) {
    throw new Error('AutoEq profile has an invalid preamp');
  }
  if (filters.length === 0 || filters.length > MAX_FILTERS) {
    throw new Error(`AutoEq profile has an unsupported filter count (${filters.length})`);
  }
  for (const filter of filters) {
    if (!['PK', 'LSC', 'HSC'].includes(filter.type) ||
        !Number.isFinite(filter.frequency) || filter.frequency < 20 || filter.frequency > 20000 ||
        !Number.isFinite(filter.gainDb) || filter.gainDb < -12 || filter.gainDb > 12 ||
        !Number.isFinite(filter.q) || filter.q < 0.1 || filter.q > 12) {
      throw new Error('AutoEq profile contains an out-of-range filter');
    }
  }
  return { preampDb, filters };
}

async function loadProfile(userData, modelPath) {
  const models = await loadIndex(userData);
  const model = models.find((entry) => entry.path === modelPath);
  if (!model) throw new Error('Headphone model is not in the pinned AutoEq catalog');

  const cacheFile = profileCacheFile(userData, modelPath);
  let text;
  try {
    text = fs.readFileSync(cacheFile, 'utf8');
  } catch {
    text = await requestText(profileUrl(modelPath), MAX_PROFILE_BYTES);
    // Parse before caching so a bad upstream response never becomes sticky.
    parseProfile(text);
    atomicWrite(cacheFile, text);
  }
  return { model, ...parseProfile(text) };
}

function nativeArgs(profile) {
  const args = [profile.preampDb, profile.filters.length];
  for (const filter of profile.filters) {
    args.push(filter.type, filter.frequency, filter.q, filter.gainDb);
  }
  return args;
}

module.exports = {
  REVISION,
  loadIndex,
  loadProfile,
  nativeArgs,
  parseProfile,
  parseRecommendedIndex,
};
