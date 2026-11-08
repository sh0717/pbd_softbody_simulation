import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { createRequire } from 'node:module';
import path from 'node:path';

const root = fileURLToPath(new URL('../../', import.meta.url));
const pkg = JSON.parse(await readFile(path.join(root, 'package.json'), 'utf8'));
const config = createRequire(import.meta.url)('../../desktop/config.cjs');
const { chromium } = await import(
  pkg.devDependencies.playwright ? 'playwright' : '@playwright/test'
);
const types = {
  '.html': 'text/html',
  '.css': 'text/css',
  '.js': 'text/javascript',
  '.mjs': 'text/javascript',
  '.wasm': 'application/wasm',
  '.json': 'application/json',
};
const server = createServer(async (request, response) => {
  try {
    const pathname = decodeURIComponent(new URL(request.url, 'http://localhost').pathname);
    const file = path.resolve(root, `.${pathname}`);
    const relative = path.relative(root, file);
    if (relative.startsWith('..') || path.isAbsolute(relative)) throw new Error('Outside project');
    const body = await readFile(file);
    response.writeHead(200, {
      'Content-Type': types[path.extname(file)] || 'application/octet-stream',
      'Content-Security-Policy': config.csp,
      'Cache-Control': 'no-store',
    });
    response.end(body);
  } catch {
    response.writeHead(404).end('Not found');
  }
});
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
const base = `http://127.0.0.1:${server.address().port}`;
const errors = [],
  external = [],
  failedRequests = [],
  wasmRequests = [],
  checks = [];
let browser;
// CI의 WebGL 검사는 하드웨어 GPU 없이도 같은 렌더링 경로를 실행합니다.
const launchArgs =
  process.env.PBD_TEST_SOFTWARE_WEBGL === '1' ? ['--use-gl=angle', '--use-angle=swiftshader'] : [];
try {
  browser = await chromium.launch({ channel: 'chrome', headless: true, args: launchArgs });
  const context = await browser.newContext({ viewport: { width: 1440, height: 1000 } });
  await context.route('**/*', async (route) => {
    const url = route.request().url();
    if (/^https?:/.test(url) && !url.startsWith(`${base}/`)) {
      external.push(url);
      await route.abort();
    } else await route.continue();
  });
  const entries = [
    'dist/index.html',
    ...(config.scheme === 'pbd'
      ? ['index.html', 'pbd_model.html']
      : config.scheme === 'boids'
        ? ['index.html', 'yoosh.html']
        : ['SPH-Fluid-Lab.html']),
  ];
  const targets = [];
  for (const entry of entries) {
    targets.push({ entry: `http: ${entry}`, url: `${base}/${entry}` });
    targets.push({ entry: `file: ${entry}`, url: pathToFileURL(path.join(root, entry)).href });
  }
  for (const { entry, url } of targets) {
    const page = await context.newPage();
    page.on('pageerror', (error) => errors.push(error.message));
    page.on('console', (message) => {
      if (message.type() === 'error') errors.push(message.text());
    });
    page.on('requestfailed', (request) => {
      failedRequests.push({ entry, url: request.url(), error: request.failure()?.errorText });
    });
    page.on('request', (request) => {
      if (new URL(request.url()).pathname.endsWith('.wasm')) wasmRequests.push(request.url());
    });
    await page.goto(url);
    await page.waitForFunction(
      (scheme) =>
        scheme === 'pbd'
          ? Boolean(window.softLab) || document.getElementById('error-banner')?.hidden === false
          : scheme === 'sph'
            ? window.sphApp?.getState().ready
            : document.getElementById('viewport')?.dataset.ready === 'true',
      config.scheme,
      { timeout: 60000 },
    );
    if (config.scheme === 'pbd') {
      assert.equal(
        await page.locator('#error-banner').isHidden(),
        true,
        `${entry}: initialization`,
      );
    }
    assert.ok((await page.locator('canvas').count()) > 0, `${entry}: WebGL canvas`);
    if (config.scheme === 'pbd') {
      await page.evaluate(() => {
        window.softLab.setRunning(false);
        window.softLab.world.step();
      });
      assert.ok(
        await page.evaluate(() =>
          window.softLab.world.bodies.every((body) =>
            Array.from(body.positions).every(Number.isFinite),
          ),
        ),
      );
    } else if (config.scheme === 'sph') {
      const before = await page.evaluate(() => window.sphApp.getState().snapshot.step_index);
      await page.evaluate(() => window.sphApp.advanceSteps(1));
      assert.equal(
        await page.evaluate(() => window.sphApp.getState().snapshot.step_index),
        before + 1,
      );
    } else assert.equal(await page.locator('#viewport').getAttribute('data-backend'), 'cpp17-wasm');
    const environment = url.startsWith('file:') ? 'browser file origin' : 'desktop CSP';
    checks.push(`${entry}: initialized and exercised under ${environment}`);
    await page.close();
  }
  assert.deepEqual(errors, []);
  assert.deepEqual(external, []);
  assert.deepEqual(failedRequests, []);
  assert.deepEqual(wasmRequests, [], 'the embedded core must initialize without a Wasm request');
  await mkdir(path.join(root, 'artifacts'), { recursive: true });
  await writeFile(
    path.join(root, 'artifacts/distribution-results.json'),
    JSON.stringify(
      { passed: true, checks, errors, external, failedRequests, wasmRequests },
      null,
      2,
    ) + '\n',
  );
  console.log(
    `PASS: ${checks.length} HTTP/file distribution entries; HTTP entries use desktop CSP`,
  );
} finally {
  await browser?.close();
  await new Promise((resolve) => server.close(resolve));
}
