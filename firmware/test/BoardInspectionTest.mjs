import test from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { readFile, rm, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { captureInspectionProcess, inspectionSucceeded } from '../tools/inspection-process.mjs';
import { compareSerialPorts } from '../tools/inspection-ports.mjs';

const firmware = fileURLToPath(new URL('../', import.meta.url));
const cli = join(firmware, 'tools/inspect-board.mjs');
const runNode = (source, options = {}) => captureInspectionProcess(process.execPath, ['-e', source], options);

test('capture retains both streams and distinguishes successful and failed processes', async () => {
  const ok = await runNode('console.log("chip-fixture"); console.error("warning-fixture")');
  assert.equal(inspectionSucceeded(ok), true);
  assert.equal(ok.stdout, 'chip-fixture\n');
  assert.equal(ok.stderr, 'warning-fixture\n');
  assert.equal(ok.spawnError, null);
  const failed = await runNode('console.log("partial-fixture"); process.exitCode=2');
  assert.equal(failed.code, 2);
  assert.equal(failed.stdout, 'partial-fixture\n');
  assert.equal(inspectionSucceeded(failed), false);
});

test('missing executable returns a structured error instead of losing the report', async () => {
  const result = await captureInspectionProcess(join(firmware, 'build', `missing-tool-${process.pid}`), []);
  assert.equal(result.spawnError.code, 'ENOENT');
  assert.equal(result.stdout, '');
  assert.equal(result.timedOut, false);
  assert.equal(inspectionSucceeded(result), false);
});

test('each output stream has a byte cap and truncated success is not accepted', async () => {
  const result = await runNode('process.stdout.write(Buffer.alloc(262144,65)); process.stderr.write(Buffer.alloc(262144,66));',
    { maxOutputBytes: 1024 });
  assert.equal(result.code, 0);
  assert.equal(result.stdout, 'A'.repeat(1024));
  assert.equal(result.stderr, 'B'.repeat(1024));
  assert.equal(result.stdoutTruncated, true);
  assert.equal(result.stderrTruncated, true);
  assert.equal(inspectionSucceeded(result), false);
});

test('timeout remains failure even if a cooperative child exits with zero', { timeout: 10000 }, async () => {
  const result = await runNode('process.on("SIGTERM",()=>process.exit(0)); console.log("ready"); setInterval(()=>{},1000);',
    { timeoutMs: 1500, killGraceMs: 500 });
  assert.equal(result.stdout, 'ready\n');
  assert.equal(result.code, 0);
  assert.equal(result.timedOut, true);
  assert.equal(result.forcedKill, false);
  assert.equal(inspectionSucceeded(result), false);
});

test('timeout kills the owned group when launcher and child ignore termination', { timeout: 10000 }, async () => {
  const source = `
    const {spawn}=require('node:child_process');
    process.on('SIGTERM',()=>{});
    spawn(process.execPath,['-e', 'process.on("SIGTERM",()=>{}); console.log("child-ready"); setInterval(()=>{},1000);'],
      {stdio:['ignore','inherit','inherit']});
    console.log('parent-ready');
    setInterval(()=>{},1000);
  `;
  const result = await runNode(source, { timeoutMs: 1500, killGraceMs: 100 });
  assert.match(result.stdout, /parent-ready/);
  assert.match(result.stdout, /child-ready/);
  assert.equal(result.timedOut, true);
  assert.equal(result.forcedKill, true);
  assert.equal(result.signal, 'SIGKILL');
  assert.ok(result.durationMs < 8000, 'inherited pipes close after group cleanup');
  assert.equal(inspectionSucceeded(result), false);
});

test('CLI rejects command injection and dry-run never needs a real port', () => {
  for (const args of [[], ['erase-flash'], ['--snapshot','extra'], ['--compare'],
    ['--port','@file'], ['--port','/tmp/device'],
    ['--port','/dev/cu.fake','--after','hard-reset'], ['--list','write-flash']]) {
    const result = spawnSync(process.execPath, [cli, ...args], { encoding: 'utf8' });
    assert.equal(result.status, 1);
    assert.match(result.stderr, /Usage:/);
    assert.doesNotMatch(result.stdout, /Raw identification output:/);
  }
  const result = spawnSync(process.execPath, [cli, '--port','/dev/cu.no-device','--dry-run'], { encoding: 'utf8' });
  assert.equal(result.status, 0);
  const plan = JSON.parse(result.stdout);
  assert.equal(plan.opensPort, false);
  assert.equal(plan.args.at(-1), 'flash-id');
  assert.ok(plan.args.includes('--no-stub'));
  assert.equal(plan.args[plan.args.indexOf('--before')+1], 'no-reset');
  assert.equal(plan.args[plan.args.indexOf('--after')+1], 'no-reset');
});

test('serial port comparison reports only changes, without guessing a board', () => {
  const snapshot = { kind: 'serial-port-snapshot', schemaVersion: 1,
    capturedAt: '2026-11-01T00:00:00.000Z',
    ports: ['/dev/cu.Bluetooth-Incoming-Port', '/dev/cu.old-board'] };
  assert.deepEqual(compareSerialPorts(snapshot,
    ['/dev/cu.Bluetooth-Incoming-Port', '/dev/cu.new-board', '/dev/cu.second-board']), {
    addedPorts: ['/dev/cu.new-board', '/dev/cu.second-board'],
    removedPorts: ['/dev/cu.old-board'],
  });
  for (const bad of [null, {}, { ...snapshot, schemaVersion: 2 },
    { ...snapshot, ports: ['/tmp/not-a-port'] },
    { ...snapshot, ports: ['/dev/cu.duplicate', '/dev/cu.duplicate'] }])
    assert.throws(() => compareSerialPorts(bad, []), /Invalid serial port snapshot/);
});

test('CLI saves and compares a port snapshot without opening a device', async () => {
  const saved = spawnSync(process.execPath, [cli, '--snapshot'], { encoding: 'utf8' });
  assert.equal(saved.status, 0);
  const output = JSON.parse(saved.stdout);
  assert.ok(output.path.startsWith(join(firmware, 'build/board-inspection/ports-')));
  try {
    const snapshot = JSON.parse(await readFile(output.path, 'utf8'));
    assert.equal(snapshot.kind, 'serial-port-snapshot');
    assert.equal(snapshot.schemaVersion, 1);
    assert.deepEqual(snapshot.ports, output.ports);
    const compared = spawnSync(process.execPath, [cli, '--compare', output.path], { encoding: 'utf8' });
    assert.equal(compared.status, 0);
    const change = JSON.parse(compared.stdout);
    assert.equal(change.snapshot, output.path);
    assert.ok(Array.isArray(change.addedPorts) && Array.isArray(change.removedPorts));
    assert.match(change.note, /No port opened/);
    const invalid = join(dirname(output.path), 'invalid.json');
    await writeFile(invalid, JSON.stringify({ ...snapshot, ports: ['/tmp/not-a-port'] }));
    const rejected = spawnSync(process.execPath, [cli, '--compare', invalid], { encoding: 'utf8' });
    assert.equal(rejected.status, 1);
    assert.match(rejected.stderr, /Invalid serial port snapshot/);
  } finally { await rm(dirname(output.path), { recursive: true, force: true }); }
});

test('already cancelled inspection does not launch a process', async () => {
  const abort = new AbortController();
  abort.abort('SIGINT');
  const result = await captureInspectionProcess('/nonexistent-inspection-tool', [], { abortSignal: abort.signal });
  assert.equal(result.cancelled, true);
  assert.equal(result.cancelReason, 'SIGINT');
  assert.equal(result.spawnError, null);
  assert.equal(result.code, null);
  assert.equal(result.durationMs, 0);
  assert.equal(inspectionSucceeded(result), false);
});

test('cancellation terminates a stubborn process before the normal deadline', { timeout: 10000 }, async () => {
  const abort = new AbortController();
  const cancel = setTimeout(() => abort.abort('SIGINT'), 1500);
  try {
    const result = await runNode('process.on("SIGTERM",()=>{}); console.log("ready"); setInterval(()=>{},1000);',
      { abortSignal: abort.signal, timeoutMs: 8000, killGraceMs: 100 });
    assert.equal(result.stdout, 'ready\n');
    assert.equal(result.cancelled, true);
    assert.equal(result.cancelReason, 'SIGINT');
    assert.equal(result.timedOut, false);
    assert.equal(result.forcedKill, true);
    assert.equal(result.signal, 'SIGKILL');
    assert.equal(inspectionSucceeded(result), false);
  } finally { clearTimeout(cancel); }
});

test('missing-port preflight still saves failure evidence without opening hardware', async () => {
  const port = `/dev/cu.codex-missing-${process.pid}-${Date.now()}`;
  const result = spawnSync(process.execPath, [cli, '--port', port], { encoding: 'utf8' });
  assert.equal(result.status, 1);
  const path = result.stdout.match(/Raw identification output: (.+\/result\.json)/)?.[1];
  assert.ok(path?.startsWith(join(firmware, 'build/board-inspection/read-')));
  try {
    const report = JSON.parse(await readFile(path, 'utf8'));
    assert.equal(report.schemaVersion, 1);
    assert.equal(report.ok, false);
    assert.equal(report.port, port);
    assert.equal(report.preflightError.code, 'ENOENT');
    assert.equal(report.code, null);
    assert.equal(report.stdout, '');
    assert.equal(report.args.at(-1), 'flash-id');
  } finally { await rm(dirname(path), { recursive: true, force: true }); }
});
