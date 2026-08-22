'use strict';

const allowedCommands = new Set([
  'ping', 'status', 'devices', 'apps', 'quit', 'mode', 'output', 'enabled', 'night',
  'headphone_enable', 'headphone_profile', 'preamp', 'bass', 'clarity', 'fidelity', 'spatial',
  'surround', 'ambience', 'dynamics', 'pitch', 'eq', 'app_volume', 'app_mute',
]);

function validateNativeCommand(name, args = []) {
  if (typeof name !== 'string' || !allowedCommands.has(name)) {
    throw new Error('unsupported native host command');
  }
  if (!Array.isArray(args) || args.length > 52) throw new Error('invalid command arguments');
  for (const arg of args) {
    if (!['string', 'number', 'boolean'].includes(typeof arg)) throw new Error('invalid command argument type');
    if (String(arg).length > 4096) throw new Error('command argument is too long');
  }
  return true;
}

module.exports = {
  allowedCommands,
  validateNativeCommand,
};
