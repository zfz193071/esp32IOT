import { readdir, stat, mkdir, mkdtemp, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { spawn } from 'node:child_process';
import { firmware, toolchain } from './arduino-environment.mjs';

const usage = 'Usage: node firmware/tools/inspect-board.mjs --list | --port /dev/DEVICE [--dry-run]';
// Version required by the pinned Arduino-ESP32 3.3.3 platform.
const version = '5.1.0';
const executable = join(toolchain, `data/packages/esp32/tools/esptool_py/${version}/esptool`);
const portName = /^(cu\.[A-Za-z0-9._-]+|ttyUSB\d+|ttyACM\d+)$/;

async function main() {
  const args = process.argv.slice(2);
  if (args.length === 1 && args[0] === '--help') { console.log(usage); return; }
  if (args.length === 1 && args[0] === '--list') {
    const ports = (await readdir('/dev')).filter((name) => portName.test(name)).sort();
    console.log(JSON.stringify({ ports: ports.map((name) => `/dev/${name}`),
      note: 'Names only; no port opened and no ESP32 identity inferred.' }, null, 2));
    return;
  }
  const dryRun = args.length === 3 && args[2] === '--dry-run';
  if ((args.length !== 2 && !dryRun) || args[0] !== '--port' ||
      !args[1].startsWith('/dev/') || !portName.test(args[1].slice(5))) throw new Error(usage);
  // No arbitrary esptool subcommands/options, auto-selected ports or RAM stub.
  const command = ['--chip', 'auto', '--port', args[1], '--baud', '115200',
    '--before', 'no-reset', '--after', 'no-reset', '--no-stub', '--connect-attempts', '3', 'flash-id'];
  if (dryRun) {
    console.log(JSON.stringify({ executable, args: command, opensPort: false,
      prerequisite: 'Motor/load disconnected; manually enter ROM download mode first.' }, null, 2));
    return;
  }
  if (!(await stat(args[1])).isCharacterDevice()) throw new Error('Port must be a serial character device');
  if (!(await stat(executable)).isFile()) throw new Error('Run setup-arduino.mjs first');
  const parent = join(firmware, 'build/board-inspection');
  await mkdir(parent, { recursive: true });
  const output = await mkdtemp(join(parent, 'read-'));
  const config = join(output, 'esptool.cfg');
  await writeFile(config, '[esptool]\nopen_port_attempts = 1\n');
  const env = Object.fromEntries(Object.entries(process.env).filter(([key]) => !key.startsWith('ESPTOOL_')));
  env.ESPTOOL_CFGFILE = config;
  console.log('Reading ROM chip/Flash identity; no Flash write/erase. Device must already be in download mode.');
  const result = await new Promise((resolve, reject) => {
    const child = spawn(executable, command, { env, cwd: output, timeout: 45000, stdio: ['ignore', 'pipe', 'pipe'] });
    let stdout = '', stderr = '';
    child.stdout.on('data', (chunk) => { stdout += chunk; process.stdout.write(chunk); });
    child.stderr.on('data', (chunk) => { stderr += chunk; process.stderr.write(chunk); });
    child.on('error', reject);
    child.on('close', (code, signal) => resolve({ code, signal, stdout, stderr }));
  });
  await writeFile(join(output, 'result.json'), JSON.stringify({
    port: args[1], executable, expectedToolVersion: version, args: command,
    capturedAt: new Date().toISOString(), ...result,
  }, null, 2) + '\n');
  console.log(`Raw identification output: ${output}/result.json`);
  if (result.code !== 0) throw new Error('Identification failed; no board/Flash configuration accepted');
  console.log('Read completed. Review chip and detected Flash size; no board profile was changed.');
}
main().catch((error) => { console.error(error.message); process.exitCode = 1; });
