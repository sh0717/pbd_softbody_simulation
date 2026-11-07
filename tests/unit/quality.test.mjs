import assert from 'node:assert/strict';
import test from 'node:test';
import { mkdtemp, mkdir, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import {
  CRITERIA,
  evaluateScore,
  runtimeFingerprint,
  sourceFingerprint,
} from '../../tools/quality.mjs';

function report() {
  return {
    schemaVersion: 1,
    reviewer: 'independent-verification',
    reviewedAt: '2026-10-26T00:00:00Z',
    criteria: CRITERIA.map((id) => ({
      id,
      score: 5,
      reason: 'Reviewed',
      evidence: [{ path: 'README.md', line: 1 }],
    })),
    blockers: [],
  };
}

test('quality gate rejects missing or duplicated criteria and unevidenced scores', () => {
  for (const mutate of [
    (value) => value.criteria.pop(),
    (value) => {
      value.criteria[0].id = 'R2';
    },
    (value) => {
      value.criteria[0].evidence = [];
    },
    (value) => {
      value.criteria[0].evidence[0].path = '../README.md';
    },
  ]) {
    const value = report();
    mutate(value);
    assert.throws(() => evaluateScore(value));
  }
});

test('quality gate rejects high totals with a weak group, criterion, or blocking defect', () => {
  for (const mutate of [
    (value) =>
      value.criteria
        .filter(({ id }) => id.startsWith('P'))
        .forEach((item) => {
          item.score = 3;
        }),
    (value) => {
      value.criteria[0].score = 2;
    },
    (value) => {
      value.blockers = ['Unresolved failure'];
    },
    (value) =>
      value.criteria.forEach((item) => {
        item.score = 4;
      }),
  ]) {
    const value = report();
    mutate(value);
    assert.throws(() => evaluateScore(value));
  }
  assert.equal(evaluateScore(report()).total, 100);
  const belowTarget = report();
  belowTarget.criteria[0].score = 4;
  belowTarget.criteria[1].score = 4;
  belowTarget.criteria[2].score = 4;
  assert.throws(() => evaluateScore(belowTarget), /below 98/);
  belowTarget.criteria[2].score = 5;
  assert.equal(evaluateScore(belowTarget).total, 98);
});

test('review fingerprint changes with source edits but survives line endings and generated outputs', async () => {
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
