import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createRequire } from 'node:module';
import vm from 'node:vm';
import test from 'node:test';

const require = createRequire(import.meta.url);
const native = await require('../../wasm/pbd_core.js')();
const source = await readFile(new URL('../../src/simulation/physics.js', import.meta.url), 'utf8');

// Exercise the real adapter and Wasm. Only the native entry points are counted;
// no production counters or alternate validation implementation are required.
async function fixture(options = {}) {
  const calls = { configure: 0, setObstacles: 0 };
  const overrides = new Map();
  const observed = new Proxy(native, {
    get(target, name) {
      if (overrides.has(name)) return overrides.get(name);
      if (Object.hasOwn(calls, name)) return counted[name];
      return target[name];
    },
  });
  const counted = Object.fromEntries(
    Object.keys(calls).map((name) => [
      name,
      (...args) => {
        calls[name]++;
        return native[name](...args);
      },
    ]),
  );
  const sandbox = {
    module: { exports: {} },
    require: () => async () => observed,
    FinalizationRegistry,
    Error,
    TypeError,
    RangeError,
  };
  vm.runInNewContext(source, sandbox, { filename: 'physics.js' });
  const api = await sandbox.module.exports.init();
  const world = new api.PhysicsWorld(options);
  return {
    world,
    calls,
    overrides,
    counted,
    resetCounts() {
      calls.configure = 0;
      calls.setObstacles = 0;
    },
  };
}

test('unchanged steps and explicit no-op commands avoid native input reconstruction', async () => {
  const f = await fixture();
  try {
    const obstacle = f.world.addObstacle({ bumper: false });
    f.world.addBall(0.5, [5, 5, 5]);
    f.resetCounts();
    for (let frame = 0; frame < 10; frame++) f.world.step();
    f.world.step(0);
    f.world.settleObstacleContacts();
    f.world.configure({});
    f.world.updateObstacle(obstacle, { radius: obstacle.radius });
    assert.deepEqual(f.calls, { configure: 0, setObstacles: 0 });
  } finally {
    f.world.dispose();
  }
});

test('mutable settings and obstacle vectors commit once and retain caller object identity', async () => {
  const f = await fixture();
  try {
    const obstacle = f.world.addObstacle({ bumper: false });
    const ball = f.world.addBall(0.5, [0, 5, 0]);
    f.resetCounts();
    f.world.gravity[1] = -9;
    obstacle.position[0] = 1;
    obstacle.size[1] = 3;
    f.world.step();
    assert.ok(ball.velocities[1] < 0, 'the edited gravity must reach the native integrator');
    assert.equal(f.world.obstacles[0], obstacle);
    assert.deepEqual(f.calls, { configure: 1, setObstacles: 1 });
    f.world.step();
    assert.deepEqual(f.calls, { configure: 1, setObstacles: 1 });
    f.world.dt = 0;
    assert.equal(f.world.step(), false);
    assert.deepEqual(f.calls, { configure: 2, setObstacles: 1 });
  } finally {
    f.world.dispose();
  }
});

test('equal geometry with a new obstacle identity and reordered obstacles updates hit owners', async () => {
  const f = await fixture({ substeps: 1, solverIterations: 1 });
  try {
    const original = f.world.addObstacle({
      shape: 'box',
      position: [0, 0, 0],
      size: [4, 2, 4],
    });
    const far = f.world.addObstacle({ position: [5, -5, 5] });
    const replacement = {
      ...original,
      position: Array.from(original.position),
      size: Array.from(original.size),
    };
    f.world.obstacles[0] = replacement;
    f.world.obstacles.reverse();
    f.world.addBall(0.5, [0, 1.4, 0], [0, -1, 0]);
    f.resetCounts();
    f.world.step();
    assert.deepEqual(f.calls, { configure: 0, setObstacles: 1 });
    assert.ok(f.world.bumperHits.some((hit) => hit.obstacle === replacement));
    assert.ok(f.world.bumperHits.every((hit) => hit.obstacle !== original));
    assert.equal(f.world.obstacles[0], far);
    f.world.step();
    assert.deepEqual(f.calls, { configure: 0, setObstacles: 1 });
  } finally {
    f.world.dispose();
  }
});

test('invalid direct edits never become a successful cache entry, including zero-time steps', async () => {
  const f = await fixture();
  try {
    const obstacle = f.world.addObstacle({ bumper: false });
    for (const invalidGravity of [
      [0, NaN, 0],
      [0, 0],
      [0, 'invalid', 0],
    ]) {
      f.world.gravity = invalidGravity;
      for (let attempt = 0; attempt < 2; attempt++) assert.throws(() => f.world.step(0));
    }
    f.world.gravity = [0, 0, 0];
    for (const invalid of [
      [0, NaN, 0],
      [0, 0],
      [0, 'invalid', 0],
    ]) {
      obstacle.position = invalid;
      for (let attempt = 0; attempt < 2; attempt++) assert.throws(() => f.world.step(0));
    }
    obstacle.position = [0, -3, 0];
    f.world.step(0);
    delete f.world.obstacles[0];
    for (let attempt = 0; attempt < 2; attempt++) assert.throws(() => f.world.step(0));
    f.world.obstacles[0] = obstacle;
    f.resetCounts();
    f.world.step(0);
    assert.deepEqual(f.calls, { configure: 0, setObstacles: 0 });
  } finally {
    f.world.dispose();
  }
});

test('a failed native commit is retried and a changed native function is not hidden by wrappers', async () => {
  const f = await fixture();
  try {
    const failure = new Error('injected input commit failure');
    let attempts = 0;
    f.overrides.set('configure', () => {
      attempts++;
      throw failure;
    });
    f.world.gravity[1] = -1;
    assert.throws(
      () => f.world.step(0),
      (error) => error === failure,
    );
    assert.throws(
      () => f.world.step(0),
      (error) => error === failure,
    );
    assert.equal(attempts, 2);
    f.overrides.delete('configure');
    assert.equal(f.world.step(0), false);
    assert.equal(f.calls.configure, 1);
    f.world.step(0);
    assert.equal(f.calls.configure, 1);
    const obstacle = f.world.addObstacle({ bumper: false });
    const before = f.calls.setObstacles;
    f.overrides.set('setObstacles', () => {
      throw failure;
    });
    for (let attempt = 0; attempt < 2; attempt++)
      assert.throws(
        () => f.world.updateObstacle(obstacle, { radius: 3 }),
        (error) => error === failure,
      );
    assert.equal(obstacle.radius, 2, 'failed commands retain the original public obstacle');
    f.overrides.delete('setObstacles');
    f.world.updateObstacle(obstacle, { radius: 3 });
    f.world.step(0);
    assert.equal(f.calls.setObstacles, before + 1);
  } finally {
    f.world.dispose();
  }
});
