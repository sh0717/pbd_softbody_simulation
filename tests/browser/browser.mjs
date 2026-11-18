import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright';

const root = fileURLToPath(new URL('../../', import.meta.url));
const artifacts = path.join(root, 'artifacts');
const port = Number(process.env.PBD_TEST_PORT ?? 4174);

assert.ok(
  Number.isInteger(port) && port > 0 && port <= 65535,
  'PBD_TEST_PORT must be a valid port',
);

const url = `http://127.0.0.1:${port}/pbd_model.html`;
// CI에서는 GPU 드라이버 대신 Chromium의 CPU 렌더러를 명시적으로 사용합니다.
const launchArgs =
  process.env.PBD_TEST_SOFTWARE_WEBGL === '1' ? ['--use-gl=angle', '--use-angle=swiftshader'] : [];

// serve.py verifies project identity before reusing an existing listener.
// Keep shell disabled so this process owns the Python server it starts.
const server = spawn(process.env.PYTHON ?? 'python', ['tools/serve.py', '--port', String(port)], {
  cwd: root,
  windowsHide: true,
  stdio: ['ignore', 'pipe', 'pipe'],
});
let serverOutput = '';
let browser;
let page;
const errors = [];
let expectedConsoleError;
const expectedErrors = [];
const failedRequests = [];
const failedResponses = [];

function observePage(target) {
  target.on('pageerror', (error) => errors.push(error.message));
  target.on('console', (message) => {
    if (message.type() === 'error' && !message.location().url.endsWith('/favicon.ico')) {
      if (expectedConsoleError && message.text().includes(expectedConsoleError))
        expectedErrors.push(message.text());
      else errors.push(message.text());
    }
  });
  target.on('requestfailed', (request) => {
    failedRequests.push({ url: request.url(), error: request.failure()?.errorText });
  });
  target.on('response', (response) => {
    if (response.status() >= 400 && !response.url().endsWith('/favicon.ico'))
      failedResponses.push({ url: response.url(), status: response.status() });
  });
}

async function checkViewport(target) {
  await target.waitForFunction(() => Boolean(window.softLab), undefined, { timeout: 60000 });

  const layout = await target.evaluate(() => {
    const canvas = document.querySelector('#viewport canvas').getBoundingClientRect();
    const run = document.getElementById('run-button').getBoundingClientRect();

    return {
      width: document.documentElement.clientWidth,
      height: innerHeight,
      pageWidth: document.documentElement.scrollWidth,
      canvasWidth: Math.round(canvas.width),
      canvasHeight: Math.round(canvas.height),
      canvasTop: Math.round(canvas.top),
      playbackBottom: Math.round(run.bottom),
    };
  });

  assert.ok(
    layout.pageWidth <= layout.width,
    'the workspace must fit without horizontal scrolling',
  );
  assert.ok(
    layout.canvasWidth >= 280 && layout.canvasHeight >= 240,
    'the 3D stage must remain usable',
  );
  assert.ok(layout.canvasTop < layout.height / 3, 'the 3D stage must appear near the top');
  assert.ok(layout.playbackBottom <= layout.height, 'playback must remain reachable on first view');

  return layout;
}

async function checkPointerDrag(target) {
  const origin = await target.evaluate(() => {
    const { camera, renderer, world } = window.softLab;
    const position = Array.from(world.balls[0].center);
    const point = new window.THREE.Vector3().fromArray(position).project(camera);
    const canvas = renderer.domElement.getBoundingClientRect();

    return {
      x: canvas.left + ((point.x + 1) / 2) * canvas.width,
      y: canvas.top + ((1 - point.y) / 2) * canvas.height,
      position,
    };
  });

  const canvas = target.locator('#viewport canvas');
  await target.mouse.move(origin.x, origin.y);
  await target.mouse.down();

  try {
    assert.equal(await canvas.getAttribute('data-dragging'), '');
    await canvas.dispatchEvent('pointercancel', { pointerId: 999, pointerType: 'touch' });
    assert.equal(
      await canvas.getAttribute('data-dragging'),
      '',
      'a second pointer must not cancel the drag',
    );
    await target.mouse.move(origin.x + 40, origin.y + 12);

    const position = await target.evaluate(() => Array.from(window.softLab.world.balls[0].center));
    assert.notDeepEqual(position, origin.position, 'the primary pointer must still move the body');
  } finally {
    await target.mouse.up();
  }

  assert.equal(await canvas.getAttribute('data-dragging'), null);
  assert.ok(
    await target.evaluate(() => window.softLab.controls.enabled),
    'release restores orbit controls',
  );
}

async function readPageState() {
  if (!page || page.isClosed()) return null;

  return page
    .evaluate(() => {
      const gl = window.softLab?.renderer?.getContext();
      const debugInfo = gl?.getExtension('WEBGL_debug_renderer_info');
      return {
        ready: Boolean(window.softLab),
        errorBanner: document.getElementById('error-banner')?.textContent,
        canvasCount: document.querySelectorAll('#viewport canvas').length,
        webgl: gl
          ? {
              contextLost: gl.isContextLost(),
              version: gl.getParameter(gl.VERSION),
              renderer: debugInfo ? gl.getParameter(debugInfo.UNMASKED_RENDERER_WEBGL) : null,
            }
          : null,
      };
    })
    .catch(() => null);
}

try {
  await new Promise((resolve, reject) => {
    const timeout = setTimeout(
      () => reject(new Error(`Server readiness timed out: ${serverOutput}`)),
      30000,
    );
    const finish = (error) => {
      clearTimeout(timeout);
      server.off('error', onError);
      server.off('exit', onExit);
      error ? reject(error) : resolve();
    };
    const onError = (error) => finish(error);
    const onExit = (code) =>
      finish(new Error(`Server exited before readiness (${code}): ${serverOutput}`));

    server.on('error', onError);
    server.on('exit', onExit);
    server.stderr.on('data', (chunk) => {
      serverOutput += chunk.toString();
    });
    server.stdout.on('data', (chunk) => {
      serverOutput += chunk.toString();

      if (serverOutput.includes('PBD_SERVER_READY')) finish();
    });
  });

  await mkdir(artifacts, { recursive: true });
  browser = await chromium.launch({ channel: 'chrome', headless: true, args: launchArgs });
  page = await browser.newPage({
    viewport: { width: 1440, height: 900 },
    deviceScaleFactor: 1,
  });
  observePage(page);

  await page.goto(url, { waitUntil: 'networkidle' });
  await page.waitForFunction(
    () => Boolean(window.softLab) || document.getElementById('error-banner')?.hidden === false,
    undefined,
    { timeout: 60000 },
  );

  const initialState = await readPageState();

  assert.equal(
    await page.locator('#error-banner').isHidden(),
    true,
    `initialization must succeed: ${JSON.stringify({ initialState, errors })}`,
  );

  const viewports = { desktop: await checkViewport(page) };
  await page.screenshot({ path: path.join(artifacts, 'browser-desktop.png') });
  await page.locator('#obstacle-list').selectOption({ index: 1 });
  await page.locator('#obstacle-properties').waitFor({ state: 'visible' });
  await page.locator('#remove-obstacle').scrollIntoViewIfNeeded();
  await page.screenshot({ path: path.join(artifacts, 'browser-inspector.png') });
  await page.locator('#obstacle-list').selectOption({ index: 0 });
  await checkPointerDrag(page);
  await page.locator('#view-expand').click();
  await page.waitForFunction(() => !!document.fullscreenElement);
  await page.keyboard.press('f');
  await page.waitForFunction(() => !document.fullscreenElement);
  await page.reload({ waitUntil: 'networkidle' });
  await page.waitForFunction(() => Boolean(window.softLab), undefined, { timeout: 60000 });

  for (const [name, width, height] of [
    ['tablet', 834, 1112],
    ['mobile', 390, 844],
  ]) {
    const responsive = await browser.newPage({
      viewport: { width, height },
      deviceScaleFactor: 1,
      isMobile: true,
      hasTouch: true,
    });

    try {
      observePage(responsive);
      await responsive.goto(url, { waitUntil: 'networkidle' });
      viewports[name] = await checkViewport(responsive);
      await responsive.screenshot({ path: path.join(artifacts, `browser-${name}.png`) });
      await responsive.locator('#mode-pbd').tap();
      assert.equal(await responsive.locator('#mode-pbd').getAttribute('aria-pressed'), 'true');
    } finally {
      await responsive.close();
    }
  }

  await page.addScriptTag({
    content: await readFile(path.join(root, 'tests/browser/checks.js'), 'utf8'),
  });

  const checks = await page.evaluate(() => window.runSoftLabChecks());

  assert.equal(checks.passed, 32, 'run the complete UI regression suite');

  const disposed = await page.evaluate(() => {
    const app = window.softLab;
    const body = app.world.bodies[0];

    app.dispose();
    app.dispose();

    let expiredBody = false;

    try {
      body.positions;
    } catch (error) {
      expiredBody = /disposed/i.test(error.message);
    }

    const running = app.running;

    document.getElementById('run-button').click();

    return {
      canvasCount: document.querySelectorAll('#viewport canvas').length,
      expiredBody,
      eventsRemoved: app.running === running,
    };
  });

  assert.deepEqual(disposed, { canvasCount: 0, expiredBody: true, eventsRemoved: true });

  await page.reload({ waitUntil: 'networkidle' });
  await page.waitForFunction(() => Boolean(window.softLab), undefined, { timeout: 60000 });
  expectedConsoleError = 'PBD_TEST_INJECTED_STEP_FAILURE';
  await page.evaluate((message) => {
    const app = window.softLab;

    window.expiredBodyForTest = app.world.bodies[0];
    app.world.step = () => {
      throw new Error(message);
    };
    app.setRunning(true);
  }, expectedConsoleError);
  await page.locator('#error-banner').waitFor({ state: 'visible' });
  await page.waitForFunction(() => document.querySelectorAll('#viewport canvas').length === 0);

  assert.equal(
    await page.evaluate(() => {
      try {
        window.expiredBodyForTest.positions;

        return false;
      } catch (error) {
        return /disposed/i.test(error.message);
      }
    }),
    true,
    'frame failure must invalidate native body owners',
  );
  assert.equal(
    expectedErrors.length,
    1,
    'injected frame failure must reach the controlled error path once',
  );

  expectedConsoleError = 'PBD_TEST_INJECTED_INITIALIZATION_FAILURE';
  await page.addInitScript((message) => {
    let physics;
    Object.defineProperty(window, 'PBDPhysics', {
      configurable: true,
      get: () => physics,

      set(api) {
        physics = api;

        const World = api.PhysicsWorld;

        api.PhysicsWorld = class extends World {
          constructor(...args) {
            super(...args);
            window.startupWorldForTest = this;
          }
        };
      },
    });

    const getElementById = Document.prototype.getElementById;

    Document.prototype.getElementById = function (id) {
      if (id === 'run-button' && window.startupWorldForTest) throw new Error(message);

      return getElementById.call(this, id);
    };
  }, expectedConsoleError);
  await page.reload({ waitUntil: 'networkidle' });
  await page.locator('#error-banner').waitFor({ state: 'visible' });

  assert.equal(
    await page.locator('#viewport canvas').count(),
    0,
    'partial initialization must remove its renderer',
  );
  assert.equal(
    await page.evaluate(() => {
      try {
        window.startupWorldForTest.step();

        return false;
      } catch (error) {
        return /disposed/i.test(error.message);
      }
    }),
    true,
    'partial initialization must dispose its native world',
  );
  assert.equal(
    expectedErrors.length,
    2,
    'initialization failure must reach the controlled error path once',
  );
  assert.deepEqual(errors, [], 'browser console and page errors');
  assert.deepEqual(failedRequests, [], 'all application resources must load');
  assert.deepEqual(failedResponses, [], 'all application resources must return success');

  const result = {
    ...checks,
    browserVersion: browser.version(),
    launchArgs,
    initialState,
    viewports,
    interactionChecks: [
      'Object drag survives an unrelated pointer and restores the orbit controls',
      'Fullscreen can be entered from the toolbar and exited with F',
    ],
    url,
    errors,
    failedRequests,
    failedResponses,
    lifecycleChecks: [
      'repeatable disposal removes canvas, native owners and UI listeners',
      'frame failure disposes resources and reports the error once',
      'partial initialization failure disposes renderer and native world',
    ],
  };

  await writeFile(
    path.join(artifacts, 'browser-results.json'),
    `${JSON.stringify(result, null, 2)}\n`,
  );
  console.log(JSON.stringify(result, null, 2));
} catch (error) {
  const diagnostics = {
    error: error.stack || String(error),
    browserVersion: browser?.version(),
    launchArgs,
    url,
    pageState: await readPageState(),
    errors,
    failedRequests,
    failedResponses,
    serverOutput,
  };

  console.error(JSON.stringify(diagnostics, null, 2));

  try {
    await mkdir(artifacts, { recursive: true });
    await writeFile(
      path.join(artifacts, 'browser-failure.json'),
      `${JSON.stringify(diagnostics, null, 2)}\n`,
    );

    if (page && !page.isClosed())
      await page.screenshot({ path: path.join(artifacts, 'browser-failure.png'), timeout: 5000 });
  } catch (diagnosticError) {
    console.error(`Browser diagnostics could not be saved: ${diagnosticError.message}`);
  }

  throw error;
} finally {
  try {
    await browser?.close();
  } finally {
    if (server.pid && server.exitCode === null && server.signalCode === null) {
      const exited = once(server, 'exit');

      server.kill();
      await exited;
    }
  }
}
