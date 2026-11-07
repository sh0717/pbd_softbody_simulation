import { npmRun } from './npm-runner.mjs';
import { readFile, writeFile, rm } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import path from 'node:path';

const root = fileURLToPath(new URL('../', import.meta.url));
// An unsuccessful attempt must never leave a previous success marker in place.
await rm(path.join(root, 'release/release-manifest.json'), { force: true });
for (const command of ['build', 'check', 'test:distribution', 'build:exe', 'test:exe']) {
  const result = npmRun(command, {
    cwd: root,
    stdio: 'inherit',
    windowsHide: true,
  });
  if (result.error || result.status !== 0) throw result.error || new Error(`${command} failed`);
}
const pkg = JSON.parse(await readFile(path.join(root, 'package.json'), 'utf8'));
const report = JSON.parse(
  await readFile(path.join(root, 'artifacts/desktop-results.json'), 'utf8'),
);
const bytes = await readFile(path.join(root, 'release', report.binary.filename));
if (!report.passed || createHash('sha256').update(bytes).digest('hex') !== report.binary.sha256)
  throw new Error('The packaged EXE does not match the successful smoke test.');
await writeFile(
  path.join(root, 'release/release-manifest.json'),
  JSON.stringify(
    {
      schemaVersion: 1,
      generatedAt: new Date().toISOString(),
      project: pkg.name,
      version: pkg.version,
      target: 'win-x64-portable',
      signed: false,
      offline: true,
      node: process.version,
      electron: pkg.devDependencies.electron,
      electronBuilder: pkg.devDependencies['electron-builder'],
      binary: report.binary,
      checks: report.checks,
      errors: report.errors,
      blockedRequests: report.blockedRequests,
      isolation: report.isolation,
    },
    null,
    2,
  ) + '\n',
);
console.log('PASS: release/release-manifest.json');
