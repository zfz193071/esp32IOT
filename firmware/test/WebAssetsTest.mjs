import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, readFile, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';

const firmware = fileURLToPath(new URL('../', import.meta.url));
test('web resources reproducibly compile and serve byte-identical UTF-8 payloads', async () => {
  const dir = await mkdtemp(join(tmpdir(), 'esp32-web-assets-'));
  try {
    const generate = () => execFileSync(process.execPath, [join(firmware, 'tools/embed-web.mjs'), dir]);
    generate();
    const first = await readFile(join(dir, 'WebAssets.cpp'));
    generate();
    assert.deepEqual(await readFile(join(dir, 'WebAssets.cpp')), first);
    const manifest = JSON.parse(await readFile(join(dir, 'web-manifest.json'), 'utf8'));
    assert.deepEqual(manifest.map((item) => item.route), ['/', '/style.css', '/app.mjs', '/client.mjs', '/config.mjs']);
    const smoke = join(dir, 'smoke.cpp');
    await writeFile(smoke, `#include "WebAssets.h"
#include <cstdio>
#include <cstring>
int main(int argc, char** argv) {
    using namespace esp32fan::web;
    if (assetCount != 5 || findAsset(nullptr) || findAsset("/missing") || findAsset("/../index.html")) return 1;
    if (argc != 2) return 2;
    const Asset* asset = findAsset(argv[1]);
    if (!asset) return 3;
    if (std::strstr(argv[1], ".mjs") && !std::strstr(asset->contentType, "text/javascript")) return 4;
    return std::fwrite(asset->data, 1, asset->size, stdout) == asset->size ? 0 : 5;
}
`);
    const binary = join(dir, 'smoke');
    execFileSync(process.env.CXX || 'c++', ['-std=c++11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
      '-fno-exceptions', '-fno-rtti', '-I', dir, join(dir, 'WebAssets.cpp'), smoke, '-o', binary]);
    for (const item of manifest) {
      const original = await readFile(join(firmware, 'web', item.filename));
      assert.deepEqual(execFileSync(binary, [item.route]), original);
      assert.equal(item.size, original.length);
      assert.equal(item.sha256, createHash('sha256').update(original).digest('hex'));
    }
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
});
