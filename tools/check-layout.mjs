import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const root = fileURLToPath(new URL('../', import.meta.url));
for (const relative of [
  'src/index.html',
  'src/styles/app.css',
  'src/app.js',
  'src/simulation',
  'src/rendering',
  'src/ui',
  'native/src',
  'native/bindings',
  'native/sources.txt',
  'wasm',
  'dist/index.html',
  'desktop/config.cjs',
  'desktop/main.cjs',
  'desktop/smoke.cjs',
  'tests/desktop/desktop.mjs',
  'tools/build.mjs',
  'tools/build-web.mjs',
  'tools/build-core.mjs',
  'tools/native.mjs',
  'CMakePresets.json',
])
  assert.ok(existsSync(path.join(root, relative)), `Common layout missing: ${relative}`);
const pkg = JSON.parse(await readFile(path.join(root, 'package.json'), 'utf8'));
for (const relative of ['tools/npm-runner.mjs', 'tools/run.mjs'])
  assert.ok(existsSync(path.join(root, relative)), `Missing script runner: ${relative}`);
for (const [name, command] of Object.entries({
  build: 'node tools/build.mjs',
  start: 'node tools/serve.mjs',
  'build:web': 'node tools/build.mjs',
  'build:core': 'node tools/build-core.mjs',
  'build:all': 'npm run build:core && npm run build',
  'build:native': 'node tools/native.mjs build',
  'build:native:release': 'node tools/native.mjs build --release',
  'test:native': 'node tools/native.mjs test',
  'test:native:release': 'node tools/native.mjs test --release',
  'build:exe': 'node tools/package-desktop.mjs',
  'test:exe': 'node tests/desktop/desktop.mjs',
  'release:exe': 'node tools/release-desktop.mjs',
}))
  assert.equal(pkg.scripts[name], command, `Common build command differs: ${name}`);
const presets = JSON.parse(await readFile(path.join(root, 'CMakePresets.json'), 'utf8'));
assert.equal(presets.configurePresets[0].binaryDir, '${sourceDir}/build/native');
for (const group of ['buildPresets', 'testPresets'])
  for (const configuration of ['debug', 'release'])
    assert.ok(presets[group].some(({ name }) => name === `native-${configuration}`));
const source = await readFile(path.join(root, 'src/index.html'), 'utf8');
assert.ok(!/\b(?:src|href)=["']https?:/i.test(source), 'HTML must not load assets from a CDN');
console.log('Common source/output layout and build command contracts passed.');
