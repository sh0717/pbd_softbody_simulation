import { build } from 'esbuild';
import { mkdir, readFile, copyFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const output = path.join(root, 'dist');
const declared = JSON.parse(await readFile(path.join(root, 'package.json'), 'utf8'));
const three = JSON.parse(
  await readFile(path.join(root, 'node_modules/three/package.json'), 'utf8'),
);
if (three.version !== '0.139.2' || declared.dependencies.three !== three.version)
  throw new Error('PBD requires exactly Three.js 0.139.2. Run npm ci.');
for (const [source, target] of [
  ['src/styles/app.css', 'styles.css'],
  ['src/data/bunny-data.js', 'data/bunny-data.js'],
  ['src/simulation/physics.js', 'simulation/physics.js'],
  ['wasm/pbd_core.js', 'wasm/pbd_core.js'],
  ['wasm/pbd_core.wasm', 'wasm/pbd_core.wasm'],
  ['node_modules/three/build/three.min.js', 'vendor/three.min.js'],
  ['node_modules/three/examples/js/controls/OrbitControls.js', 'vendor/OrbitControls.js'],
  ['node_modules/three/LICENSE', 'THREE-LICENSE.txt'],
]) {
  await mkdir(path.dirname(path.join(output, target)), { recursive: true });
  await copyFile(path.join(root, source), path.join(output, target));
}
await build({
  entryPoints: [path.join(root, 'src/app.js')],
  outfile: path.join(output, 'app.js'),
  bundle: true,
  format: 'iife',
  platform: 'browser',
  target: 'chrome109',
  minify: true,
  define: {
    PBD_CORE_WASM_BASE64: JSON.stringify(
      (await readFile(path.join(root, 'wasm/pbd_core.wasm'))).toString('base64'),
    ),
  },
});
const source = await readFile(path.join(root, 'src/index.html'), 'utf8');
const html = source
  .replace('../dist/vendor/three.min.js', 'vendor/three.min.js')
  .replace('../dist/vendor/OrbitControls.js', 'vendor/OrbitControls.js')
  .replace('href="styles/app.css"', 'href="styles.css"')
  .replace('src="./data/bunny-data.js"', 'src="data/bunny-data.js"')
  .replace('src="../wasm/pbd_core.js"', 'src="wasm/pbd_core.js"')
  .replace('src="./simulation/physics.js"', 'src="simulation/physics.js"')
  .replace('<script type="module" src="./app.js"></script>', '<script defer src="app.js"></script>')
  .replace('<title>', '<link rel="icon" href="data:,"><title>');
if (/\b(?:src|href)=["']https?:/i.test(html))
  throw new Error('PBD distribution still requires a remote asset.');
await writeFile(path.join(output, 'index.html'), html);
const rootHtml = html.replace(
  /(<(?:script|link)\b[^>]*(?:src|href)=["'])(?!data:)([^"']+)(["'])/g,
  '$1dist/$2$3',
);
await writeFile(path.join(root, 'index.html'), rootHtml);
await writeFile(path.join(root, 'pbd_model.html'), rootHtml);
console.log('Built offline dist/ and compatible root entries using Three.js 0.139.2.');
