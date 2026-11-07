import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const action = process.argv[2] || 'build';
if (!['configure', 'build', 'test'].includes(action))
  throw new Error(`Unknown native action: ${action}`);
const configuration = process.argv.includes('--release') ? 'Release' : 'Debug';
const preset = `native-${configuration.toLowerCase()}`;
function findCMake() {
  if (process.env.CMAKE) return process.env.CMAKE;
  const found = spawnSync(process.platform === 'win32' ? 'where.exe' : 'which', ['cmake'], {
    encoding: 'utf8',
    windowsHide: true,
  });
  if (found.status === 0) return found.stdout.trim().split(/\r?\n/)[0];
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
  throw new Error('Install CMake and the C++20 compiler, or set CMAKE.');
}
const cmake = findCMake();
const ctest = path.join(path.dirname(cmake), process.platform === 'win32' ? 'ctest.exe' : 'ctest');
const commands = [[cmake, ['--preset', 'native-msvc']]];
if (action !== 'configure') commands.push([cmake, ['--build', '--preset', preset, '--parallel']]);
if (action === 'test') commands.push([ctest, ['--preset', preset]]);
for (const [executable, args] of commands) {
  const result = spawnSync(executable, args, { cwd: root, stdio: 'inherit', windowsHide: true });
  if (result.error || result.status !== 0)
    throw result.error || new Error(`Native ${action} failed (${configuration})`);
}
