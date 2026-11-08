import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright';

const root = fileURLToPath(new URL('../../', import.meta.url));
const server = createServer(async (request, response) => {
  response.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
  response.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');

  try {
    const pathname = new URL(request.url, 'http://localhost').pathname;

    if (pathname === '/') {
      response.setHeader('Content-Type', 'text/html');
      response.end(
        '<!doctype html><link rel="icon" href="data:,"><script src="/wasm/pbd_core.js"></script><script src="/wasm/pbd_core_threaded.js"></script>',
      );

      return;
    }

    const file = path.resolve(root, `.${decodeURIComponent(pathname)}`);
    const relative = path.relative(root, file);

    if (relative.startsWith('..') || path.isAbsolute(relative)) throw new Error('Outside project');

    response.setHeader(
      'Content-Type',
      file.endsWith('.wasm') ? 'application/wasm' : 'text/javascript',
    );
    response.end(await readFile(file));
  } catch {
    response.writeHead(404).end();
  }
});
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));

let browser;
const errors = [];

try {
  browser = await chromium.launch({ channel: 'chrome', headless: true });

  const page = await browser.newPage();
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => {
    if (message.type() === 'error') errors.push(message.text());
  });
  await page.goto(`http://127.0.0.1:${server.address().port}/`);

  const result = await page.evaluate(async () => {
    if (!crossOriginIsolated) throw new Error('Missing isolation headers');

    const { runParallelScenario } = await import('/tests/browser/parallel-scenario.mjs');
    const serial = await createPBDModule();
    const parallel = await createPBDThreadedModule();
    const checks = [];
    const expectedWorkers = Math.max(0, Math.min(3, navigator.hardwareConcurrency - 1));

    for (const mode of ['pbd', 'xpbd']) {
      for (const count of [0, 1, 2, 5]) {
        const expected = runParallelScenario(serial, mode, count);
        const actual = runParallelScenario(parallel, mode, count);

        if (JSON.stringify(actual.snapshots) !== JSON.stringify(expected.snapshots))
          throw new Error(`Numeric mismatch: ${mode}/${count}`);

        if (actual.workers !== (count > 1 ? expectedWorkers : 0))
          throw new Error(`No workers: ${mode}/${count}/${actual.workers}`);

        checks.push({ mode, count, frames: actual.snapshots.length, workers: actual.workers });
        await new Promise((resolve) => setTimeout(resolve, 0));
      }
    }

    const worlds = [];
    let sharedMemory;

    try {
      const data = {
        verts: [0, 0, 0, 1, 0, 0],
        tetEdgeIds: [0, 1],
        tetIds: [],
        tetSurfaceTriIds: [],
      };

      for (let index = 0; index < 4; index++) {
        const world = parallel.createWorld({});

        worlds.push(world);

        const body = parallel.addSoft(world, data, [0, 0, 0], {});

        parallel.addSoft(world, data, [3, 0, 0], {});
        sharedMemory = parallel.bodyArray(body, 0).buffer instanceof SharedArrayBuffer;
        parallel.step(world, 0.01);
      }

      if (
        parallel.worldWorkerCount(worlds[0]) !== expectedWorkers ||
        parallel.worldWorkerCount(worlds[3]) !== 0
      )
        throw new Error('Worker pool exhaustion did not fall back safely');

      checks.push({ poolExhaustionFallback: true });
    } finally {
      for (const world of worlds.reverse()) parallel.destroyWorld(world);
    }

    // join 직후 같은 JS 호출 안에서 예약과 Worker를 다시 사용할 수 있어야 합니다.
    for (let iteration = 0; iteration < 10; iteration++) {
      const reused = runParallelScenario(parallel, 'xpbd', 2, 1);

      if (reused.workers !== expectedWorkers)
        throw new Error('Worker budget was not released after join');
    }

    checks.push({ immediateWorkerReuse: true });

    return { checks, crossOriginIsolated, sharedMemory };
  });

  assert.equal(result.sharedMemory, true);
  assert.deepEqual(errors, []);

  const report = { ...result, browser: browser.version(), errors };

  await mkdir(path.join(root, 'artifacts/threading-20261108'), { recursive: true });
  await writeFile(
    path.join(root, 'artifacts/threading-20261108/browser-parallel.json'),
    JSON.stringify(report, null, 2),
  );
  console.log(JSON.stringify(report, null, 2));
} finally {
  await browser?.close();
  await new Promise((resolve) => server.close(resolve));
}
