import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const result = spawnSync(process.env.PYTHON || 'python', ['-X', 'utf8', 'tools/build_wasm.py'], {
  cwd: root,
  stdio: 'inherit',
  windowsHide: true,
});
if (result.error || result.status !== 0) throw result.error || new Error('Wasm core build failed.');
