// Refuse tests against a deployed Wasm built from different C++ inputs.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { existsSync } from 'node:fs';
import { readdir, readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

export function contentHash(bytes, binary = false) {
  const content = binary
    ? bytes
    : bytes.toString('utf8').replaceAll('\r\n', '\n').replaceAll('\r', '\n');
  return createHash('sha256').update(content).digest('hex');
}

export async function checkWasmManifest(root, manifest, moduleStem = 'pbd_core') {
  assert.match(moduleStem, /^[a-z][a-z0-9_-]*$/, 'Invalid deployed module name');
  assert.equal(manifest.schemaVersion, 1, 'Unknown Wasm manifest schema');
  assert.equal(manifest.sdk?.version, '4.0.23', 'Unexpected Emscripten version');
  assert.equal(manifest.languageStandard, 'c++20', 'Unexpected C++ standard');
  assert.equal(manifest.textNormalization, 'LF', 'Unexpected text hashing contract');
  const sources = ['native/sources.txt', 'tools/build_wasm.py'];
  async function visit(directory) {
    for (const entry of await readdir(path.join(root, directory), { withFileTypes: true })) {
      const file = `${directory}/${entry.name}`;
      if (entry.isDirectory()) await visit(file);
      else if (/\.(?:cpp|h)$/.test(entry.name)) sources.push(file);
    }
  }
  await visit('native');
  assert.deepEqual(
    Object.keys(manifest.sourceHashes).sort(),
    sources.sort(),
    'Wasm manifest source set changed: rebuild core',
  );
  assert.deepEqual(
    Object.keys(manifest.outputHashes).sort(),
    [`wasm/${moduleStem}.js`, `wasm/${moduleStem}.wasm`].sort(),
    'Unexpected deployed outputs',
  );
  for (const hashes of [manifest.sourceHashes, manifest.outputHashes]) {
    for (const [file, expected] of Object.entries(hashes)) {
      const actual = contentHash(await readFile(path.join(root, file)), file.endsWith('.wasm'));
      assert.equal(actual, expected, `${file}: Wasm provenance changed; run npm run build:core`);
    }
  }
}

async function main() {
  const root = fileURLToPath(new URL('../', import.meta.url));
  const boidsChecker = path.join(root, 'tools/check-core.mjs');
  if (existsSync(boidsChecker)) {
    const { checkCore } = await import(pathToFileURL(boidsChecker));
    await checkCore();
  } else {
    const manifest = JSON.parse(
      await readFile(path.join(root, 'wasm/build-manifest.json'), 'utf8'),
    );
    const pkg = JSON.parse(await readFile(path.join(root, 'package.json'), 'utf8'));
    await checkWasmManifest(root, manifest, pkg.wasmModule ?? 'pbd_core');
  }
  console.log('Deployed Wasm matches current source and recorded output hashes.');
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch((error) => {
    console.error(error.message);
    process.exitCode = 1;
  });
}
