import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const root = fileURLToPath(new URL('../', import.meta.url));
const check = spawnSync(process.execPath, [path.join(root, 'tools/check-runtime.mjs')], {
  cwd: root,
  stdio: 'inherit',
  windowsHide: true,
});
if (check.error || check.status !== 0)
  throw check.error || new Error('Runtime provenance check failed. Rebuild the C++ core.');
await import('./build-web.mjs');
