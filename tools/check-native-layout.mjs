import assert from 'node:assert/strict';
import { readdir, readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
async function directories(relative) {
  return (await readdir(path.join(root, relative), { withFileTypes: true }))
    .filter((entry) => entry.isDirectory())
    .map((entry) => entry.name)
    .sort();
}
assert.deepEqual(
  await directories('native'),
  ['bindings', 'src'],
  'Use native/bindings and native/src. Put standalone runners in tests/native.',
);
assert.deepEqual(
  await directories('native/src'),
  ['collision', 'core', 'simulation'],
  'Use the shared core/simulation/collision classification.',
);
const implementations = [];
async function visit(relative) {
  for (const entry of await readdir(path.join(root, relative), { withFileTypes: true })) {
    const file = `${relative}/${entry.name}`;
    if (entry.isDirectory()) await visit(file);
    else if (entry.name.endsWith('.cpp')) implementations.push(file);
  }
}
await visit('native');
const manifest = (await readFile(path.join(root, 'native/sources.txt'), 'utf8'))
  .split(/\r?\n/)
  .map((line) => line.trim())
  .filter(Boolean);
assert.equal(new Set(manifest).size, manifest.length);
assert.deepEqual(
  [...manifest].sort(),
  implementations.sort(),
  'Every native implementation must be in the shared source manifest.',
);
assert.ok(manifest.includes('native/bindings/Bindings.cpp'), 'Use the shared API entry filename.');
for (const category of ['core', 'simulation', 'collision']) {
  const files = await readdir(path.join(root, 'native/src', category), { withFileTypes: true });
  assert.ok(
    files.some((file) => file.isFile() && file.name.endsWith('.h')),
    `Missing ${category} headers`,
  );
  assert.ok(
    files.every((file) => file.isFile() && /\.(?:h|cpp)$/.test(file.name)),
    `${category}: co-locate module .h/.cpp files`,
  );
}
console.log(
  `Native C++ layout passed: bindings + core/simulation/collision (${manifest.length} implementations).`,
);
