// 실행에 사용한 소스와 배포 바이트의 변경 여부를 SHA-256으로 확인합니다.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { existsSync } from 'node:fs';
import { readdir, readFile, stat } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

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

export async function verifyExecution(root) {
  const execution = JSON.parse(
    await readFile(path.join(root, 'artifacts/validation.json'), 'utf8'),
  );

  assert.equal(execution.schemaVersion, 1, 'Unsupported execution schema');

  const source = await sourceFingerprint(root);
  const runtime = await runtimeFingerprint(root);

  assert.equal(execution.sourceSha256, source.sha256, 'Execution results are stale');
  assert.equal(
    execution.runtimeSha256,
    runtime.sha256,
    'Deployed runtime changed: rerun execution gates',
  );
  assert.deepEqual(
    execution.checks.map(({ name }) => name),
    ['build', 'check', 'native:Debug', 'native:Release', 'browser', 'parallel-browser'],
  );
  assert.ok(
    execution.checks.every(({ exitCode }) => exitCode === 0),
    'An execution gate failed',
  );
  return {
    sourceSha256: source.sha256,
    runtimeSha256: runtime.sha256,
    gates: execution.checks.length,
  };
}

async function main() {
  const root = fileURLToPath(new URL('../', import.meta.url));

  if (process.argv.includes('--fingerprint')) {
    console.log(JSON.stringify(await sourceFingerprint(root), null, 2));
  } else if (process.argv.includes('--execution')) {
    console.log(JSON.stringify(await verifyExecution(root), null, 2));
  } else throw new Error('Use --fingerprint or --execution');
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch((error) => {
    console.error(error.message);
    process.exitCode = 1;
  });
}
