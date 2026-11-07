import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import path from 'node:path';

// Node 22 --run misreads this checkout's Korean path. Invoke npm's JS CLI
// directly, including when a VS Code process task has no npm_execpath.
export function npmRun(script, options) {
  const candidates = [
    process.env.npm_execpath,
    path.join(path.dirname(process.execPath), 'node_modules/npm/bin/npm-cli.js'),
  ];
  if (process.platform === 'win32') {
    const found = spawnSync('where.exe', ['npm.cmd'], { encoding: 'utf8', windowsHide: true });
    for (const command of (found.stdout || '').trim().split(/\r?\n/).filter(Boolean))
      candidates.push(path.join(path.dirname(command), 'node_modules/npm/bin/npm-cli.js'));
  }
  const npm = candidates.find(
    (file) => file && path.basename(file) === 'npm-cli.js' && existsSync(file),
  );
  if (!npm) throw new Error('npm CLI not found. Install Node.js 22.20.0 with npm.');
  return spawnSync(process.execPath, [npm, 'run', script], {
    windowsHide: true,
    stdio: 'inherit',
    ...options,
  });
}
