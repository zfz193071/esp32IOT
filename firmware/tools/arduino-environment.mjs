import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { join } from 'node:path';
import { spawn } from 'node:child_process';
import { createReadStream } from 'node:fs';
import { createHash } from 'node:crypto';

export const firmware = fileURLToPath(new URL('../', import.meta.url));
export const toolchain = join(firmware, 'build/toolchain');
export const cli = join(toolchain, 'bin/arduino-cli');
export const configPath = join(toolchain, 'arduino-cli.yaml');
export const lock = JSON.parse(await readFile(join(firmware, 'config/arduino-build.json'), 'utf8'));

export async function configure() {
  for (const name of ['bin', 'data', 'downloads', 'user', 'cache']) await mkdir(join(toolchain, name), { recursive: true });
  // JSON is valid YAML. Absolute paths prevent accidental use of global Arduino15.
  await writeFile(configPath, JSON.stringify({
    directories: {
      data: join(toolchain, 'data'), downloads: join(toolchain, 'downloads'), user: join(toolchain, 'user'),
    },
    board_manager: { additional_urls: [lock.indexUrl] },
    build_cache: { path: join(toolchain, 'cache') },
  }, null, 2) + '\n');
}

export function run(command, args, capture = false, env = process.env) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { env, stdio: capture ? ['ignore', 'pipe', 'inherit'] : 'inherit' });
    let output = '';
    if (capture) child.stdout.setEncoding('utf8').on('data', (chunk) => { output += chunk; });
    child.on('error', reject);
    child.on('close', (code) => code === 0 ? resolve(output) : reject(new Error(`${command} exited ${code}`)));
  });
}

// Arduino environment settings override YAML; keep this child isolated even if
// the user's shell configures a different global IDE installation.
const arduinoEnv = Object.fromEntries(Object.entries(process.env).filter(([key]) => !key.startsWith('ARDUINO_')));
export const arduino = (args, capture = false) => run(cli, ['--config-file', configPath, ...args], capture, arduinoEnv);
export async function sha256(path) {
  const hash = createHash('sha256');
  for await (const chunk of createReadStream(path)) hash.update(chunk);
  return hash.digest('hex');
}
export async function verifyCli() {
  const version = JSON.parse(await arduino(['version', '--format', 'json'], true));
  if (version.VersionString !== lock.cliVersion) throw new Error(`Expected Arduino CLI ${lock.cliVersion}`);
  return version;
}
