import assert from 'node:assert/strict';
import test from 'node:test';
import { mkdtemp, mkdir, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import {
  runtimeFingerprint,
  sourceFingerprint,
  verifyExecution,
} from '../../tools/fingerprint.mjs';

test('source fingerprint changes with source edits but survives line endings and generated outputs', async () => {
  const root = await mkdtemp(path.join(tmpdir(), 'portfolio-quality-'));

  try {
    await mkdir(path.join(root, 'src/simulation/generated'), { recursive: true });
    await mkdir(path.join(root, 'docs'));

    const source = path.join(root, 'src/app.js');

    await writeFile(source, 'const value = 1;\n');

    const original = (await sourceFingerprint(root)).sha256;
    const originalRuntime = (await runtimeFingerprint(root)).sha256;

    await writeFile(source, 'const value = 1;\r\n');
    await writeFile(path.join(root, 'src/simulation/generated/core-binary.js'), 'generated output');
    await writeFile(path.join(root, 'docs/portfolio-score.json'), '{}');

    assert.equal((await sourceFingerprint(root)).sha256, original);
    assert.notEqual((await runtimeFingerprint(root)).sha256, originalRuntime);

    const beforeStandalone = (await runtimeFingerprint(root)).sha256;

    await writeFile(path.join(root, 'SPH-Fluid-Lab.html'), 'generated standalone');

    assert.equal((await sourceFingerprint(root)).sha256, original);
    assert.notEqual((await runtimeFingerprint(root)).sha256, beforeStandalone);

    await writeFile(path.join(root, 'yoosh.html'), 'another generated standalone');

    assert.equal((await sourceFingerprint(root)).sha256, original);

    await mkdir(path.join(root, '.vscode'));
    await writeFile(
      path.join(root, '.vscode/c_cpp_properties.json'),
      'generated local compiler paths',
    );

    assert.equal((await sourceFingerprint(root)).sha256, original);

    await writeFile(path.join(root, '.vscode/settings.json'), '{}');

    assert.notEqual((await sourceFingerprint(root)).sha256, original);

    const beforeSourceEdit = (await sourceFingerprint(root)).sha256;

    await writeFile(source, 'const value = 2;\n');

    assert.notEqual((await sourceFingerprint(root)).sha256, beforeSourceEdit);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test('source fingerprint matches public files while retaining ordinary scripts and documents', async () => {
  const root = await mkdtemp(path.join(tmpdir(), 'portfolio-public-source-'));

  try {
    await mkdir(path.join(root, 'src'));
    await mkdir(path.join(root, 'docs'));
    await writeFile(path.join(root, 'src/app.js'), 'public source');

    const original = await sourceFingerprint(root);

    for (const relative of ['build_report.py', '추가설명.txt']) {
      const file = path.join(root, relative);

      await writeFile(file, 'private historical material');

      assert.deepEqual(await sourceFingerprint(root), original);

      await writeFile(file, 'edited private material');

      assert.deepEqual(await sourceFingerprint(root), original);

      await rm(file);

      assert.deepEqual(await sourceFingerprint(root), original);
    }

    for (const relative of ['script.py', 'notes.txt', 'src/build_report.py', 'docs/추가설명.txt']) {
      const file = path.join(root, relative);

      await writeFile(file, 'first public version');

      const created = await sourceFingerprint(root);

      assert.ok(created.files.includes(relative));
      assert.notEqual(created.sha256, original.sha256);

      await writeFile(file, 'edited public version');

      assert.notEqual((await sourceFingerprint(root)).sha256, created.sha256);

      await rm(file);

      assert.deepEqual(await sourceFingerprint(root), original);
    }
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test('runtime fingerprint detects nested output creation, edits and deletion in every root', async () => {
  const root = await mkdtemp(path.join(tmpdir(), 'portfolio-runtime-tree-'));

  try {
    await mkdir(path.join(root, 'dist'));
    await writeFile(path.join(root, 'dist/index.html'), 'unchanged entry');

    const original = await runtimeFingerprint(root);

    for (const directory of ['wasm', 'dist', 'src/simulation/generated']) {
      const relative = `${directory}/nested/probe.js`;
      const output = path.join(root, relative);

      await mkdir(path.dirname(output), { recursive: true });
      await writeFile(output, 'version one');

      const created = await runtimeFingerprint(root);

      assert.ok(created.files.includes(relative), `${relative}: nested output is missing`);
      assert.notEqual(created.sha256, original.sha256);

      await writeFile(output, 'version two');

      assert.notEqual((await runtimeFingerprint(root)).sha256, created.sha256);

      await rm(output);

      assert.deepEqual(await runtimeFingerprint(root), original);
    }
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test('execution verification rejects stale source, changed runtime and incomplete or failed gates', async () => {
  const root = await mkdtemp(path.join(tmpdir(), 'pbd-execution-'));

  try {
    await mkdir(path.join(root, 'artifacts'));
    await mkdir(path.join(root, 'src'));
    await mkdir(path.join(root, 'dist'));

    await writeFile(path.join(root, 'src/app.js'), 'source');
    await writeFile(path.join(root, 'dist/app.js'), 'runtime');

    const execution = {
      schemaVersion: 1,
      sourceSha256: (await sourceFingerprint(root)).sha256,
      runtimeSha256: (await runtimeFingerprint(root)).sha256,
      checks: [
        'build',
        'check',
        'native:Debug',
        'native:Release',
        'browser',
        'parallel-browser',
      ].map((name) => ({
        name,
        exitCode: 0,
      })),
    };
    const record = path.join(root, 'artifacts/validation.json');

    await writeFile(record, JSON.stringify(execution));

    assert.equal((await verifyExecution(root)).gates, 6);

    for (const [change, pattern] of [
      [{ sourceSha256: 'stale' }, /stale/],
      [{ runtimeSha256: 'changed' }, /runtime changed/],
      [{ checks: execution.checks.slice(0, -1) }, /Expected values/],
      [{ checks: execution.checks.map((check) => ({ ...check, exitCode: 1 })) }, /gate failed/],
    ]) {
      await writeFile(record, JSON.stringify({ ...execution, ...change }));

      await assert.rejects(() => verifyExecution(root), pattern);
    }

    await writeFile(record, JSON.stringify(execution));
    await writeFile(path.join(root, 'src/app.js'), 'edited source');

    await assert.rejects(() => verifyExecution(root), /stale/);

    await writeFile(path.join(root, 'src/app.js'), 'source');
    await writeFile(path.join(root, 'dist/app.js'), 'edited runtime');

    await assert.rejects(() => verifyExecution(root), /runtime changed/);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});
