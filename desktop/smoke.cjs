const { app } = require('electron');
const assert = require('node:assert/strict');
const { readFile, writeFile } = require('node:fs/promises');
const path = require('node:path');

exports.runSmokeTest = async (window, output, diagnostics) => {
  const report = {
    passed: false,
    packaged: app.isPackaged,
    versions: process.versions,
    checks: [],
    ...diagnostics,
  };
  const evaluate = (fn) => window.webContents.executeJavaScript(`(${fn.toString()})()`);

  try {
    assert.equal(app.isPackaged, true);

    const deadline = Date.now() + 60000;

    while (!(await evaluate(() => Boolean(window.softLab)))) {
      if (Date.now() > deadline) throw new Error('PBD initialization timed out');

      await new Promise((resolve) => setTimeout(resolve, 100));
    }

    const initial = await evaluate(() => ({
      require: typeof require,
      process: typeof process,
      origin: location.origin,
      backend: window.softLab.world.backend,
      revision: window.THREE.REVISION,
      canvas: document.querySelectorAll('#viewport canvas').length,
      error: !document.getElementById('error-banner').hidden,
    }));

    assert.equal(initial.require, 'undefined');
    assert.equal(initial.process, 'undefined');
    assert.equal(initial.origin, 'pbd://lab');
    assert.equal(initial.revision, '139');
    assert.equal(initial.canvas, 1);
    assert.equal(initial.error, false);
    assert.match(initial.backend, /WebAssembly/);

    report.initial = initial;
    report.checks.push('Packaged offline assets, WebGL canvas and isolated renderer');
    await writeFile(
      path.join(output, 'desktop.png'),
      (await window.webContents.capturePage()).toPNG(),
    );
    await window.webContents.executeJavaScript(
      await readFile(path.join(__dirname, 'checks.js'), 'utf8'),
    );
    report.ui = await evaluate(() => window.runSoftLabChecks());

    assert.equal(report.ui.passed, 26);

    report.checks.push(...report.ui.checks);
    report.parallel = await evaluate(() => {
      const world = window.softLab.world;

      window.softLab.setRunning(false);

      const data = {
        verts: [0, 0, 0, 1, 0, 0],
        tetEdgeIds: [0, 1],
        tetIds: [],
        tetSurfaceTriIds: [],
      };

      while (world.softBodies.length < 2) world.addSoftBody(data, [3, 3, 3]);

      world.step();

      return {
        isolated: crossOriginIsolated,
        workers: world.constraintWorkerCount,
        expected: Math.max(0, Math.min(3, navigator.hardwareConcurrency - 1)),
      };
    });

    assert.equal(report.parallel.isolated, true);
    assert.equal(report.parallel.workers, report.parallel.expected);

    report.checks.push('Packaged std::thread runtime executes body constraints');

    const disposed = await evaluate(() => {
      window.softLab.dispose();
      window.softLab.dispose();

      return document.querySelectorAll('#viewport canvas').length;
    });

    assert.equal(disposed, 0);

    report.checks.push('Repeated disposal removes the WebGL canvas');

    assert.deepEqual(diagnostics.errors, []);
    assert.deepEqual(diagnostics.blockedRequests, []);

    report.gpu = app.getGPUFeatureStatus();
    report.passed = true;
  } catch (error) {
    report.error = error.stack;
  }

  await writeFile(path.join(output, 'result.json'), JSON.stringify(report, null, 2));

  return report.passed;
};
