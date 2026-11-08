// 검사 시작 시점의 소스와 배포 바이트를 완료 시점까지 고정합니다.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { mkdir, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { runtimeFingerprint, sourceFingerprint } from './fingerprint.mjs';

const root = fileURLToPath(new URL('../', import.meta.url));
const npmCli = process.env.npm_execpath;
assert.ok(npmCli && existsSync(npmCli), 'Run this tool with npm run verify');
const checks = [];
await mkdir(path.join(root, 'artifacts'), { recursive: true });

function run(name, command, args) {
  console.log(`\n[${name}] ${command} ${args.join(' ')}`);
  const start = performance.now();
  const result = spawnSync(command, args, { cwd: root, encoding: 'utf8', windowsHide: true });
  const output = (result.stdout || '') + (result.stderr || '') + (result.error?.message || '');
  process.stdout.write(output);
  const log = `artifacts/verify-${name.replaceAll(':', '-')}.log`;
  checks.push({
    name,
    exitCode: result.status ?? -1,
    elapsedMs: Math.round(performance.now() - start),
    log,
  });
  return writeFile(path.join(root, log), output).then(() => {
    assert.equal(result.status, 0, `${name} failed; see ${log}`);
  });
}

function findCMake() {
  if (process.env.CMAKE) return process.env.CMAKE;
  const found = spawnSync(process.platform === 'win32' ? 'where.exe' : 'which', ['cmake'], {
    encoding: 'utf8',
    windowsHide: true,
  });
  if (found.status === 0) return found.stdout.trim().split(/\r?\n/)[0];
  if (process.platform === 'win32') {
    const locator = path.join(
      process.env['ProgramFiles(x86)'] || 'C:/Program Files (x86)',
      'Microsoft Visual Studio/Installer/vswhere.exe',
    );
    const vs = spawnSync(
      locator,
      [
        '-latest',
        '-products',
        '*',
        '-requires',
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
        '-property',
        'installationPath',
      ],
      { encoding: 'utf8', windowsHide: true },
    );
    const candidate = path.join(
      vs.stdout?.trim() || '',
      'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe',
    );
    if (existsSync(candidate)) return candidate;
  }
  throw new Error('Install CMake and a C++20 compiler, or set CMAKE');
}

let sourceSha256;
let runtimeSha256;
try {
  await run('build', process.execPath, [npmCli, 'run', 'build:all']);
  sourceSha256 = (await sourceFingerprint(root)).sha256;
  runtimeSha256 = (await runtimeFingerprint(root)).sha256;
  await run('check', process.execPath, [npmCli, 'run', 'check']);
  const cmake = findCMake();
  const ctest = path.join(
    path.dirname(cmake),
    process.platform === 'win32' ? 'ctest.exe' : 'ctest',
  );
  for (const configuration of ['Debug', 'Release']) {
    const directory = `build/verify-native-${configuration.toLowerCase()}`;
    // Keep native execution in one gate, retaining configure/build output as well.
    const args = [
      '-S',
      '.',
      '-B',
      directory,
      ...(process.platform === 'win32' ? ['-A', 'x64'] : [`-DCMAKE_BUILD_TYPE=${configuration}`]),
    ];
    const commands = [
      [cmake, args],
      [cmake, ['--build', directory, '--config', configuration, '--parallel']],
      [
        ctest,
        ['--test-dir', directory, '-C', configuration, '--output-on-failure', '--no-tests=error'],
      ],
    ];
    let output = '';
    let exitCode = 0;
    const start = performance.now();
    for (const [command, commandArgs] of commands) {
      const result = spawnSync(command, commandArgs, {
        cwd: root,
        encoding: 'utf8',
        windowsHide: true,
      });
      const chunk = (result.stdout || '') + (result.stderr || '') + (result.error?.message || '');
      process.stdout.write(chunk);
      output += chunk;
      exitCode = result.status ?? -1;
      if (exitCode !== 0) break;
    }
    const log = `artifacts/verify-native-${configuration}.log`;
    await writeFile(path.join(root, log), output);
    checks.push({
      name: `native:${configuration}`,
      exitCode,
      elapsedMs: Math.round(performance.now() - start),
      log,
    });
    assert.equal(exitCode, 0, `Native ${configuration} failed; see ${log}`);
  }
  await run('browser', process.execPath, [npmCli, 'run', 'test:browser']);
  assert.equal(
    (await sourceFingerprint(root)).sha256,
    sourceSha256,
    'Source changed during verification',
  );
  assert.equal(
    (await runtimeFingerprint(root)).sha256,
    runtimeSha256,
    'Runtime changed during verification',
  );
} catch (error) {
  console.error(error.message);
  process.exitCode = 1;
} finally {
  await writeFile(
    path.join(root, 'artifacts/validation.json'),
    JSON.stringify(
      {
        schemaVersion: 1,
        sourceSha256,
        runtimeSha256,
        completedAt: new Date().toISOString(),
        node: process.version,
        platform: process.platform,
        checks,
      },
      null,
      2,
    ) + '\n',
  );
}
