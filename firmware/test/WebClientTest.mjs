import test from 'node:test';
import assert from 'node:assert/strict';
import { DeviceClient } from '../web/client.mjs';

const state = (changes = {}) => ({ bootId: 'boot-a', revision: 0, mode: 'manual',
  fanOutputOn: false, temperatureC: null, sensorStatus: 'initializing', sampleAgeMs: null,
  onThresholdC: 30, offThresholdC: 28, fault: 'none', lastCommandSource: 'system',
  wifiConnected: true, ...changes });
const response = (body, status = 200) => ({ ok: status === 200, status, json: async () => body });
function pending() {
  let resolve;
  const promise = new Promise((r) => { resolve = r; });
  return { promise, resolve };
}
function harness(options = {}) {
  const calls = [];
  const client = new DeviceClient({ ...options, fetcher: (path, init) => {
    const p = pending();
    calls.push({ path, init, ...p });
    return p.promise;
  } });
  return { client, calls };
}
async function settle() { for (let i = 0; i < 20; i++) await Promise.resolve(); }
async function connect(h) {
  const p = h.client.refresh(); h.calls.at(-1).resolve(response(state())); await p;
}

test('one write at a time, no optimistic success, explicit payload and post-write read', async () => {
  const h = harness(); await connect(h);
  const p = h.client.write('power', true);
  assert.equal(h.client.state.fanOutputOn, false);
  assert.equal(await h.client.write('power', false), false);
  assert.equal(await h.client.refresh(), false);
  assert.equal(h.calls.length, 2);
  assert.equal(h.calls[1].init.body, '{"on":true}');
  assert.equal(h.calls[1].init.headers['Content-Type'], 'application/json');
  h.calls[1].resolve(response(state({ revision: 1, fanOutputOn: true })));
  await settle();
  assert.equal(h.calls[2].path, '/api/v1/state');
  assert.equal(h.client.busy, true);
  h.calls[2].resolve(response(state({ revision: 1, fanOutputOn: true })));
  assert.equal(await p, true);
  assert.equal(h.client.busy, false);
});

test('pre-write poll arriving after a new write cannot roll state back', async () => {
  const h = harness(); await connect(h);
  const old = h.client.refresh();
  const write = h.client.write('power', true);
  h.calls[2].resolve(response(state({ revision: 2, fanOutputOn: true })));
  await settle();
  h.calls[3].resolve(response(state({ revision: 2, fanOutputOn: true })));
  await write;
  h.calls[1].resolve(response(state())); await old;
  assert.equal(h.client.state.revision, 2);
  assert.equal(h.client.state.fanOutputOn, true);
  assert.equal(h.client.connected, true);
});

test('write crossing reboot is not replayed; old boot replies cannot replace new boot', async () => {
  const h = harness(); await connect(h);
  const old = h.client.refresh();
  const write = h.client.write('power', true);
  h.calls[2].resolve(response(state({ bootId: 'boot-b', fanOutputOn: true })));
  await settle();
  assert.equal(h.client.state.bootId, 'boot-a');
  h.calls[3].resolve(response(state({ bootId: 'boot-b' })));
  await write;
  h.calls[1].resolve(response(state({ revision: 100, fanOutputOn: true })));
  await old;
  assert.equal(h.client.state.bootId, 'boot-b');
  assert.equal(h.client.state.fanOutputOn, false);
  const stale = h.client.refresh();
  h.calls[4].resolve(response(state({ revision: 101 })));
  await stale;
  assert.equal(h.client.state.bootId, 'boot-b');
  assert.equal(h.client.connected, false);
  assert.equal(h.calls.filter((c) => c.init.method === 'PUT').length, 1);
});

test('lost write response reports unknown then queries; timeout never replays the command', async () => {
  const messages = [];
  const h = harness({ timeoutMs: 20, onChange: (v) => messages.push(v.message) });
  await connect(h);
  const write = h.client.write('power', true);
  await new Promise((resolve) => setTimeout(resolve, 25));
  assert.equal(h.calls.length, 3);
  assert.equal(h.calls[1].init.signal.aborted, true);
  assert.ok(messages.some((s) => s.includes('操作结果未知')));
  h.calls[2].resolve(response(state({ revision: 1, fanOutputOn: true })));
  await write;
  assert.equal(h.client.connected, true);
  assert.equal(h.client.state.fanOutputOn, true);
  // Even a transport ignoring abort cannot apply its late response.
  h.calls[1].resolve(response(state({ revision: 99, fanOutputOn: false })));
  await settle();
  assert.equal(h.client.state.revision, 1);
  assert.equal(h.calls.filter((c) => c.init.method === 'PUT').length, 1);
});

test('invalid state and network failure preserve historical data and disable writes', async () => {
  const h = harness(); await connect(h);
  const p = h.client.refresh();
  h.calls[1].resolve(response({ bootId: 'broken' })); await p;
  assert.equal(h.client.connected, false);
  assert.equal(h.client.state.bootId, 'boot-a');
  assert.equal(await h.client.write('power', true), false);
  const recovery = h.client.refresh();
  h.calls[2].resolve(response(state({ revision: 3 }))); await recovery;
  assert.equal(h.client.connected, true);
  const older = h.client.refresh(); h.calls[3].resolve(response(state({ revision: 2 })));
  await older;
  assert.equal(h.client.state.revision, 3);
  assert.equal(h.client.connected, false);
});

test('unavailable auto mode returns a visible rejection and retains confirmed output', async () => {
  const h = harness(); await connect(h);
  const write = h.client.write('mode', 'auto');
  h.calls[1].resolve(response({ error: { code: 'TEMPERATURE_UNAVAILABLE' } }, 409));
  await settle(); h.calls[2].resolve(response(state())); await write;
  assert.match(h.client.message, /温度不可用/);
  assert.equal(h.client.state.mode, 'manual');
  assert.equal(h.client.state.fanOutputOn, false);
});

test('equal revision updates sample age; unsafe revision numbers are rejected', async () => {
  const h = harness(); await connect(h);
  const p = h.client.refresh();
  h.calls[1].resolve(response(state({ sampleAgeMs: 1000 }))); await p;
  assert.equal(h.client.state.sampleAgeMs, 1000);
  const bad = h.client.refresh();
  h.calls[2].resolve(response(state({ revision: Number.MAX_SAFE_INTEGER + 1 }))); await bad;
  assert.equal(h.client.state.revision, 0);
  assert.equal(h.client.connected, false);
});
