import { spawnSync } from 'node:child_process';
import { readdir } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const files = (await readdir(path.join(root, 'tests/unit')))
  .filter((name) => /\.test\.[cm]?js$/.test(name))
  .sort()
  .map((name) => `tests/unit/${name}`);
if (!files.length) throw new Error('No tests/unit/*.test.{js,mjs,cjs} files were found.');
const result = spawnSync(process.execPath, ['--test', ...files], {
  cwd: root,
  stdio: 'inherit',
  windowsHide: true,
});
if (result.error) throw result.error;
process.exitCode = result.status ?? 1;
