import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const api = await require('../../src/simulation/physics.js').init();
const bunny = require('../../src/data/bunny-data.js');

test('box-only obstacle boundary rejects sphere and radius without changing registered geometry', () => {
  const world = new api.PhysicsWorld({ bounds: 100, gravity: [0, 0, 0] });
  try {
    const obstacle = world.addObstacle({ position: [0, 0, 0], size: [2, 2, 2], bumper: false });
    const ball = world.addBall(1, [0, 0, 0]);
    const saved = JSON.stringify(obstacle);
    for (const options of [{ shape: 'sphere' }, { radius: 2 }]) {
      assert.throws(() => world.addObstacle(options), TypeError);
      assert.throws(() => world.updateObstacle(obstacle, options), TypeError);
      assert.equal(JSON.stringify(obstacle), saved);
      assert.equal(world.obstacles.length, 1);

      // 공개 mirror 직접 수정은 JS 명령 검증을 우회하므로 Wasm 경계에서도 거부해야 합니다.
      obstacle.size = [100, 100, 100];
      world.obstacles.push({ shape: 'box', position: [0, 0, 0], size: [2, 2, 2], ...options });
      assert.throws(() => world.step(0), TypeError);
      world.obstacles.pop();
      obstacle.size = [2, 2, 2];
      Object.assign(obstacle, options);
      assert.throws(
        () => world.step(0),
        TypeError,
        'cached inputs must validate direct shape/radius edits',
      );
      obstacle.shape = 'box';
      delete obstacle.radius;
    }
    assert.equal('radius' in obstacle, false);
    world.settleObstacleContacts();
    assert.deepEqual(
      Array.from(ball.positions),
      [2, 0, 0],
      'failed batches preserve the registered box',
    );
    assert.equal(world.step(0), false);
  } finally {
    world.dispose();
  }
});

test('configuration commands validate before committing mirrored settings', () => {
  const world = new api.PhysicsWorld({ dt: 1 / 60, substeps: 3 });
  world.configure({ dt: 0.02, mode: 'pbd', gravity: [0, -9.81, 0] });
  assert.equal(world.dt, 0.02);
  assert.equal(world.mode, 'pbd');
  for (const [options, ErrorType] of [
    [{ dt: -1 }, RangeError],
    [{ substeps: 0 }, RangeError],
    [{ mode: 'unknown' }, RangeError],
    [{ gravity: [0, NaN, 0] }, TypeError],
  ]) {
    assert.throws(() => world.configure(options), ErrorType);
    assert.equal(world.dt, 0.02);
    assert.equal(world.substeps, 3);
    assert.equal(world.mode, 'pbd');
    assert.deepEqual(world.gravity, [0, -9.81, 0]);
  }
  assert.equal(world.step(), true);
  world.dispose();
  assert.throws(() => world.configure({ dt: 0 }), /disposed/);
});

test('obstacle commands preserve ownership, object identity and failed edit state', () => {
  const world = new api.PhysicsWorld({ bounds: 10 });
  const other = new api.PhysicsWorld();
  const obstacle = world.addObstacle({ shape: 'box', position: [100, 0, 0] });
  assert.equal(world.obstacles[0], obstacle);
  assert.deepEqual(obstacle.position, [6, 0, 0]);
  world.updateObstacle(obstacle, { size: [6, 2, 4] });
  assert.equal(world.obstacles[0], obstacle);
  assert.deepEqual(obstacle.position, [5, 0, 0]);
  const saved = JSON.stringify(obstacle);
  for (const edit of [
    { radius: -1 },
    { radius: 1 },
    { shape: 'sphere' },
    { size: [1, NaN, 1] },
    { shape: 'invalid' },
    { bumperStrength: 20 },
  ]) {
    assert.throws(() => world.updateObstacle(obstacle, edit));
    assert.equal(JSON.stringify(obstacle), saved);
  }
  assert.throws(() => other.updateObstacle(obstacle, { size: [2, 2, 2] }), /another world/);
  assert.throws(() => other.removeObstacle(obstacle), /another world/);
  assert.throws(() => world.addObstacle({ position: [0, Infinity, 0] }));
  assert.equal(world.obstacles.length, 1);
  world.addBall(1, [0, 0, 0]);
  assert.equal(world.step(), true);
  world.removeObstacle(obstacle);
  assert.equal(world.obstacles.length, 0);
  world.dispose();
  other.dispose();
  assert.throws(() => world.addObstacle(), /disposed/);
});

test('native exceptions become typed JavaScript errors and the module remains usable', () => {
  for (let i = 0; i < 100; i++)
    assert.throws(() => new api.PhysicsWorld({ substeps: 0 }), RangeError);
  const world = new api.PhysicsWorld();
  assert.throws(() => world.addBall(-1, [0, 0, 0]), TypeError);
  assert.throws(() => world.addSoftBody({ verts: [0, 0, 0], tetIds: [0, 1, 2, 3] }), TypeError);
  assert.throws(() => world.addSoftBody(bunny, [0, 0, 0], { inverseMass: [] }), TypeError);
  assert.throws(() => world.step(-1), RangeError);
  assert.throws(() => world.step(1e-300), RangeError);
  const body = world.addBall(1, [0, 0, 0]);
  assert.throws(() => {
    body.dragTarget = [0, NaN, 0];
  }, TypeError);
  assert.equal(world.step(), true);
  world.dispose();
});

test('worlds own independent native state and disposal invalidates borrowed bodies', () => {
  const first = new api.PhysicsWorld(),
    second = new api.PhysicsWorld();
  const a = first.addBall(1, [0, 0, 0], [1, 0, 0]),
    b = second.addBall(1, [0, 0, 0]);
  first.step();
  assert.ok(a.positions[0] > 0);
  assert.equal(b.positions[0], 0);
  assert.throws(() => second.moveBody(a, [1, 2, 3]), TypeError);
  first.dispose();
  first.dispose();
  assert.throws(() => first.step(), /disposed/);
  assert.throws(() => a.positions, /disposed/);
  assert.equal(second.step(), true);
  second.dispose();
});

test('native buffers are reacquired after WebAssembly memory growth', () => {
  const world = new api.PhysicsWorld();
  const first = world.addSoftBody(bunny),
    saved = first.positions;
  for (let i = 0; i < 700; i++) world.addSoftBody(bunny, [i * 2, 0, 0]);
  assert.equal(saved.buffer.byteLength, 0, 'test must trigger memory growth');
  assert.equal(first.positions.length, bunny.verts.length);
  first.positions[0] = 123;
  assert.equal(first.positions[0], 123);
  world.reset();
  assert.equal(first.positions[0], bunny.verts[0]);
  world.dispose();
});

test('native move preserves soft shape and reset restores the original translation', () => {
  const world = new api.PhysicsWorld();
  const body = world.addSoftBody(bunny, [2, 3, 4]);
  const rest = body.positions.slice(),
    center = body.center.slice();
  world.moveBody(body, [5, 6, 7]);
  for (let i = 0; i < rest.length; i++)
    assert.ok(Math.abs(body.positions[i] - rest[i] - ([5, 6, 7][i % 3] - center[i % 3])) < 1e-12);
  world.reset();
  assert.deepEqual(body.positions, rest);
  world.dispose();
});

test('contact batches cannot outlive preparation or cross solver ownership', () => {
  const world = new api.PhysicsWorld();
  const a = world.addSoftBody({ verts: [0.1, 0.2, 0.3] }),
    b = world.addSoftBody({ verts: [0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1], tetIds: [0, 1, 2, 3] });
  const first = new api.SoftContactSolver(),
    second = new api.SoftContactSolver();
  first.prepare([a, b]);
  const contacts = first.generateContacts(a, b);
  assert.equal(contacts.length, 1);
  assert.throws(() => second.projectContacts(contacts), TypeError);
  first.prepare([a, b]);
  assert.throws(() => first.projectContacts(contacts), /Expired/);
  first.dispose();
  second.dispose();
  assert.throws(() => first.prepare([a, b]), /disposed/);
  world.dispose();
});

test('solver retains borrowed body owners and rejects use after world disposal', () => {
  const world = new api.PhysicsWorld(),
    body = world.addSoftBody(bunny),
    solver = new api.SoftContactSolver();
  solver.prepare([body]);
  world.dispose();
  assert.throws(() => solver.queryPoint(body, [0, 0, 0]), /disposed/);
  solver.dispose();
});

test('preparing a new batch releases previous owners and recovers from their disposal', () => {
  const oldWorld = new api.PhysicsWorld();
  const currentWorld = new api.PhysicsWorld();
  const oldBody = oldWorld.addSoftBody(bunny);
  const currentBody = currentWorld.addSoftBody(bunny);
  const solver = new api.SoftContactSolver();
  try {
    solver.prepare([oldBody]);
    solver.prepare([currentBody]);
    oldWorld.dispose();
    assert.doesNotThrow(() => solver.queryPoint(currentBody, [0, 0, 0]));
    const nextWorld = new api.PhysicsWorld();
    try {
      const nextBody = nextWorld.addSoftBody(bunny);
      currentWorld.dispose();
      assert.doesNotThrow(() => solver.prepare([nextBody]));
      assert.doesNotThrow(() => solver.queryPoint(nextBody, [0, 0, 0]));
    } finally {
      nextWorld.dispose();
    }
  } finally {
    solver.dispose();
    oldWorld.dispose();
    currentWorld.dispose();
  }
});

test('failed preparation preserves the previous native batch and its owner mapping', () => {
  const world = new api.PhysicsWorld();
  const body = world.addSoftBody(bunny);
  const invalid = world.addSoftBody({ verts: [0, 0, 0, 1, 0, 0], tetIds: [0, 1, 1, 0] });
  const solver = new api.SoftContactSolver();
  try {
    solver.prepare([body]);
    const before = solver.queryPoint(body, [0, 0, 0]);
    assert.throws(() => solver.prepare([invalid]), /contact tetrahedron/);
    assert.deepEqual(solver.queryPoint(body, [0, 0, 0]), before);
    assert.throws(() => solver.queryPoint(invalid, [0, 0, 0]), /Call prepare/);
    const ball = world.addBall(1, [0, 0, 0]);
    assert.throws(() => solver.prepare([ball]), TypeError);
    assert.deepEqual(solver.queryPoint(body, [0, 0, 0]), before);
  } finally {
    solver.dispose();
    world.dispose();
  }
});

test('switching native solver owners rejects contacts even when native generation numbers repeat', () => {
  const oldWorld = new api.PhysicsWorld();
  const newWorld = new api.PhysicsWorld();
  const solver = new api.SoftContactSolver();
  const tetra = { verts: [0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1], tetIds: [0, 1, 2, 3] };
  try {
    const oldPoint = oldWorld.addSoftBody({ verts: [0.1, 0.2, 0.3] });
    const oldTarget = oldWorld.addSoftBody(tetra);
    solver.prepare([oldPoint, oldTarget]);
    const expired = solver.generateContacts(oldPoint, oldTarget);
    assert.equal(expired.length, 1);
    const newPoint = newWorld.addSoftBody({ verts: [0.1, 0.2, 0.3] });
    const newTarget = newWorld.addSoftBody(tetra);
    solver.prepare([newPoint, newTarget]);
    const current = solver.generateContacts(newPoint, newTarget);
    assert.equal(current.length, 1);
    assert.equal(
      expired[0]._generation,
      current[0]._generation,
      'exercise repeated native generations',
    );
    const saved = Array.from(newPoint.positions);
    assert.throws(() => solver.projectContacts(expired), /Expired contact batch/);
    assert.deepEqual(Array.from(newPoint.positions), saved);
    assert.equal(solver.projectContacts(current), 1);
  } finally {
    solver.dispose();
    oldWorld.dispose();
    newWorld.dispose();
  }
});

test('successive worlds with different topology never inherit a previous body cache', () => {
  const solver = new api.SoftContactSolver();
  const tetra = { verts: [0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1], tetIds: [0, 1, 2, 3] };
  try {
    for (let round = 0; round < 16; round++) {
      const world = new api.PhysicsWorld();
      try {
        const data = round % 2 ? tetra : bunny;
        const body = world.addSoftBody(data);
        solver.prepare([body]);
        assert.equal(
          solver.getBodyInfo(body).tetCount,
          (data.tetIds ?? data.tetIndices).length / 4,
        );
        assert.doesNotThrow(() => solver.queryPoint(body, [0.1, 0.1, 0.1]));
      } finally {
        world.dispose();
      }
    }
  } finally {
    solver.dispose();
  }
});

test('native owner registries tolerate duplicate destruction and reject expired owner handles', async () => {
  const native = await require('../../wasm/pbd_core.js')();
  const world = native.createWorld({});
  const solver = native.createSolver({});
  const exceptionMessage = (action) => {
    try {
      action();
    } catch (error) {
      if (typeof error !== 'number') throw error;
      try {
        return native.getExceptionMessage(error)[1];
      } finally {
        native.decrementExceptionRefcount(error);
      }
    }
    assert.fail('expired native owner was accepted');
  };
  native.destroyWorld(world);
  native.destroyWorld(world);
  assert.match(
    exceptionMessage(() => native.step(world, 1 / 60)),
    /Expired native owner/,
  );
  native.destroySolver(solver);
  native.destroySolver(solver);
  assert.match(
    exceptionMessage(() => native.solverStats(solver)),
    /Expired native owner/,
  );
  const nextWorld = native.createWorld({});
  const nextSolver = native.createSolver({});
  try {
    assert.notEqual(nextWorld, world, 'world recreation reused an expired owner token');
    assert.notEqual(nextSolver, solver, 'solver recreation reused an expired owner token');
    assert.notEqual(nextWorld, nextSolver, 'different owner types share a live token');
    assert.match(
      exceptionMessage(() => native.step(world, 1 / 60)),
      /Expired native owner/,
    );
    assert.match(
      exceptionMessage(() => native.solverStats(solver)),
      /Expired native owner/,
    );
    assert.match(
      exceptionMessage(() => native.solverStats(nextWorld)),
      /Expired native owner/,
    );
    assert.match(
      exceptionMessage(() => native.step(nextSolver, 1 / 60)),
      /Expired native owner/,
    );
    native.destroyWorld(world);
    native.destroySolver(solver);
    native.destroyWorld(nextSolver);
    native.destroySolver(nextWorld);
    assert.equal(native.step(nextWorld, 1 / 60), true);
    assert.equal(native.solverStats(nextSolver).contacts, 0);
  } finally {
    native.destroyWorld(nextWorld);
    native.destroySolver(nextSolver);
  }
});
