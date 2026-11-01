import { mkdir, mkdtemp, readdir, readFile, writeFile, copyFile, stat } from 'node:fs/promises';
import { join, relative } from 'node:path';
import { createHash } from 'node:crypto';
import { firmware, lock, configure, arduino, verifyCli, run, sha256 } from './arduino-environment.mjs';

async function main() {
  const args = process.argv.slice(2);
  const sketchName = args.length === 0 ? 'CompileCheck' :
    args.length === 2 && args[0] === '--sketch' && ['CompileCheck', 'SerialBringup'].includes(args[1]) ? args[1] : null;
  if (!sketchName) throw new Error('Usage: node firmware/tools/build-arduino.mjs [--sketch CompileCheck|SerialBringup]');
  const integration = sketchName === 'CompileCheck';
  await configure();
  const cliVersion = await verifyCli();
  const platforms = JSON.parse(await arduino(['core', 'list', '--format', 'json'], true));
  const core = platforms.platforms?.find((item) => item.id === lock.platform);
  if (core?.installed_version !== lock.coreVersion)
    throw new Error(`Expected ${lock.platform}@${lock.coreVersion}; run setup-arduino.mjs first`);

  const parent = join(firmware, 'build/arduino-check');
  await mkdir(parent, { recursive: true });
  const stage = await mkdtemp(join(parent, 'build-'));
  const library = join(stage, 'libraries/Esp32Fan');
  const sources = join(library, 'src');
  const sketch = join(stage, sketchName);
  await mkdir(sketch);
  const manifest = [];
  async function copySources(directory, destination) {
    await mkdir(destination, { recursive: true });
    const entries = (await readdir(directory, { withFileTypes: true })).sort((a, b) => a.name.localeCompare(b.name));
    for (const entry of entries) {
      const source = join(directory, entry.name);
      const target = join(destination, entry.name);
      if (entry.isSymbolicLink()) throw new Error(`Source symlinks are not allowed: ${source}`);
      if (entry.isDirectory()) await copySources(source, target);
      else if (entry.isFile() && /\.(h|cpp)$/.test(entry.name)) {
        const bytes = await readFile(source);
        await writeFile(target, bytes);
        manifest.push({ source: relative(firmware, source), sha256: createHash('sha256').update(bytes).digest('hex') });
      }
    }
  }
  if (integration) {
    // Only production C++ sources: no test stubs, local credentials or build caches.
    await copySources(join(firmware, 'src'), sources);
    await run(process.execPath, [join(firmware, 'tools/embed-web.mjs'), sources]);
    await writeFile(join(library, 'library.properties'),
      'name=Esp32Fan\nversion=0.1.0\nauthor=ESP32 Fan Project\nmaintainer=ESP32 Fan Project\n' +
      'sentence=Compile-only project staging library.\nparagraph=Generated from repository production sources.\n' +
      'category=Device Control\nurl=https://github.com/zfz193071/esp32IOT\narchitectures=esp32\nincludes=Esp32Fan.h\n');
    await writeFile(join(sources, 'Esp32Fan.h'), '#pragma once\n' + [
      'app/AppLoop.h', 'adapters/TextDiagnostics.h', 'adapters/NetworkHttpApi.h',
      'board/ArduinoNetworkPort.h', 'board/NvsConfigPersistence.h', 'board/Esp32SocketCloser.h',
      'board/Esp32SerialSink.h',
      'board/Esp32BootId.h',
    ].map((header) => `#include "${header}"\n`).join(''));
  }
  await copyFile(join(firmware, 'arduino', sketchName, `${sketchName}.ino`), join(sketch, `${sketchName}.ino`));
  const metadata = {
    purpose: lock.purpose, lock, cli: cliVersion, platform: core, sketchName, sources: manifest,
    sketchSha256: await sha256(join(sketch, `${sketchName}.ino`)),
    webAssets: integration ? JSON.parse(await readFile(join(sources, 'web-manifest.json'), 'utf8')) : null,
  };
  await writeFile(join(stage, 'build-inputs.json'), JSON.stringify(metadata, null, 2) + '\n');
  console.log(`Compiling ${sketchName}, reference target ${lock.fqbn}; output: ${stage}`);
  await arduino(['compile', '--fqbn', lock.fqbn, ...(integration ? ['--libraries', join(stage, 'libraries')] : []),
    '--build-path', join(stage, 'output'), '--warnings', 'all', '--jobs', '2', sketch]);
  const binary = join(stage, 'output', `${sketchName}.ino.bin`);
  if (!(await stat(binary)).isFile()) throw new Error('Expected reference binary was not generated');
  await writeFile(join(stage, 'build-result.json'), JSON.stringify({
    referenceOnly: true, sketchName, fqbn: lock.fqbn, binary: relative(stage, binary), sha256: await sha256(binary),
  }, null, 2) + '\n');
  console.log(`PASS: reference ESP32 compile only; no board detection or upload performed\n${binary}`);
}
main().catch((error) => { console.error(error.message); process.exitCode = 1; });
