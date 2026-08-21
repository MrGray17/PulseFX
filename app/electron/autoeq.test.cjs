const assert = require('node:assert/strict');
const { nativeArgs, parseProfile, parseRecommendedIndex } = require('./autoeq.cjs');

const catalog = parseRecommendedIndex([
  '# Recommended Results',
  ...Array.from({ length: 5001 }, (_, index) => `- [Model ${index}](./source/Model%20${index})`),
].join('\n'));
assert.equal(catalog.length, 5001);
assert.deepEqual(catalog[0], { name: 'Model 0', path: 'source/Model 0' });

const profile = parseProfile([
  'Preamp: -5.7 dB',
  'Filter 1: ON LSC Fc 105 Hz Gain 5.5 dB Q 0.71',
  'Filter 2: ON PK Fc 2650 Hz Gain -3.2 dB Q 1.42',
  'Filter 3: ON HSC Fc 10000 Hz Gain -1.5 dB Q 0.70',
].join('\n'));
assert.equal(profile.preampDb, -5.7);
assert.equal(profile.filters.length, 3);
assert.deepEqual(profile.filters.map((filter) => filter.type), ['LSC', 'PK', 'HSC']);
assert.deepEqual(nativeArgs(profile), [
  -5.7, 3,
  'LSC', 105, 0.71, 5.5,
  'PK', 2650, 1.42, -3.2,
  'HSC', 10000, 0.7, -1.5,
]);

assert.throws(() => parseProfile('Preamp: NaN dB\nFilter 1: ON PK Fc 1000 Hz Gain 0 dB Q 1'));
assert.throws(() => parseProfile('Preamp: 0 dB\nFilter 1: ON PK Fc 40000 Hz Gain 0 dB Q 1'));
assert.throws(() => parseRecommendedIndex('- [Only one](./source/Only%20one)'));

console.log('PulseFX AutoEq parser tests passed');
