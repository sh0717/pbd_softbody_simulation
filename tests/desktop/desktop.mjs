import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdtemp, mkdir, copyFile, readFile, readdir, writeFile, stat, rm } from 'node:fs/promises';
import { createReadStream } from 'node:fs';
import { createHash } from 'node:crypto';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const root = fileURLToPath(new URL('../../', import.meta.url));
const config = createRequire(import.meta.url)('../../desktop/config.cjs');
const { version } = JSON.parse(await readFile(path.join(root, 'package.json'), 'utf8'));
const filename = `${config.artifactStem}-${version}-win-x64.exe`;
await mkdir(path.join(root, 'artifacts'), { recursive: true });
await rm(path.join(root, 'artifacts/desktop-results.json'), { force: true });
const testRoot = await mkdtemp(path.join(root, 'artifacts/exe-test-'));
const isolated = path.join(testRoot, '실행파일 하나');
const output = path.join(testRoot, 'report');
const temp = path.join(testRoot, 'temp');
await Promise.all([isolated, output, temp].map((dir) => mkdir(dir, { recursive: true })));
const binary = path.join(isolated, filename);
await copyFile(path.join(root, 'release', filename), binary);
assert.deepEqual(await readdir(isolated), [filename]);
const env = { ...process.env, SIMULATION_TEST_OUTPUT: output, TEMP: temp, TMP: temp };
delete env.ELECTRON_RUN_AS_NODE;
const child = spawn(binary, ['--smoke-test'], {
  cwd: isolated,
  env,
  windowsHide: true,
  stdio: 'ignore',
});
const watchdog = setTimeout(() => {
  if (child.pid) spawn('taskkill', ['/PID', String(child.pid), '/T', '/F'], { windowsHide: true });
}, 180000);
let code;
try {
  code = await new Promise((resolve, reject) => {
    child.once('exit', resolve);
    child.once('error', reject);
  });
} finally {
  clearTimeout(watchdog);
}
const report = JSON.parse(await readFile(path.join(output, 'result.json'), 'utf8'));
console.log(JSON.stringify({ code, testRoot, ...report }, null, 2));
assert.equal(code, 0, report.error);
assert.equal(report.passed, true, report.error);
assert.equal(report.packaged, true);
assert.deepEqual(report.errors, []);
assert.deepEqual(report.blockedRequests, []);
assert.deepEqual(await readdir(isolated), [filename], 'EXE must need no companion files');
const hash = createHash('sha256');
for await (const chunk of createReadStream(binary)) hash.update(chunk);
report.binary = { filename, bytes: (await stat(binary)).size, sha256: hash.digest('hex') };
const checksum = (await readFile(path.join(root, 'release', `${filename}.sha256`), 'utf8')).trim();
assert.equal(
  checksum,
  `${report.binary.sha256}  ${filename}`,
  'published checksum must match tested EXE',
);
report.isolation = { directory: isolated, files: await readdir(isolated), exitCode: code };
report.generatedAt = new Date().toISOString();
await writeFile(
  path.join(root, 'artifacts/desktop-results.json'),
  JSON.stringify(report, null, 2) + '\n',
);
await copyFile(path.join(output, 'desktop.png'), path.join(root, 'artifacts/desktop-exe.png'));
if ((await readdir(output)).includes('inspector.png'))
  await copyFile(
    path.join(output, 'inspector.png'),
    path.join(root, 'artifacts/desktop-exe-inspector.png'),
  );
console.log(`PASS: isolated portable EXE; report: ${output}`);
