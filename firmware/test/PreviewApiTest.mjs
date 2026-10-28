import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { validState } from '../web/client.mjs';

async function withServer(port, fail, check) {
  const child = spawn(process.execPath, [new URL('../tools/preview-server.mjs', import.meta.url).pathname], {
    env: { ...process.env, ESP32_PREVIEW_PORT: String(port), ESP32_SIMULATE_STORAGE_FAILURE: fail ? '1' : '0' },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let errors = '';
  child.stderr.on('data', (data) => { errors += data; });
  try {
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('preview startup timed out')), 5000);
      child.stdout.once('data', () => { clearTimeout(timer); resolve(); });
      child.once('error', (error) => { clearTimeout(timer); reject(error); });
      child.once('exit', () => { clearTimeout(timer); reject(new Error(errors || 'preview exited')); });
    });
    const base = `http://127.0.0.1:${port}`;
    const put = (body, headers = {}) => fetch(`${base}/api/v1/config`, {
      method: 'PUT', headers: { 'Content-Type': 'application/json', ...headers }, body: JSON.stringify(body),
    });
    await check(base, put);
  } finally {
    if (child.exitCode === null && child.signalCode === null) {
      const ended = once(child, 'exit'); child.kill(); await ended;
    }
  }
}

test('preview config API validates complete atomic payload and same-origin writes', async () => {
  await withServer(18313, false, async (base, put) => {
    const next = { onThresholdC: 35.5, offThresholdC: 32 };
    assert.equal((await put(next)).status, 200);
    const saved = await (await fetch(`${base}/api/v1/state`)).json();
    assert.equal(saved.onThresholdC, 35.5);
    assert.equal(saved.offThresholdC, 32);
    const identical = await (await put(next)).json();
    assert.equal(identical.revision, saved.revision);
    for (const invalid of [{ onThresholdC: 40 }, { ...next, extra: true },
      { onThresholdC: '35', offThresholdC: 32 }, { onThresholdC: 30, offThresholdC: 29.5 },
      { onThresholdC: 61, offThresholdC: 32 }]) {
      assert.equal((await put(invalid)).status, 400);
    }
    assert.equal((await put(next, { Origin: 'https://foreign.example' })).status, 403);
    assert.equal((await put(next, { 'Content-Type': 'text/plain' })).status, 400);
    assert.deepEqual(await (await fetch(`${base}/api/v1/state`)).json(), saved);
    const module = await fetch(`${base}/config.mjs`);
    assert.equal(module.status, 200);
    assert.match(module.headers.get('content-type'), /javascript/);
  });
});

test('preview storage fault returns 503 and retains both original thresholds', async () => {
  await withServer(18314, true, async (base, put) => {
    const before = await (await fetch(`${base}/api/v1/state`)).json();
    assert.equal((await put({ onThresholdC: 30, offThresholdC: 28 })).status, 200);
    const response = await put({ onThresholdC: 35, offThresholdC: 32 });
    assert.equal(response.status, 503);
    assert.equal((await response.json()).error.code, 'STORAGE_FAILURE');
    assert.deepEqual(await (await fetch(`${base}/api/v1/state`)).json(), before);
  });
});

test('preview temperature injection exercises automatic hysteresis and fault recovery', async () => {
  await withServer(18315, false, async (base) => {
    const put = (path, body, headers = {}) => fetch(`${base}${path}`, {
      method: 'PUT', headers: { 'Content-Type': 'application/json', ...headers },
      body: JSON.stringify(body),
    });
    const inject = async (temperatureC) => {
      const response = await put('/__simulator/temperature', { temperatureC });
      assert.equal(response.status, 200);
      return response.json();
    };
    const mode = async (value) => put('/api/v1/mode', { mode: value });

    assert.equal((await mode('auto')).status, 409);
    let state = await inject(29);
    assert.equal(state.sensorStatus, 'ok');
    assert.equal(state.sampleAgeMs, 0);
    assert.equal(state.mode, 'manual');
    assert.equal((await put('/api/v1/power', { on: true })).status, 200);
    state = await (await mode('auto')).json();
    assert.equal(state.mode, 'auto');
    assert.equal(state.fanOutputOn, false);

    state = await inject(30);
    assert.equal(state.fanOutputOn, true);
    assert.equal(state.lastCommandSource, 'system');
    state = await inject(29);
    assert.equal(state.fanOutputOn, true);
    state = await (await mode('auto')).json();
    assert.equal(state.fanOutputOn, true, 'repeating auto must retain deadband output');
    state = await inject(28);
    assert.equal(state.fanOutputOn, false);

    state = await inject(34);
    assert.equal(state.fanOutputOn, true);
    state = await (await put('/api/v1/config', { onThresholdC: 35, offThresholdC: 32 })).json();
    assert.equal(state.mode, 'auto');
    assert.equal(state.fanOutputOn, true, 'new thresholds retain output inside deadband');
    state = await inject(32);
    assert.equal(state.fanOutputOn, false);

    state = await inject(null);
    assert.equal(state.sensorStatus, 'invalid');
    assert.equal(state.mode, 'manual');
    assert.equal(state.fanOutputOn, false);
    assert.equal(state.fault, 'temperature_unavailable');
    assert.equal((await mode('auto')).status, 409);
    state = await (await put('/api/v1/power', { on: true })).json();
    assert.equal(state.fanOutputOn, true);
    assert.equal(state.fault, 'temperature_unavailable');
    state = await inject(31);
    assert.equal(state.mode, 'manual');
    assert.equal(state.fanOutputOn, true);
    assert.equal(state.fault, 'none');
    state = await (await mode('auto')).json();
    assert.equal(state.fanOutputOn, false, 'manual output must not seed automatic deadband');

    const beforeInvalid = state;
    for (const invalid of [{}, { temperatureC: '30' },
      { temperatureC: 30, extra: true }, { temperatureC: [] }]) {
      assert.equal((await put('/__simulator/temperature', invalid)).status, 400);
    }
    assert.equal((await put('/__simulator/temperature', { temperatureC: 30 },
      { Origin: 'https://foreign.example' })).status, 403);
    assert.equal((await fetch(`${base}/__simulator/temperature`)).status, 404);
    const afterInvalid = await (await fetch(`${base}/api/v1/state`)).json();
    assert.deepEqual({ ...afterInvalid, sampleAgeMs: beforeInvalid.sampleAgeMs }, beforeInvalid);
    assert.ok(afterInvalid.sampleAgeMs >= beforeInvalid.sampleAgeMs);

    state = await inject(40);
    assert.equal(state.fanOutputOn, true);
    const beforeStaleRevision = state.revision;
    await new Promise((resolve) => setTimeout(resolve, 5150));
    state = await (await fetch(`${base}/api/v1/state`)).json();
    assert.equal(state.sensorStatus, 'stale');
    assert.equal(state.temperatureC, null);
    assert.ok(state.sampleAgeMs > 5000);
    assert.equal(state.mode, 'manual');
    assert.equal(state.fanOutputOn, false);
    assert.equal(state.fault, 'temperature_unavailable');
    assert.equal(state.revision, beforeStaleRevision + 1);
    assert.equal(validState(state), true);
    assert.equal((await mode('auto')).status, 409);
    const later = await (await fetch(`${base}/api/v1/state`)).json();
    assert.equal(later.revision, state.revision, 'sample age alone must not change revision');
    state = await inject(31);
    assert.equal(state.sensorStatus, 'ok');
    assert.equal(state.mode, 'manual');
    assert.equal(state.fault, 'none');
    assert.equal(validState(state), true);
  });
});
