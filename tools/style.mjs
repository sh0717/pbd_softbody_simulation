// The same formatter entry point is kept in each independently runnable project.
import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { readdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import * as prettier from 'prettier';

const root = fileURLToPath(new URL('../', import.meta.url));
const write = process.argv.includes('--write');
const roots = [
  'src',
  'tools',
  'tests/unit',
  'tests/browser',
  'tests/desktop',
  'desktop',
  '.vscode',
  '.github',
];
const ignored = new Set(['generated', 'data', 'fixtures', '__pycache__']);

async function collect(directory, extensions) {
  if (!existsSync(directory)) return [];
  const files = [];
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const file = path.join(directory, entry.name);
    if (entry.isDirectory() && !ignored.has(entry.name)) {
      files.push(...(await collect(file, extensions)));
    } else if (entry.isFile() && extensions.has(path.extname(file))) {
      files.push(file);
    }
  }
  return files.sort();
}

function findClangFormat() {
  if (process.env.CLANG_FORMAT) return process.env.CLANG_FORMAT;
  const command = spawnSync(
    process.platform === 'win32' ? 'where.exe' : 'which',
    ['clang-format'],
    { encoding: 'utf8', windowsHide: true },
  );
  if (command.status === 0) return command.stdout.trim().split(/\r?\n/)[0];
  if (process.platform === 'win32') {
    const locator = path.join(
      process.env['ProgramFiles(x86)'] || 'C:/Program Files (x86)',
      'Microsoft Visual Studio/Installer/vswhere.exe',
    );
    const found = spawnSync(
      locator,
      ['-latest', '-products', '*', '-property', 'installationPath'],
      { encoding: 'utf8', windowsHide: true },
    );
    const candidate = path.join(
      found.stdout?.trim() || '',
      'VC/Tools/Llvm/x64/bin/clang-format.exe',
    );
    if (existsSync(candidate)) return candidate;
  }
  throw new Error('Install clang-format or set CLANG_FORMAT to its executable path.');
}

const jsFiles = (
  await Promise.all(
    roots.map((directory) =>
      collect(
        path.join(root, directory),
        new Set(['.js', '.mjs', '.cjs', '.json', '.yml', '.yaml']),
      ),
    ),
  )
).flat();
for (const name of ['package.json', '.prettierrc.json', 'CMakePresets.json']) {
  if (existsSync(path.join(root, name))) jsFiles.push(path.join(root, name));
}
let failures = 0;
for (const file of jsFiles) {
  const source = await readFile(file, 'utf8');
  const options = { ...(await prettier.resolveConfig(file)), filepath: file };
  if (write) {
    const formatted = await prettier.format(source, options);
    if (formatted !== source) await writeFile(file, formatted);
  } else if (!(await prettier.check(source, options))) {
    console.error(path.relative(root, file));
    failures++;
  }
}

const cppFiles = (
  await Promise.all(
    ['native', 'tests/native'].map((directory) =>
      collect(path.join(root, directory), new Set(['.cpp', '.h'])),
    ),
  )
).flat();
const formatter = findClangFormat();
const version = spawnSync(formatter, ['--version'], { encoding: 'utf8', windowsHide: true });
if (version.error || version.status !== 0) throw version.error || new Error(version.stderr);
const result = spawnSync(
  formatter,
  [write ? '-i' : '--dry-run', ...(write ? [] : ['--Werror']), ...cppFiles],
  {
    cwd: root,
    stdio: 'inherit',
    windowsHide: true,
  },
);
if (result.error) throw result.error;
if (result.status !== 0) failures++;
console.log(
  `${write ? 'Formatted' : 'Checked'} ${jsFiles.length} JS/config and ${cppFiles.length} C++ files; ${version.stdout.trim()}.`,
);
process.exitCode = failures ? 1 : 0;
