const assert = require('node:assert/strict');
const { isSafeStreamUrl, sanitizeStation } = require('./radio.cjs');

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
  countrycode: 'MA',
  codec: 'MP3',
  bitrate: 128,
});
assert.equal(clean.name, 'Example FM');
assert.equal(clean.countrycode, 'MA');
assert.equal(clean.bitrate, 128);
assert.equal(sanitizeStation({ stationuuid: 'valid-uuid-1234', name: 'Local', url: 'http://10.0.0.1/live' }), null);

console.log('PulseFX radio sanitizer tests passed');
