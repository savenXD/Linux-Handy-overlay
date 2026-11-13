// Exercises the actual inline gate script without navigating or downloading a file.
// Supply CRONQ_DOWNLOAD_CODE locally to verify the real code; never store it in source/CI logs.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { webcrypto, randomUUID } from 'node:crypto';
import vm from 'node:vm';

const html = await readFile(new URL('./index.html', import.meta.url), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)?.[1];
assert.ok(script, 'inline download gate script exists');
const elements = new Map();
function element(id) {
  if (!elements.has(id)) elements.set(id, {
    value: '', textContent: '', open: false, offsetWidth: 1, listeners: {},
    classList: { add() {}, remove() {} },
    addEventListener(event, listener) { this.listeners[event] = listener; },
    showModal() { this.open = true; }, close() { this.open = false; },
    focus() {}, select() {},
  });
  return elements.get(id);
}
const location = { href: 'https://download-test.invalid/' };
const context = vm.createContext({
  document: { getElementById: element }, location, crypto: webcrypto,
  TextEncoder, TextDecoder, Uint8Array,
  atob: value => Buffer.from(value, 'base64').toString('binary'),
});
new vm.Script(script).runInContext(context);
element('code').listeners.dblclick();
assert.equal(element('gate').open, true, 'double-click opens the code dialog');
element('gate-input').value = `invalid-${randomUUID()}`;
await element('gate-form').listeners.submit({ preventDefault() {} });
assert.equal(element('gate-msg').textContent, 'Incorrect code');
assert.equal(location.href, 'https://download-test.invalid/');
assert.equal(element('gate').open, true);

if (process.env.CRONQ_DOWNLOAD_CODE) {
  element('gate-input').value = process.env.CRONQ_DOWNLOAD_CODE;
  await element('gate-form').listeners.submit({ preventDefault() {} });
  assert.equal(element('gate').open, false, 'correct code closes the dialog');
  assert.equal(location.href,
    'https://github.com/savenXD/Linux-Handy-overlay/releases/latest/download/cronq-setup.exe',
    'download follows the latest installer, never a pinned old release');
  console.log('PASS download gate: trigger, wrong code, correct code and latest installer URL');
} else {
  console.log('PASS download gate: trigger and wrong code; set CRONQ_DOWNLOAD_CODE for the correct-code check');
}
