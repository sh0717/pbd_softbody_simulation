import assert from 'node:assert/strict';
import test from 'node:test';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const api = await require('../../src/simulation/physics.js').init();
const data = {
  verts: [0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1],
  tetEdgeIds: [0, 1],
  tetIds: [0, 1, 2, 3],
  tetSurfaceTriIds: [],
};

for (const [field, method] of [
  ['edgeCompliance', 'solveEdges'],
  ['volumeCompliance', 'solveVolumes'],
]) {
  test(`${field} overflow is rejected before world prediction and direct projection`, () => {
    const world = new api.PhysicsWorld({ dt: 0.1, substeps: 1, [field]: 1e308 });

    try {
      const body = world.addSoftBody(data);
      const positions = Array.from(body.positions);
      const velocities = Array.from(body.velocities);

      assert.throws(() => world.step(), /compliance|scaling/i);
      assert.deepEqual(Array.from(body.positions), positions);
      assert.deepEqual(Array.from(body.velocities), velocities);
      assert.equal(world.simulationTime, 0);
      assert.throws(() => body[method](0.1, world), /compliance|scaling/i);
      assert.deepEqual(Array.from(body.positions), positions);
      assert.ok(Array.from(body.edgeLambdas).every((value) => value === 0));
      assert.ok(Array.from(body.volumeLambdas).every((value) => value === 0));

      world.configure({ [field]: 0 });
      world.step();

      assert.ok(Array.from(body.positions).every(Number.isFinite));
    } finally {
      world.dispose();
    }
  });
}

test('six-volume compliance overflow is rejected before any mutation', () => {
  const world = new api.PhysicsWorld({ dt: 1, substeps: 1, volumeCompliance: 1e307 });

  try {
    const body = world.addSoftBody(data, [0, 0, 0], { velocity: [1, 2, 3] });
    const before = [body.positions, body.velocities, body.volumeLambdas].map((values) =>
      Array.from(values),
    );

    assert.throws(() => world.step(), /compliance|scaling/i);
    assert.throws(() => body.solveVolumes(1, world), /compliance|scaling/i);
    assert.deepEqual(
      [body.positions, body.velocities, body.volumeLambdas].map((values) => Array.from(values)),
      before,
    );
    assert.equal(world.simulationTime, 0);
  } finally {
    world.dispose();
  }
});

test('six-volume compliance divides by h squared before multiplying by 36', () => {
  const world = new api.PhysicsWorld({ volumeCompliance: 1e307 });

  try {
    const body = world.addSoftBody(data, [0, 0, 0], { inverseMass: [0, 0, 0, 1] });
    body.positions[11] = 2;
    body.solveVolumes(1e154, world);

    const expectedLambda = -1 / (1 + 36 * (1e307 / (1e154 * 1e154)));

    assert.ok(Math.abs(body.volumeLambdas[0] - expectedLambda) < 1e-12);
    assert.ok(Math.abs(body.positions[11] - (2 + expectedLambda)) < 1e-12);
  } finally {
    world.dispose();
  }
});

test('unused PBD compliance and a zero-time XPBD step remain supported', () => {
  const world = new api.PhysicsWorld({
    dt: 0,
    substeps: 1,
    edgeCompliance: 1e308,
    volumeCompliance: 1e308,
  });

  try {
    const body = world.addSoftBody(data);
    const before = Array.from(body.positions);

    world.step();

    assert.deepEqual(Array.from(body.positions), before);

    world.configure({ mode: 'pbd', dt: 0.1 });
    world.step();

    assert.ok(Array.from(body.positions).every(Number.isFinite));
  } finally {
    world.dispose();
  }
});
