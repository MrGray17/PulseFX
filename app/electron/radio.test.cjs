const assert = require('node:assert/strict');
const {
  countryCodesPath,
  isSafeStreamUrl,
  normalizeCountryCode,
  sanitizeCountryCode,
  sanitizeStation,
  stationBrowsePath,
  stationSearchPath,
} = require('./radio.cjs');

assert.equal(isSafeStreamUrl('https://example.org/live.mp3'), true);
assert.equal(isSafeStreamUrl('http://radio.example.org:8000/live'), true);
assert.equal(isSafeStreamUrl('file:///C:/secret.txt'), false);
assert.equal(isSafeStreamUrl('http://127.0.0.1/live'), false);
assert.equal(isSafeStreamUrl('http://192.168.1.2/live'), false);
assert.equal(isSafeStreamUrl('http://localhost/live'), false);

const clean = sanitizeStation({
  stationuuid: '96202f73-0601-11e8-ae97-52543be04c81',
  name: 'Example FM',
  url_resolved: 'https://example.org/live.mp3',
  country: 'Morocco',
  countrycode: 'ma',
  codec: 'MP3',
  bitrate: 128,
});
assert.equal(clean.name, 'Example FM');
assert.equal(clean.countrycode, 'MA');
assert.equal(clean.bitrate, 128);
assert.equal(sanitizeStation({ stationuuid: 'valid-uuid-1234', name: 'Local', url: 'http://10.0.0.1/live' }), null);

assert.equal(normalizeCountryCode('ma'), 'MA');
assert.equal(normalizeCountryCode('USA'), '');
assert.deepEqual(sanitizeCountryCode({ name: 'MA', stationcount: '42' }), { code: 'MA', stationcount: 42 });
assert.equal(sanitizeCountryCode({ name: '<script>', stationcount: '4' }), null);

const popularPath = stationBrowsePath('popular');
assert.match(popularPath, /^\/json\/stations\/topvote\/80\?hidebroken=true$/);
const localPath = stationBrowsePath('local', 'ma');
assert.match(localPath, /countrycode=MA/);
assert.match(localPath, /hidebroken=true/);
assert.throws(() => stationBrowsePath('local', ''), /two-letter country code/);
assert.throws(() => stationBrowsePath('country', 'MOR'), /two-letter country code/);
assert.throws(() => stationBrowsePath('unknown', 'MA'), /unsupported radio browse mode/);

const searchPath = stationSearchPath({ query: 'Jazz & Blues', countryCode: 'fr' });
assert.match(searchPath, /name=Jazz%20%26%20Blues|name=Jazz\+%26\+Blues/);
assert.match(searchPath, /countrycode=FR/);
assert.match(countryCodesPath(), /^\/json\/countrycodes\?/);
assert.match(countryCodesPath(), /order=stationcount/);

console.log('PulseFX radio sanitizer and browse-path tests passed');
