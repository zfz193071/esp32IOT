import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';

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
