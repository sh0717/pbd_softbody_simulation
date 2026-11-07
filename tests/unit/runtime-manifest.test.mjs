import assert from 'node:assert/strict';
import test from 'node:test';
import { mkdtemp, mkdir, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { checkWasmManifest, contentHash } from '../../tools/check-runtime.mjs';

test('Wasm provenance rejects edited sources, omitted inputs, and replaced deployed bytes', async () => {
  const root = await mkdtemp(path.join(tmpdir(), 'wasm-provenance-'));
  try {
    const sourceHashes = {};
    const outputHashes = {};
    for (const [files, hashes] of [
      [
        ['native/src/Core.cpp', 'native/src/Core.h', 'native/sources.txt', 'tools/build_wasm.py'],
        sourceHashes,
      ],
      [['wasm/pbd_core.js', 'wasm/pbd_core.wasm'], outputHashes],
    ]) {
      for (const file of files) {
        await mkdir(path.dirname(path.join(root, file)), { recursive: true });
        const bytes = Buffer.from(file.endsWith('.wasm') ? [0, 97, 115, 109] : 'original\r\n');
        await writeFile(path.join(root, file), bytes);
        hashes[file] = contentHash(bytes, file.endsWith('.wasm'));
      }
    }
    const manifest = {
      schemaVersion: 1,
      sdk: { version: '4.0.23' },
      languageStandard: 'c++20',
      textNormalization: 'LF',
      sourceHashes,
      outputHashes,
    };
    await checkWasmManifest(root, manifest);
    await assert.rejects(
      checkWasmManifest(root, { ...manifest, languageStandard: 'c++17' }),
      /Unexpected C\+\+ standard/,
    );
    await writeFile(path.join(root, 'native/src/Core.cpp'), 'original\n');
    await checkWasmManifest(root, manifest);
    await assert.rejects(checkWasmManifest(root, { ...manifest, sourceHashes: {} }), /source set/);
    await writeFile(path.join(root, 'native/src/Core.cpp'), 'changed\n');
    await assert.rejects(checkWasmManifest(root, manifest), /provenance changed/);
    await writeFile(path.join(root, 'native/src/Core.cpp'), 'original\n');
    await writeFile(path.join(root, 'wasm/pbd_core.wasm'), Buffer.from([0, 97, 115, 110]));
    await assert.rejects(checkWasmManifest(root, manifest), /provenance changed/);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});
