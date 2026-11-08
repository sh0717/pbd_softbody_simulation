import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';
import { runCleanup } from '../../src/lifecycle.mjs';

const sceneSource = await readFile(
  new URL('../../src/rendering/scene.js', import.meta.url),
  'utf8',
);
const lifecycleUrl = new URL('../../src/lifecycle.mjs', import.meta.url).href;
const { PhysicsScene } = await import(
  `data:text/javascript,${encodeURIComponent(sceneSource.replace("'../lifecycle.mjs'", JSON.stringify(lifecycleUrl)))}`
);
const appSource = await readFile(new URL('../../src/app.js', import.meta.url), 'utf8');

test('cleanup attempts every release and preserves an existing failure, even undefined', () => {
  const primary = new Error('creation failed');
  const calls = [];
  assert.throws(
    () =>
      runCleanup(
        [
          () => {
            calls.push('first');
            throw new Error('release failed');
          },
          () => calls.push('second'),
        ],
        primary,
      ),
    (error) => error === primary,
  );
  assert.deepEqual(calls, ['first', 'second']);
  let caught = false;
  try {
    runCleanup([() => calls.push('last')], undefined);
  } catch (error) {
    caught = true;
    assert.equal(error, undefined);
  }
  assert.equal(caught, true);
});

test('scene disposal continues through failing observers, mesh resources and renderer', () => {
  const calls = [];
  const failure = new Error('observer failed');
  const release = (name, error) => ({
    dispose() {
      calls.push(name);
      if (error) throw error;
    },
  });
  const scene = Object.create(PhysicsScene.prototype);
  scene.resizeObserver = {
    disconnect() {
      calls.push('observer');
      throw failure;
    },
  };
  scene.controls = release('controls');
  scene.scene = {
    traverse(visit) {
      visit({
        geometry: release('geometry-a', new Error('geometry failed')),
        material: [release('material-a', new Error('material failed')), release('material-b')],
      });
      visit({ geometry: release('geometry-b'), shadow: { map: release('shadow') } });
    },
  };
  scene.bodyViews = new Map([[1, {}]]);
  scene.obstacleViews = new Map([[1, {}]]);
  scene.renderer = {
    ...release('renderer', new Error('renderer failed')),
    domElement: { remove: () => calls.push('canvas') },
  };
  assert.throws(
    () => scene.dispose(),
    (error) => error === failure,
  );
  assert.deepEqual(calls, [
    'observer',
    'controls',
    'geometry-a',
    'material-a',
    'material-b',
    'geometry-b',
    'shadow',
    'renderer',
    'canvas',
  ]);
  assert.equal(scene.bodyViews.size, 0);
  assert.equal(scene.obstacleViews.size, 0);
  scene.dispose();
  assert.equal(calls.length, 9, 'repeated disposal does not repeat failed resource releases');
});

test('partial scene creation preserves its cause and still removes the canvas', () => {
  const original = new Error('pixel ratio setup failed');
  const calls = [];
  const previousWindow = globalThis.window;
  globalThis.window = { devicePixelRatio: 1 };
  try {
    const THREE = {
      Scene: class {
        traverse() {}
      },
      Color: class {},
      Fog: class {},
      WebGLRenderer: class {
        constructor() {
          this.domElement = { remove: () => calls.push('canvas') };
        }
        setPixelRatio() {
          throw original;
        }
        dispose() {
          calls.push('renderer');
          throw new Error('renderer cleanup failed');
        }
      },
    };
    assert.throws(
      () => new PhysicsScene({ THREE }),
      (error) => error === original,
    );
    assert.deepEqual(calls, ['renderer', 'canvas']);
  } finally {
    if (previousWindow === undefined) delete globalThis.window;
    else globalThis.window = previousWindow;
  }
});

test('box obstacle removal finishes all resource releases', () => {
  const failure = new Error('geometry dispose failed');
  const calls = [];
  const obstacle = { shape: 'box' };
  const mesh = {
    geometry: {
      dispose() {
        calls.push('geometry');
        throw failure;
      },
    },
    material: { dispose: () => calls.push('material') },
  };
  const scene = Object.create(PhysicsScene.prototype);
  scene.scene = { remove: () => calls.push('remove') };
  scene.obstacleViews = new Map([[obstacle, mesh]]);
  assert.throws(
    () => scene.removeObstacle(obstacle),
    (error) => error === failure,
  );
  assert.deepEqual(calls, ['remove', 'geometry', 'material']);
  assert.equal(scene.obstacleViews.size, 0);
});

async function appFixture({
  pickingFailure,
  viewFailure,
  startupFailure,
  frameFailure,
  commandFailure,
} = {}) {
  const calls = [];
  const errors = [];
  const nodes = new Map();
  const node = (id) => {
    if (!nodes.has(id)) nodes.set(id, { hidden: true, addEventListener() {} });
    return nodes.get(id);
  };
  let world;
  let frame;
  const window = {
    THREE: {},
    PBD_BUNNY_DATA: { verts: [0, 0, 0] },
    addEventListener() {},
    PBDPhysics: {
      async init() {},
      PhysicsWorld: class {
        constructor(options) {
          Object.assign(this, options);
          world = this;
        }
        step() {
          if (this.disposed) throw new Error('world disposed');
          if (frameFailure) throw frameFailure;
        }
        dispose() {
          calls.push('world');
          this.disposed = true;
        }
      },
    },
  };
  const sandbox = {
    window,
    document: { getElementById: node, addEventListener() {} },
    console: { error: (error) => errors.push(error) },
    performance: { now: () => 0 },
    requestAnimationFrame(callback) {
      frame = callback;
      return 1;
    },
    cancelAnimationFrame: () => calls.push('frame'),
    AbortController: class {
      signal = {};
      abort() {
        calls.push('events');
      }
    },
    runCleanup,
    PhysicsScene: class {
      renderer = { domElement: { addEventListener() {} } };
      dispose() {
        calls.push('view');
        if (viewFailure) throw viewFailure;
      }
      render() {}
    },
    ControlPanel: class {
      constructor() {
        if (startupFailure) throw startupFailure;
      }
      updateMaterial() {}
      updateStatus() {}
      dispose() {
        calls.push('ui');
      }
    },
    PickingController: class {
      dispose() {
        calls.push('picking');
        if (pickingFailure) throw pickingFailure;
      }
    },
    ExperimentController: class {
      running = false;
      ballCount = 0;
      constructor({ onFatal }) {
        this.onFatal = onFatal;
      }
      addObstacle() {}
      addSoft() {}
      addBall() {
        if (++this.ballCount > 1 && commandFailure) {
          this.onFatal(commandFailure);
          throw commandFailure;
        }
      }
      selectObstacle() {}
      setRunning(value) {
        this.running = value;
      }
      dispose() {
        calls.push('experiment');
      }
    },
  };
  // Replace only module imports and the top-level startup promise. The actual
  // app construction, frame callback, fail handler and disposal run unchanged.
  const executable = appSource
    .replace(/^import .*;\r?$/gm, '')
    .replace(/start\(\)\.catch\(fail\);\s*$/, 'globalThis.startup = start().catch(fail);');
  vm.runInNewContext(executable, sandbox, { filename: 'app.js' });
  await sandbox.startup;
  return { calls, errors, nodes, world, app: window.softLab, frame: (time) => frame(time) };
}

test('actual app disposal releases its world after an earlier component throws', async () => {
  const failure = new Error('picking cleanup failed');
  const f = await appFixture({ pickingFailure: failure });
  assert.throws(
    () => f.app.dispose(),
    (error) => error === failure,
  );
  assert.deepEqual(f.calls, ['frame', 'events', 'picking', 'ui', 'experiment', 'view', 'world']);
  assert.equal(f.world.disposed, true);
  f.app.dispose();
  assert.equal(f.calls.length, 7);
});

test('actual app startup failure retains its cause when cleanup also fails', async () => {
  const original = new Error('startup failed');
  const cleanupFailure = new Error('view cleanup failed');
  const f = await appFixture({ startupFailure: original, viewFailure: cleanupFailure });
  assert.equal(f.world.disposed, true);
  assert.ok(f.nodes.get('error-banner').textContent.includes(original.message));
  assert.equal(f.errors.at(-1), original);
  assert.ok(f.errors.includes(cleanupFailure));
});

test('actual app frame failure retains its cause and releases the native owner', async () => {
  const original = new Error('step failed');
  const cleanupFailure = new Error('view cleanup failed');
  const f = await appFixture({ frameFailure: original, viewFailure: cleanupFailure });
  f.app.setRunning(true);
  f.frame(0);
  f.frame(20);
  assert.equal(f.world.disposed, true);
  assert.ok(f.nodes.get('error-banner').textContent.includes(original.message));
  assert.equal(f.errors.at(-1), original);
  assert.ok(f.errors.includes(cleanupFailure));
});

test('fatal command failure reports the error, stops the frame and disposes the world', async () => {
  const failure = new Error('view creation failed');
  const f = await appFixture({ commandFailure: failure });
  assert.throws(
    () => f.app.addBall(),
    (error) => error === failure,
  );
  assert.equal(f.world.disposed, true);
  assert.deepEqual(f.calls, ['frame', 'events', 'picking', 'ui', 'experiment', 'view', 'world']);
  assert.ok(f.nodes.get('error-banner').textContent.includes(failure.message));
  f.frame(20);
  assert.equal(f.errors.at(-1), failure);
  assert.equal(f.calls.length, 7);
});
