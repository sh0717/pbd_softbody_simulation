import { build, Platform, Arch } from 'electron-builder';
import { npmRun } from './npm-runner.mjs';
import { mkdir, copyFile, readFile, writeFile, stat, mkdtemp, readdir } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { createReadStream } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
import path from 'node:path';

const root = fileURLToPath(new URL('../', import.meta.url));
const require = createRequire(import.meta.url);
const config = require('../desktop/config.cjs');
const manifest = JSON.parse(await readFile(path.join(root, 'package.json'), 'utf8'));
if (process.platform !== 'win32' || process.arch !== 'x64')
  throw new Error('This release target requires Windows x64.');
for (const command of ['check:runtime', 'build:web']) {
  const result = npmRun(command, {
    cwd: root,
    stdio: 'inherit',
    windowsHide: true,
  });
  if (result.error || result.status !== 0) throw result.error || new Error(`${command} failed`);
}
await mkdir(path.join(root, 'build'), { recursive: true });
// A fresh allowlisted stage prevents old assets or development dependencies from leaking in.
const staging = await mkdtemp(path.join(root, 'build/desktop-app-'));
const release = path.join(root, 'release');
const assets = {};
const mime = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.wasm': 'application/wasm',
  '.txt': 'text/plain; charset=utf-8',
};
async function copy(source, target, asset = false) {
  const destination = path.join(staging, target);
  await mkdir(path.dirname(destination), { recursive: true });
  await copyFile(path.join(root, source), destination);
  if (asset) assets[`/${target}`] = mime[path.extname(target)] || 'application/octet-stream';
}
async function copyAssets(source, target) {
  const info = await stat(path.join(root, source));
  if (info.isDirectory()) {
    for (const entry of await readdir(path.join(root, source)))
      await copyAssets(`${source}/${entry}`, path.posix.join(target, entry));
  } else await copy(source, target, true);
}
for (const [source, target] of config.assets) await copyAssets(source, target);
for (const file of ['main.cjs', 'config.cjs', 'smoke.cjs', ...(config.supportFiles || [])])
  await copy(`desktop/${file}`, file);
for (const [source, target] of config.qaFiles || []) await copy(source, target);
if (config.icon) await copy(`desktop/${config.icon}`, config.icon);
await writeFile(path.join(staging, 'assets.json'), JSON.stringify(assets, null, 2));
await writeFile(
  path.join(staging, 'package.json'),
  JSON.stringify(
    {
      name: manifest.name,
      version: manifest.version,
      private: true,
      main: 'main.cjs',
      dependencies: {},
      description: config.productName,
      author: 'Simulation Lab project',
    },
    null,
    2,
  ),
);
const outputs = await build({
  targets: Platform.WINDOWS.createTarget(['portable'], Arch.x64),
  publish: 'never',
  projectDir: staging,
  config: {
    appId: config.appId,
    productName: config.productName,
    electronVersion: manifest.devDependencies.electron,
    electronDist: path.join(root, 'node_modules/electron/dist'),
    directories: { app: staging, output: release, buildResources: path.join(root, 'desktop') },
    files: ['**/*', '!node_modules{,/**/*}'],
    asar: true,
    npmRebuild: false,
    compression: 'normal',
    electronLanguages: ['en-US', 'ko'],
    artifactName: `${config.artifactStem}-\${version}-win-x64.exe`,
    win: {
      ...(config.icon ? { icon: path.join(root, 'desktop', config.icon) } : {}),
      signExecutable: false,
      requestedExecutionLevel: 'asInvoker',
    },
    portable: { requestExecutionLevel: 'user', unpackDirName: false },
  },
});
for (const file of outputs.filter((file) => file.endsWith('.exe'))) {
  const hash = createHash('sha256');
  for await (const chunk of createReadStream(file)) hash.update(chunk);
  const sha256 = hash.digest('hex');
  await writeFile(`${file}.sha256`, `${sha256}  ${path.basename(file)}\n`);
  console.log(JSON.stringify({ file, bytes: (await stat(file)).size, sha256 }, null, 2));
}
