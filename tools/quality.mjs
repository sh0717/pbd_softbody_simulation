// Review scores are judgments with source evidence, never a substitute for execution.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { existsSync } from 'node:fs';
import { readdir, readFile, stat, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export const GROUPS = {
  R: { name: '가독성', count: 6 },
  A: { name: '설계와 책임', count: 5 },
  S: { name: '정확성과 안전성', count: 4 },
  B: { name: '빌드와 검증 재현', count: 3 },
  P: { name: '제출 자료', count: 2 },
};
export const CRITERIA = Object.entries(GROUPS).flatMap(([group, { count }]) =>
  Array.from({ length: count }, (_, index) => `${group}${index + 1}`),
);

export function evaluateScore(report) {
  assert.equal(report.schemaVersion, 1, 'Unsupported review schema');
  assert.equal(report.reviewer, 'independent-verification', 'An independent review is required');
  assert.ok(Number.isFinite(Date.parse(report.reviewedAt)), 'Review date is required');
  assert.ok(Array.isArray(report.criteria), 'Missing criteria');
  assert.deepEqual(
    report.criteria.map(({ id }) => id).sort(),
    [...CRITERIA].sort(),
    'Every criterion must appear exactly once',
  );
  const totals = Object.fromEntries(Object.keys(GROUPS).map((key) => [key, 0]));
  for (const criterion of report.criteria) {
    const { id, score, reason, evidence } = criterion;
    assert.ok(Number.isInteger(score) && score >= 3 && score <= 5, `${id}: requires 3..5 points`);
    assert.ok(typeof reason === 'string' && reason.trim().length > 0, `${id}: missing judgment`);
    assert.ok(Array.isArray(evidence) && evidence.length > 0, `${id}: missing source evidence`);
    for (const item of evidence) {
      assert.ok(
        typeof item.path === 'string' &&
          !path.isAbsolute(item.path) &&
          !item.path.includes('\\') &&
          !item.path.split('/').includes('..'),
        `${id}: evidence must use a repository-relative path`,
      );
      assert.ok(Number.isInteger(item.line) && item.line > 0, `${id}: invalid evidence line`);
    }
    totals[id[0]] += score;
  }
  assert.ok(
    Array.isArray(report.blockers) && report.blockers.length === 0,
    'Open blocking defects',
  );
  for (const [group, { name, count }] of Object.entries(GROUPS)) {
    assert.ok(totals[group] >= count * 4, `${name}: requires at least 80%`);
  }
  const total = Object.values(totals).reduce((sum, value) => sum + value, 0);
  assert.ok(total >= 98, `Review score ${total}/100 is below 98`);
  return { total, groups: totals };
}

// Include executable inputs and reader-facing contracts. Review records and outputs
// are excluded to avoid a circular fingerprint when the reviewer records the result.
export async function sourceFingerprint(root) {
  const files = [];
  const excluded = new Set([
    'portfolio-review.md',
    'portfolio-refactoring.md',
    'portfolio-score.json',
    'c_cpp_properties.json',
    'CMakeUserPresets.json',
  ]);
  const extensions =
    /\.(?:[cm]?js|cpp|h|hpp|json|txt|cmake|ps1|py|html|css|md|yml|yaml|wasm|toml|cmd)$/;
  async function visit(directory) {
    if (!existsSync(directory)) return;
    for (const entry of await readdir(directory, { withFileTypes: true })) {
      if (['__pycache__', 'generated'].includes(entry.name) || excluded.has(entry.name)) continue;
      const absolute = path.join(directory, entry.name);
      if (entry.isDirectory()) await visit(absolute);
      else if (extensions.test(entry.name)) files.push(absolute);
    }
  }
  for (const directory of [
    'src',
    'native',
    'tools',
    'tests',
    'docs',
    'desktop',
    '.github',
    '.vscode',
  ]) {
    // Historical screenshots/logs and legacy Python reports are not execution inputs.
    if (directory === 'tests') {
      for (const subdirectory of ['unit', 'native', 'browser', 'desktop', 'fixtures']) {
        await visit(path.join(root, directory, subdirectory));
      }
    } else await visit(path.join(root, directory));
  }
  for (const name of await readdir(root)) {
    if (
      extensions.test(name) ||
      [
        '.clang-format',
        '.gitignore',
        '.gitattributes',
        '.editorconfig',
        '.prettierignore',
      ].includes(name)
    ) {
      // 공개 정책에서 제외한 루트의 과거 보고서 자료도 로컬에 보존합니다.
      if (
        [
          'SPH-Fluid-Lab.html',
          'yoosh.html',
          'CMakeUserPresets.json',
          'build_report.py',
          '추가설명.txt',
        ].includes(name)
      )
        continue;
      const absolute = path.join(root, name);
      if ((await stat(absolute)).isFile()) files.push(absolute);
    }
  }
  const hash = createHash('sha256');
  const relativeFiles = [];
  for (const file of [...new Set(files)].sort()) {
    const relative = path.relative(root, file).replaceAll('\\', '/');
    relativeFiles.push(relative);
    const bytes = await readFile(file);
    const content = file.endsWith('.wasm')
      ? bytes
      : bytes.toString('utf8').replaceAll('\r\n', '\n');
    hash.update(relative).update('\0').update(content).update('\0');
  }
  return { sha256: hash.digest('hex'), files: relativeFiles };
}

// Execution evidence also belongs to the exact deployed bytes. Rebuilding may
// change these without changing the source reviewed by the independent reviewer.
export async function runtimeFingerprint(root) {
  const files = [];
  async function visit(directory) {
    if (!existsSync(directory)) return;
    for (const entry of await readdir(directory, { withFileTypes: true })) {
      const file = path.join(directory, entry.name);
      if (entry.isDirectory()) await visit(file);
      else if (entry.isFile()) files.push(file);
    }
  }
  for (const directory of ['src/simulation/generated', 'wasm', 'dist']) {
    await visit(path.join(root, directory));
  }
  if (existsSync(path.join(root, 'yoosh.html'))) files.push(path.join(root, 'yoosh.html'));
  if (existsSync(path.join(root, 'SPH-Fluid-Lab.html')))
    files.push(path.join(root, 'SPH-Fluid-Lab.html'));
  const hash = createHash('sha256');
  const relativeFiles = [];
  for (const file of files.sort()) {
    const relative = path.relative(root, file).replaceAll('\\', '/');
    relativeFiles.push(relative);
    hash
      .update(relative)
      .update('\0')
      .update(await readFile(file))
      .update('\0');
  }
  return { sha256: hash.digest('hex'), files: relativeFiles };
}

async function main() {
  const root = fileURLToPath(new URL('../', import.meta.url));
  const fingerprint = await sourceFingerprint(root);
  if (process.argv.includes('--fingerprint')) {
    console.log(JSON.stringify(fingerprint, null, 2));
    return;
  }
  const reportFile = path.join(root, 'docs/portfolio-score.json');
  const report = JSON.parse(await readFile(reportFile, 'utf8'));
  const pkg = JSON.parse(await readFile(path.join(root, 'package.json'), 'utf8'));
  assert.equal(report.project, pkg.name, 'Wrong project review');
  const result = evaluateScore(report);
  for (const { id, evidence } of report.criteria) {
    for (const item of evidence) {
      const source = await readFile(path.join(root, item.path), 'utf8');
      assert.ok(item.line <= source.split(/\r?\n/).length, `${id}: evidence line is out of bounds`);
    }
  }
  if (process.argv.includes('--seal')) {
    report.sourceSha256 = fingerprint.sha256;
    await writeFile(reportFile, JSON.stringify(report, null, 2) + '\n');
  } else
    assert.equal(
      report.sourceSha256,
      fingerprint.sha256,
      'Source changed: independent re-review required',
    );
  if (process.argv.includes('--execution')) {
    const execution = JSON.parse(
      await readFile(path.join(root, 'artifacts/portfolio-validation.json'), 'utf8'),
    );
    assert.equal(execution.sourceSha256, fingerprint.sha256, 'Execution results are stale');
    assert.equal(
      execution.runtimeSha256,
      (await runtimeFingerprint(root)).sha256,
      'Deployed runtime changed: rerun execution gates',
    );
    assert.deepEqual(
      execution.checks.map(({ name }) => name),
      ['build', 'check', 'native:Debug', 'native:Release', 'browser'],
    );
    assert.ok(
      execution.checks.every(({ exitCode }) => exitCode === 0),
      'An execution gate failed',
    );
  }
  console.log(
    `Independent review: ${result.total}/100; ${JSON.stringify(result.groups)}; ${fingerprint.files.length} fingerprinted inputs.`,
  );
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch((error) => {
    console.error(error.message);
    process.exitCode = 1;
  });
}
