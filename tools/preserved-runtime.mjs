// Local numerical comparisons require a preserved archive; public smoke tests do not.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdir, readFile } from 'node:fs/promises';
import path from 'node:path';

export async function preparePreservedRuntime(root) {
  const option = process.argv.indexOf('--baseline');
  const label = option === -1 ? 'unification' : process.argv[option + 1];
  assert.ok(['unification', 'portfolio'].includes(label), 'Use --baseline unification|portfolio');
  const baseline = path.join(
    root,
    'build',
    label === 'portfolio' ? 'portfolio-baseline' : 'style-unification-baseline',
  );
  const runtime = path.join(baseline, 'runtime');
  const archive = path.join(baseline, 'sources.zip');
  const manifest = JSON.parse(await readFile(path.join(baseline, 'manifest.json'), 'utf8'));
  const archiveSha256 = createHash('sha256')
    .update(await readFile(archive))
    .digest('hex');
  if (manifest.archiveSha256)
    assert.equal(archiveSha256, manifest.archiveSha256, 'Baseline archive integrity');
  await mkdir(runtime, { recursive: true });
  const result = spawnSync(
    process.env.PYTHON ?? 'python',
    ['-m', 'zipfile', '-e', archive, runtime],
    { cwd: root, encoding: 'utf8', windowsHide: true },
  );
  if (result.error || result.status !== 0) throw result.error ?? new Error(result.stderr);
  const files = manifest.files
    ? manifest.files.map(({ path: file, sha256 }) => [file, sha256])
    : Object.entries(manifest);
  for (const [file, expected] of files) {
    assert.ok(
      !path.isAbsolute(file) && !file.split(/[\\/]/).includes('..'),
      'Invalid baseline path',
    );
    const actual = createHash('sha256')
      .update(await readFile(path.join(runtime, file)))
      .digest('hex');
    assert.equal(actual, expected, `Baseline integrity: ${file}`);
  }
  return { runtime, archive, label };
}
