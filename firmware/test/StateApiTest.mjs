import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
import { validState } from '../web/client.mjs';

test('C++ state endpoint produces JSON consumed by the real browser validator', async () => {
  const dir = await mkdtemp(join(tmpdir(), 'esp32-state-api-'));
  const firmware = fileURLToPath(new URL('../', import.meta.url));
  try {
    const binary = join(dir, 'state-api');
    execFileSync(process.env.CXX || 'c++', ['-std=c++11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
      '-fno-exceptions', '-fno-rtti', '-I', join(firmware, 'src'),
      ...['src/adapters/StateApi.cpp', 'src/domain/FanController.cpp', 'src/config/ConfigStore.cpp',
        'test/StateApiFixture.cpp'].map((path) => join(firmware, path)), '-o', binary]);
    function run(scenario) {
      const [status, type, cache, allow, body] = execFileSync(binary, [scenario], { encoding: 'utf8' }).split('\n');
      assert.equal(cache, 'no-store');
      assert.equal(type, 'application/json; charset=utf-8');
      return { status: Number(status), allow, body: JSON.parse(body) };
    }
    const initial = run('initial');
    assert.equal(initial.status, 200);
    assert.equal(validState(initial.body), true);
    assert.deepEqual(initial.body, { bootId: 'test-boot', revision: 0, mode: 'manual',
      fanOutputOn: false, temperatureC: null, sensorStatus: 'initializing', sampleAgeMs: null,
      onThresholdC: 30, offThresholdC: 28, fault: 'none', lastCommandSource: 'system', wifiConnected: false });
    const healthy = run('healthy').body;
    assert.equal(validState(healthy), true);
    assert.equal(healthy.temperatureC, 31.125);
    assert.equal(healthy.sampleAgeMs, 0);
    assert.equal(healthy.mode, 'auto');
    assert.equal(healthy.fanOutputOn, true);
    for (const [scenario, age, sensorStatus] of [['invalid', 100, 'invalid'], ['stale', 5001, 'stale']]) {
      const state = run(scenario).body;
      assert.equal(validState(state), true);
      assert.equal(state.temperatureC, null);
      assert.equal(state.sampleAgeMs, age);
      assert.equal(state.sensorStatus, sensorStatus);
      assert.equal(state.fault, 'temperature_unavailable');
      assert.equal(state.mode, 'manual');
      assert.equal(state.fanOutputOn, false);
    }
    assert.equal(run('escape').body.bootId, 'quote"slash\\newline\n\0\x01');
    assert.equal(run('unavailable-placeholder').body.temperatureC, null);
    assert.equal(run('max-safe').body.revision, Number.MAX_SAFE_INTEGER);
    for (const scenario of ['unsafe', 'nan', 'bad-enum', 'empty', 'long-id', 'bad-utf8', 'inconsistent', 'bad-config']) {
      const result = run(scenario);
      assert.equal(result.status, 503, scenario);
      assert.equal(result.body.error.code, 'STATE_UNAVAILABLE');
    }
    const write = run('write');
    assert.equal(write.status, 405);
    assert.equal(write.allow, 'GET');
    const missing = run('missing');
    assert.equal(missing.status, 404);
    assert.ok(!JSON.stringify(missing.body).includes('private-password'));
    assert.equal(run('long-path').status, 414);
    assert.equal(run('long-method').status, 400);
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
});
