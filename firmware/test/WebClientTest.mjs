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

test('inconsistent device snapshots cannot replace the last confirmed state', async () => {
  const h = harness(); await connect(h);
  const invalid = [
    { onThresholdC: 29, offThresholdC: 28.5 },
    { onThresholdC: 61 },
    { sensorStatus: 'ok', temperatureC: null, sampleAgeMs: 0 },
    { sensorStatus: 'ok', temperatureC: 30, sampleAgeMs: null },
    { sensorStatus: 'ok', temperatureC: 30, sampleAgeMs: 5001 },
    { sensorStatus: 'invalid', temperatureC: 30, sampleAgeMs: 0 },
    { mode: 'auto', sensorStatus: 'invalid' },
    { sensorStatus: 'ok', temperatureC: 30, sampleAgeMs: 0,
      fault: 'temperature_unavailable' },
    { bootId: '设备启动' },
    { bootId: 'x'.repeat(129) },
  ];
  for (const change of invalid) {
    const refresh = h.client.refresh();
    h.calls.at(-1).resolve(response(state({ revision: 1, ...change })));
    assert.equal(await refresh, false);
    assert.equal(h.client.connected, false);
    assert.equal(h.client.state.revision, 0);
    assert.equal(h.client.state.onThresholdC, 30);
    assert.equal(await h.client.write('power', true), false);
  }
  const recovery = h.client.refresh();
  h.calls.at(-1).resolve(response(state({ revision: 2, sensorStatus: 'ok',
    temperatureC: 30, sampleAgeMs: 5000 })));
  assert.equal(await recovery, true);
  assert.equal(h.client.connected, true);
  assert.equal(h.client.state.revision, 2);
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

test('configuration submits both fields through the same write lock', async () => {
  const h = harness(); await connect(h);
  const value = { onThresholdC: 35.5, offThresholdC: 32 };
  const write = h.client.write('config', value);
  assert.equal(await h.client.write('power', true), false);
  assert.equal(h.calls[1].path, '/api/v1/config');
  assert.deepEqual(JSON.parse(h.calls[1].init.body), value);
  assert.equal(h.client.state.onThresholdC, 30);
  h.calls[1].resolve(response(state({ ...value, revision: 1 })));
  await settle(); h.calls[2].resolve(response(state({ ...value, revision: 1 })));
  assert.equal(await write, true);
  assert.equal(h.client.state.offThresholdC, 32);
});

test('invalid configuration is rejected locally without any request', async () => {
  const h = harness(); await connect(h);
  for (const value of [null, {}, { onThresholdC: 30 },
    { onThresholdC: 30, offThresholdC: 29.1 }, { onThresholdC: 61, offThresholdC: 28 },
    { onThresholdC: 30, offThresholdC: -1 }, { onThresholdC: '30', offThresholdC: 28 },
    { onThresholdC: NaN, offThresholdC: 28 }, { onThresholdC: 30, offThresholdC: 28, extra: 1 }]) {
    assert.equal(await h.client.write('config', value), false);
  }
  assert.equal(h.calls.length, 1);
});

test('storage failure never reports acceptance or changes confirmed configuration', async () => {
  const h = harness(); await connect(h);
  const write = h.client.write('config', { onThresholdC: 35, offThresholdC: 32 });
  h.calls[1].resolve(response({ error: { code: 'STORAGE_FAILURE' } }, 503));
  await settle(); h.calls[2].resolve(response(state()));
  assert.equal(await write, false);
  assert.equal(h.client.state.onThresholdC, 30);
  assert.equal(h.client.state.offThresholdC, 28);
  assert.match(h.client.message, /保存失败/);
});

test('a reboot observed in a write response retires the old boot before resynchronizing', async () => {
  const h = harness(); await connect(h);
  const write = h.client.write('power', true);
  h.calls[1].resolve(response(state({ bootId: 'boot-b', revision: 1, fanOutputOn: true })));
  await settle();
  // A stale proxy/transport reply must not restore connectivity to boot-a,
  // even though boot-b's state is not accepted as confirmation of this command.
  h.calls[2].resolve(response(state({ revision: 99, fanOutputOn: true })));
  assert.equal(await write, false);
  assert.equal(h.client.connected, false);
  assert.equal(h.client.state.revision, 0);
  assert.equal(await h.client.write('power', false), false);
  const recovery = h.client.refresh();
  h.calls[3].resolve(response(state({ bootId: 'boot-b' })));
  assert.equal(await recovery, true);
  assert.equal(h.client.state.bootId, 'boot-b');
  assert.equal(h.client.state.fanOutputOn, false);
  assert.equal(h.calls.filter((c) => c.init.method === 'PUT').length, 1);
});

test('a retired boot in a write response does not retire the current boot', async () => {
  const h = harness(); await connect(h);
  const reboot = h.client.refresh();
  h.calls[1].resolve(response(state({ bootId: 'boot-b' })));
  await reboot;
  const write = h.client.write('power', true);
  h.calls[2].resolve(response(state({ revision: 100, fanOutputOn: true })));
  await settle();
  h.calls[3].resolve(response(state({ bootId: 'boot-b', revision: 1 })));
  assert.equal(await write, false);
  assert.equal(h.client.connected, true);
  assert.equal(h.client.state.bootId, 'boot-b');
  assert.equal(h.client.state.fanOutputOn, false);
  assert.match(h.client.message, /旧响应/);
});

test('equal revision cannot change control fields but may refresh sample age', async () => {
  const h = harness(); await connect(h);
  for (const change of [{ fanOutputOn: true }, { onThresholdC: 35 },
    { lastCommandSource: 'web' }, { wifiConnected: false },
    { sensorStatus: 'invalid' }]) {
    const read = h.client.refresh();
    h.calls.at(-1).resolve(response(state(change)));
    assert.equal(await read, false, JSON.stringify(change));
    assert.equal(h.client.connected, false);
    assert.deepEqual(h.client.state, state());
    assert.equal(await h.client.write('power', true), false);
  }
  const recovery = h.client.refresh();
  const fresh = state({ revision: 1, sensorStatus: 'ok', temperatureC: 31, sampleAgeMs: 100 });
  h.calls.at(-1).resolve(response(fresh));
  assert.equal(await recovery, true);
  const ageOnly = h.client.refresh();
  h.calls.at(-1).resolve(response({ ...fresh, sampleAgeMs: 0 }));
  assert.equal(await ageOnly, true, 'a new identical temperature sample can reset age without changing revision');
  const conflictingWrite = h.client.write('power', true);
  h.calls.at(-1).resolve(response({ ...fresh, fanOutputOn: true }));
  await settle();
  h.calls.at(-1).resolve(response({ ...fresh, sampleAgeMs: 200 }));
  assert.equal(await conflictingWrite, false);
  assert.equal(h.client.state.fanOutputOn, false);
  assert.equal(h.client.connected, true);
  assert.equal(h.calls.filter((c) => c.init.method === 'PUT').length, 1);
});

test('a stalled response body times out and its late completion cannot replace recovered state', async () => {
  const h = harness({ timeoutMs: 20 }); await connect(h);
  const body = pending();
  const read = h.client.refresh();
  h.calls[1].resolve({ ok: true, status: 200, json: () => body.promise });
  assert.equal(await read, false);
  assert.equal(h.calls[1].init.signal.aborted, true);
  assert.equal(h.client.connected, false);
  assert.equal(h.client.reading, false);
  assert.equal(await h.client.write('power', true), false);
  const recovery = h.client.refresh();
  h.calls[2].resolve(response(state({ revision: 2, fanOutputOn: true })));
  assert.equal(await recovery, true);
  body.resolve(state({ revision: 99, fanOutputOn: false }));
  await settle();
  assert.equal(h.client.state.revision, 2);
  assert.equal(h.client.state.fanOutputOn, true);
  assert.equal(h.client.connected, true);
});

test('a superseded poll failure cannot disconnect a successfully synchronized write', async () => {
  const h = harness(); await connect(h);
  const oldRead = h.client.refresh();
  const write = h.client.write('power', true);
  h.calls[2].resolve(response(state({ revision: 1, fanOutputOn: true })));
  await settle();
  h.calls[3].resolve(response(state({ revision: 1, fanOutputOn: true })));
  assert.equal(await write, true);
  const syncedAt = h.client.lastSync;
  h.calls[1].resolve({ ok: true, status: 200, json: async () => { throw new Error('old connection closed'); } });
  assert.equal(await oldRead, false);
  assert.equal(h.client.connected, true);
  assert.equal(h.client.state.fanOutputOn, true);
  assert.equal(h.client.lastSync, syncedAt);
  assert.equal(h.client.message, '状态已同步');
});

test('failed write and failed recovery release the lock without replaying the unknown command', async () => {
  const h = harness(); await connect(h);
  const broken = { ok: true, status: 200, json: async () => { throw new Error('truncated JSON'); } };
  const write = h.client.write('power', true);
  h.calls[1].resolve(broken);
  await settle();
  h.calls[2].resolve(broken);
  assert.equal(await write, false);
  assert.equal(h.client.busy, false);
  assert.equal(h.client.reading, false);
  assert.equal(h.client.connected, false);
  assert.deepEqual(h.client.state, state());
  assert.match(h.client.message, /操作结果未知/);
  assert.equal(await h.client.write('power', false), false);
  const recovery = h.client.refresh();
  h.calls[3].resolve(response(state({ revision: 1, fanOutputOn: true })));
  assert.equal(await recovery, true);
  assert.equal(h.client.state.fanOutputOn, true);
  assert.equal(h.calls.filter((c) => c.init.method === 'PUT').length, 1);
});
