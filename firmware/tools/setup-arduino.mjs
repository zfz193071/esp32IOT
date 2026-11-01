import { readFile, rename, access, mkdir } from 'node:fs/promises';
import { join, basename } from 'node:path';
import { cli, toolchain, lock, configure, run, arduino, verifyCli, sha256 } from './arduino-environment.mjs';

async function download(url, destination, checksum, allowCoreMirror = false) {
  try { if (await sha256(destination) === checksum) return; }
  catch (error) { if (error.code !== 'ENOENT') throw error; }
  const partial = `${destination}.${process.pid}.partial`;
  console.log(`Downloading official archive: ${basename(destination)}`);
  const fetch = (source) => run('curl', ['--fail', '--location', '--silent', '--show-error', '--connect-timeout', '20',
    '--max-time', '1200', '--retry', '2', source, '--output', partial]);
  try { await fetch(url); }
  catch (error) {
    // Espressif documents this mirror for its GitHub release assets. Keep the
    // official index's checksum; unavailable mirrors must still fail closed.
    if (!allowCoreMirror || !url.startsWith('https://github.com/')) throw error;
    console.log(`Retrying Espressif official mirror: ${basename(destination)}`);
    await fetch(url.replace('https://github.com/', 'https://dl.espressif.com/github_assets/'));
  }
  if (await sha256(partial) !== checksum) throw new Error(`Checksum mismatch: ${basename(destination)}`);
  await rename(partial, destination);
}

async function prefetchCore() {
  const esp = JSON.parse(await readFile(join(toolchain, 'data/package_esp32_index.json'), 'utf8'));
  const arduinoIndex = JSON.parse(await readFile(join(toolchain, 'data/package_index.json'), 'utf8'));
  const packages = [...esp.packages, ...arduinoIndex.packages];
  const pack = packages.find((item) => item.name === 'esp32');
  const platform = pack.platforms.find((item) => item.version === lock.coreVersion);
  if (platform?.checksum !== `SHA-256:${lock.coreArchiveSha256}`) throw new Error('Pinned core archive checksum differs from index');
  const host = process.platform === 'darwin' ? `${process.arch === 'arm64' ? 'arm64' : 'x86_64'}-apple-darwin` :
    `${process.arch === 'arm64' ? 'aarch64' : 'x86_64'}-linux-gnu`;
  const assets = [platform];
  for (const dependency of platform.toolsDependencies) {
    const owner = packages.find((item) => item.name === dependency.packager);
    const tool = owner?.tools.find((item) => item.name === dependency.name && item.version === dependency.version);
    const asset = tool?.systems.find((item) => item.host === host);
    // Let the official installer resolve its platform fallbacks when necessary.
    if (asset) assets.push(asset);
  }
  const cache = join(toolchain, 'downloads/packages');
  await mkdir(cache, { recursive: true });
  let next = 0;
  async function worker() {
    while (next < assets.length) {
      const asset = assets[next++];
      if (basename(asset.archiveFileName) !== asset.archiveFileName || !/^SHA-256:[a-f0-9]{64}$/.test(asset.checksum))
        throw new Error('Unexpected archive metadata in official index');
      await download(asset.url, join(cache, asset.archiveFileName), asset.checksum.slice(8), true);
    }
  }
  const results = await Promise.allSettled([worker(), worker()]);
  const failure = results.find((result) => result.status === 'rejected');
  if (failure) throw failure.reason;
}

async function main() {
  if (process.argv.length !== 2) throw new Error('Usage: node firmware/tools/setup-arduino.mjs');
  const archive = lock.cliArchives[`${process.platform}-${process.arch}`];
  if (!archive) throw new Error('No pinned CLI archive for this host platform');
  await configure();
  let installed = false;
  try { await access(cli); installed = true; } catch (error) { if (error.code !== 'ENOENT') throw error; }
  if (!installed) {
    const downloaded = join(toolchain, 'downloads', archive.name);
    const url = `https://github.com/arduino/arduino-cli/releases/download/v${lock.cliVersion}/${archive.name}`;
    await download(url, downloaded, archive.sha256);
    await run('tar', ['-xzf', downloaded, '-C', join(toolchain, 'bin'), 'arduino-cli']);
  }
  await verifyCli();
  await arduino(['core', 'update-index']);
  await prefetchCore(); // verified cache; Arduino also validates before installing
  await arduino(['core', 'install', `${lock.platform}@${lock.coreVersion}`]);
  console.log(`READY: isolated CLI ${lock.cliVersion}, ${lock.platform}@${lock.coreVersion}; no upload performed`);
}
main().catch((error) => { console.error(error.message); process.exitCode = 1; });
