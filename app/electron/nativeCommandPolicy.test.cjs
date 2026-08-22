const assert = require('node:assert/strict');
const { allowedCommands, validateNativeCommand } = require('./nativeCommandPolicy.cjs');

const rendererCommands = [
  'ping', 'status', 'devices', 'apps', 'quit', 'mode', 'output', 'enabled', 'night',
  'headphone_enable', 'headphone_profile', 'preamp', 'bass', 'clarity', 'fidelity', 'spatial',
  'surround', 'ambience', 'dynamics', 'pitch', 'eq', 'app_volume', 'app_mute',
];

for (const command of rendererCommands) {
  assert.equal(allowedCommands.has(command), true, `missing native command policy entry: ${command}`);
  assert.equal(validateNativeCommand(command, []), true);
}

assert.throws(() => validateNativeCommand('definitely_not_a_command', []), /unsupported native host command/);
assert.throws(() => validateNativeCommand('mode', Array.from({ length: 53 }, () => 0)), /invalid command arguments/);
assert.throws(() => validateNativeCommand('mode', [{ nope: true }]), /invalid command argument type/);
assert.throws(() => validateNativeCommand('mode', ['x'.repeat(4097)]), /command argument is too long/);
assert.equal(validateNativeCommand('mode', ['signature']), true);
assert.equal(validateNativeCommand('mode', ['manual']), true);

console.log('PulseFX native command policy tests passed');
