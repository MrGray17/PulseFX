const dns = require('node:dns').promises;
const https = require('node:https');
const net = require('node:net');

const DISCOVERY_NAME = '_api._tcp.radio-browser.info';
const FALLBACK_SERVERS = ['de1.api.radio-browser.info'];
const MAX_RESPONSE_BYTES = 2 * 1024 * 1024;
const MAX_RESULTS = 80;
const MAX_COUNTRIES = 250;

function isPrivateIpv4(hostname) {
  const parts = hostname.split('.').map(Number);
  if (parts.length !== 4 || parts.some((part) => !Number.isInteger(part) || part < 0 || part > 255)) return false;
  return parts[0] === 10 ||
    parts[0] === 127 ||
    (parts[0] === 169 && parts[1] === 254) ||
    (parts[0] === 172 && parts[1] >= 16 && parts[1] <= 31) ||
    (parts[0] === 192 && parts[1] === 168) ||
    (parts[0] === 100 && parts[1] >= 64 && parts[1] <= 127);
}

function isSafeStreamUrl(value) {
  try {
    const url = new URL(value);
    if (!['http:', 'https:'].includes(url.protocol) || url.username || url.password) return false;
    const host = url.hostname.toLowerCase();
    if (!host || host === 'localhost' || host.endsWith('.localhost') || host.endsWith('.local')) return false;
    if (net.isIPv4(host) && isPrivateIpv4(host)) return false;
    if (net.isIPv6(host) && (host === '::1' || host.startsWith('fc') || host.startsWith('fd') || host.startsWith('fe80:'))) return false;
    return true;
  } catch {
    return false;
  }
}

async function discoverServers() {
  try {
    const records = await dns.resolveSrv(DISCOVERY_NAME);
    const servers = [...new Set(records.map((record) => String(record.name || '').replace(/\.$/, '').toLowerCase()))]
      .filter((name) => name.endsWith('.api.radio-browser.info'));
    if (servers.length) return servers.sort(() => Math.random() - 0.5);
  } catch {}
  return [...FALLBACK_SERVERS];
}

function requestJson(server, pathname, maxBytes = MAX_RESPONSE_BYTES) {
  return new Promise((resolve, reject) => {
    if (!server.endsWith('.api.radio-browser.info')) {
      reject(new Error('invalid Radio Browser server'));
      return;
    }
    const request = https.get({
      hostname: server,
      path: pathname,
      headers: {
        'User-Agent': 'PulseFX/0.2 (radio-browser client)',
        Accept: 'application/json',
      },
      timeout: 7000,
    }, (response) => {
      if (response.statusCode !== 200) {
        response.resume();
        reject(new Error(`Radio Browser returned HTTP ${response.statusCode ?? 0}`));
        return;
      }
      const chunks = [];
      let bytes = 0;
      response.on('data', (chunk) => {
        bytes += chunk.length;
        if (bytes > maxBytes) {
          request.destroy(new Error('Radio Browser response exceeded size limit'));
          return;
        }
        chunks.push(chunk);
      });
      response.on('end', () => {
        try { resolve(JSON.parse(Buffer.concat(chunks).toString('utf8'))); }
        catch { reject(new Error('Radio Browser returned invalid JSON')); }
      });
      response.on('error', reject);
    });
    request.on('timeout', () => request.destroy(new Error('Radio Browser request timed out')));
    request.on('error', reject);
  });
}

async function requestFromNetwork(pathname) {
  const servers = await discoverServers();
  let lastError = null;
  for (const server of servers) {
    try { return await requestJson(server, pathname); }
    catch (error) { lastError = error; }
  }
  throw lastError || new Error('No Radio Browser server is available');
}

function cleanText(value, maxLength = 200) {
  return String(value ?? '').replace(/[\u0000-\u001f\u007f]/g, ' ').trim().slice(0, maxLength);
}

function normalizeCountryCode(value) {
  const code = cleanText(value, 2).toUpperCase();
  return /^[A-Z]{2}$/.test(code) ? code : '';
}

function sanitizeStation(station) {
  const streamUrl = cleanText(station?.url_resolved || station?.url, 4096);
  if (!isSafeStreamUrl(streamUrl)) return null;
  const stationuuid = cleanText(station?.stationuuid, 80);
  if (!/^[a-zA-Z0-9-]{8,80}$/.test(stationuuid)) return null;
  const name = cleanText(station?.name, 180);
  if (!name) return null;
  const bitrate = Number(station?.bitrate);
  return {
    stationuuid,
    name,
    streamUrl,
    favicon: isSafeStreamUrl(cleanText(station?.favicon, 4096)) ? cleanText(station.favicon, 4096) : '',
    country: cleanText(station?.country, 100),
    countrycode: normalizeCountryCode(station?.countrycode),
    language: cleanText(station?.language, 120),
    tags: cleanText(station?.tags, 300),
    codec: cleanText(station?.codec, 20),
    bitrate: Number.isFinite(bitrate) && bitrate >= 0 && bitrate <= 5000 ? bitrate : 0,
  };
}

function sanitizeCountryCode(entry) {
  const code = normalizeCountryCode(entry?.name);
  if (!code) return null;
  const stationcount = Number(entry?.stationcount);
  return {
    code,
    stationcount: Number.isFinite(stationcount) && stationcount >= 0 ? Math.min(Math.trunc(stationcount), 1_000_000) : 0,
  };
}

function stationSearchPath({ query = '', countryCode = '' } = {}) {
  const text = cleanText(query, 100);
  const code = normalizeCountryCode(countryCode);
  const params = new URLSearchParams({
    hidebroken: 'true',
    limit: String(MAX_RESULTS),
    order: 'votes',
    reverse: 'true',
  });
  if (text) params.set('name', text);
  if (code) params.set('countrycode', code);
  return `/json/stations/search?${params.toString()}`;
}

function stationBrowsePath(mode, countryCode = '') {
  if (mode === 'popular') return `/json/stations/topvote/${MAX_RESULTS}?hidebroken=true`;
  if (mode === 'local' || mode === 'country') {
    const code = normalizeCountryCode(countryCode);
    if (!code) throw new Error('a two-letter country code is required for this radio view');
    return stationSearchPath({ countryCode: code });
  }
  throw new Error('unsupported radio browse mode');
}

function countryCodesPath() {
  const params = new URLSearchParams({
    hidebroken: 'true',
    order: 'stationcount',
    reverse: 'true',
    limit: String(MAX_COUNTRIES),
  });
  return `/json/countrycodes?${params.toString()}`;
}

async function stationsFromPath(pathname) {
  const raw = await requestFromNetwork(pathname);
  if (!Array.isArray(raw)) throw new Error('Radio Browser station response was not an array');
  return raw.map(sanitizeStation).filter(Boolean).slice(0, MAX_RESULTS);
}

async function searchStations(query = '') {
  return stationsFromPath(stationSearchPath({ query }));
}

async function browseStations(mode, countryCode = '') {
  return stationsFromPath(stationBrowsePath(mode, countryCode));
}

async function listCountryCodes() {
  const raw = await requestFromNetwork(countryCodesPath());
  if (!Array.isArray(raw)) throw new Error('Radio Browser country response was not an array');
  return raw.map(sanitizeCountryCode).filter(Boolean).slice(0, MAX_COUNTRIES);
}

async function recordClick(stationuuid) {
  const id = cleanText(stationuuid, 80);
  if (!/^[a-zA-Z0-9-]{8,80}$/.test(id)) return false;
  try {
    await requestFromNetwork(`/json/url/${encodeURIComponent(id)}`);
    return true;
  } catch {
    // Click telemetry must never stop playback.
    return false;
  }
}

module.exports = {
  browseStations,
  countryCodesPath,
  discoverServers,
  isSafeStreamUrl,
  listCountryCodes,
  normalizeCountryCode,
  recordClick,
  sanitizeCountryCode,
  sanitizeStation,
  searchStations,
  stationBrowsePath,
  stationSearchPath,
};
