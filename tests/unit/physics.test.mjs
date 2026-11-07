// Run: node --test tests/unit/physics.test.mjs
// These tests verify geometric residuals, analytic XPBD behavior, and invariants.
// They exercise the production solver without a renderer or a browser runtime.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
await require('../../src/simulation/physics.js').init();
const { PhysicsWorld, tetraVolume } = require('../../src/simulation/physics.js');
const bunnyData = require('../../src/data/bunny-data.js');

const edgeData = {
  verts: [0, 0, 0, 1, 0, 0],
  tetEdgeIds: [0, 1],
  tetIds: [],
  tetSurfaceTriIds: [],
};
const tetraData = {
  verts: [0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1],
  tetEdgeIds: [],
  tetIds: [0, 1, 2, 3],
  tetSurfaceTriIds: [],
};

function world(options = {}) {
  return new PhysicsWorld({
    bounds: 100,
    gravity: [0, 0, 0],
    damping: 0,
    restitution: 0,
    dt: 0.1,
    substeps: 1,
    solverIterations: 1,
    mode: 'xpbd',
    edgeCompliance: 0,
    volumeCompliance: 0,
    ...options,
  });
}

function near(actual, expected, tolerance = 1e-10) {
  assert.ok(
    Math.abs(actual - expected) <= tolerance,
    `Expected ${actual} to equal ${expected} within ${tolerance}`,
  );
}

function allFinite(body) {
  for (const values of [body.positions, body.velocities]) {
    assert.ok(Array.from(values).every(Number.isFinite));
  }
}

function pointDistance(a, b) {
  return Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}

test('tetraVolume preserves orientation and translation invariance', () => {
  near(tetraVolume(tetraData.verts), 1 / 6);
  near(tetraVolume(tetraData.verts, 0, 2, 1, 3), -1 / 6);
  const shifted = tetraData.verts.map((value, index) => value + [3, -8, 11][index % 3]);
  near(tetraVolume(shifted), 1 / 6);
});

for (const orientation of ['positive', 'negative']) {
  for (const height of [0.2, 1.8, -0.3]) {
    test(`signed volume restores ${orientation} tetrahedron from height ${height}`, () => {
      const simulation = world();
      const data = {
        ...tetraData,
        tetIds: orientation === 'positive' ? [0, 1, 2, 3] : [0, 2, 1, 3],
      };
      const body = simulation.addSoftBody(data, [0, 0, 0], { inverseMass: [0, 0, 0, 1] });
      const fixed = Array.from(body.positions.slice(0, 9));
      body.positions[11] = height;
      body.solveVolumes(0.1, simulation);
      // With three pinned vertices, volume is linear in the fourth height.
      near(body.positions[11], 1);
      near(tetraVolume(body.positions, ...data.tetIds), body.restVolumes[0]);
      assert.deepEqual(Array.from(body.positions.slice(0, 9)), fixed);
      allFinite(body);
    });
  }
}

test('unpinned volume projection reduces both compression and expansion residuals', () => {
  for (const height of [0.25, 2, -0.25]) {
    const simulation = world();
    const body = simulation.addSoftBody(tetraData);
    body.positions[11] = height;
    const before = Math.abs(tetraVolume(body.positions) - 1 / 6);
    const summedBefore = [0, 1, 2].map((axis) =>
      [0, 1, 2, 3].reduce((sum, vertex) => sum + body.positions[vertex * 3 + axis], 0),
    );
    for (let iteration = 0; iteration < 20; iteration += 1) body.solveVolumes(0.1, simulation);
    assert.ok(Math.abs(tetraVolume(body.positions) - 1 / 6) < before * 1e-6);
    for (let axis = 0; axis < 3; axis += 1) {
      near(
        [0, 1, 2, 3].reduce((sum, vertex) => sum + body.positions[vertex * 3 + axis], 0),
        summedBefore[axis],
      );
    }
    allFinite(body);
  }
});

test('XPBD edge compliance uses compliance / h squared', () => {
  for (const h of [0.1, 0.2]) {
    const simulation = world({ edgeCompliance: 0.02 });
    const body = simulation.addSoftBody(edgeData, [0, 0, 0], { inverseMass: [0, 2] });
    body.positions[3] = 2;
    body.solveEdges(h, simulation);
    const scaledCompliance = 0.02 / (h * h);
    // Analytic residual for a single linear distance constraint C_initial = 1.
    near(body.positions[3] - 1, scaledCompliance / (2 + scaledCompliance));
    near(body.positions[0], 0);
  }
});

test('six-volume XPBD preserves physical compliance and uses the matching multiplier', () => {
  const simulation = world({ volumeCompliance: 0.001 });
  const body = simulation.addSoftBody(tetraData, [0, 0, 0], { inverseMass: [0, 0, 0, 1] });
  body.positions[11] = 2;
  body.solveVolumes(0.1, simulation);
  // C=6(V-V0) 의 초기값과 움직이는 입자의 기울기는 각각 1, (0,0,1)입니다.
  // 아래 기대 잔차는 물리 체적으로 계산하여 기존 유연도 설정의 의미가 보존되는지 확인합니다.
  const scaledCompliance = 0.001 / 0.01;
  near(
    tetraVolume(body.positions) - 1 / 6,
    ((1 / 6) * scaledCompliance) / (1 / 36 + scaledCompliance),
  );
  near(body.volumeLambdas[0], -1 / (1 + 36 * scaledCompliance));
});

test('XPBD multipliers are preserved across solver iterations', () => {
  const simulation = world({ edgeCompliance: 0.02 });
  const body = simulation.addSoftBody(edgeData, [0, 0, 0], { inverseMass: [0, 1] });
  body.positions[3] = 2;
  body.solveEdges(0.1, simulation);
  const afterFirst = body.positions[3];
  const lambda = body.edgeLambdas[0];
  for (let iteration = 0; iteration < 8; iteration += 1) body.solveEdges(0.1, simulation);
  near(body.positions[3], afterFirst);
  near(body.edgeLambdas[0], lambda);
});

test('XPBD volume multiplier preserves the analytic compliant equilibrium', () => {
  const simulation = world({ volumeCompliance: 0.001 });
  const body = simulation.addSoftBody(tetraData, [0, 0, 0], { inverseMass: [0, 0, 0, 1] });
  body.positions[11] = 2;
  body.solveVolumes(0.1, simulation);
  const height = body.positions[11],
    lambda = body.volumeLambdas[0];
  for (let iteration = 0; iteration < 8; iteration += 1) body.solveVolumes(0.1, simulation);
  near(body.positions[11], height);
  near(body.volumeLambdas[0], lambda);
});

test('XPBD multipliers reset at each shared substep', () => {
  const simulation = world({ dt: 0.1, substeps: 2, solverIterations: 3, edgeCompliance: 0.005 });
  const body = simulation.addSoftBody(edgeData, [0, 0, 0], { inverseMass: [0, 1] });
  body.positions[3] = 2;
  simulation.step();
  // h=.05, alpha/h²=2: substep one ends at 5/3. Its velocity predicts
  // 4/3 at substep two, whose independently reset solve ends at 11/9.
  near(body.positions[3], 11 / 9);
  near(body.edgeLambdas[0], -1 / 9);
});

test('PBD hard edge constraint is independent of the supplied timestep', () => {
  for (const h of [0.001, 0.1]) {
    const simulation = world({ mode: 'pbd', pbdStiffness: 1, edgeCompliance: 5 });
    const body = simulation.addSoftBody(edgeData, [0, 0, 0], { inverseMass: [1, 3] });
    body.positions[3] = 2;
    body.solveEdges(h, simulation);
    near(body.positions[3] - body.positions[0], 1);
    // m0=1, m1=1/3; center of mass must not move.
    near((body.positions[0] + body.positions[3] / 3) / (4 / 3), 0.5);
  }
});

test('coincident edge vertices separate along a finite rest direction', () => {
  const simulation = world();
  const body = simulation.addSoftBody(edgeData);
  body.positions[3] = 0;
  body.solveEdges(0.1, simulation);
  near(pointDistance(body.positions.slice(0, 3), body.positions.slice(3, 6)), 1);
  allFinite(body);
});

test('fully degenerate tetrahedron produces finite state', () => {
  const simulation = world();
  const body = simulation.addSoftBody(tetraData);
  body.positions.fill(0);
  body.solveVolumes(0.1, simulation);
  allFinite(body);
});

test('near collapsed tetrahedron cannot create an unbounded Newton projection', () => {
  const simulation = world({ mode: 'pbd' });
  const body = simulation.addSoftBody(tetraData);
  for (let index = 0; index < body.positions.length; index += 1) body.positions[index] *= 1e-12;
  const before = Array.from(body.positions);
  body.solveVolumes(0.01, simulation);
  // A singular configuration cannot establish a reliable restoring direction.
  // Recovery may be skipped or bounded, but it must not jump 1e23 world units.
  const restDiameter = Math.sqrt(2);
  for (let index = 0; index < body.positions.length; index += 3) {
    assert.ok(
      pointDistance(body.positions.slice(index, index + 3), before.slice(index, index + 3)) <=
        2 * restDiameter,
    );
  }
  allFinite(body);
});

test('ball contact separates both bodies according to inverse mass', () => {
  const simulation = world();
  const first = simulation.addBall(1, [-0.5, 0, 0], [0, 0, 0], { inverseMass: 1 });
  const second = simulation.addBall(1, [0.5, 0, 0], [0, 0, 0], { inverseMass: 3 });
  simulation.step();
  near(first.positions[0], -0.75);
  near(second.positions[0], 1.25);
  near(pointDistance(first.positions, second.positions), 2);
  near(first.velocities[0] + second.velocities[0] / 3, 0);
  allFinite(first);
  allFinite(second);
});

test('a pair of balls has identical labeled results in either storage order', () => {
  const simulate = (reversed) => {
    const simulation = world({ substeps: 3, restitution: 0.5 });
    let first, second;
    if (reversed) {
      second = simulation.addBall(1, [0.5, 0, 0], [-1, 0, 0]);
      first = simulation.addBall(1, [-0.5, 0, 0], [1, 0, 0]);
    } else {
      first = simulation.addBall(1, [-0.5, 0, 0], [1, 0, 0]);
      second = simulation.addBall(1, [0.5, 0, 0], [-1, 0, 0]);
    }
    simulation.step();
    return [...first.positions, ...first.velocities, ...second.positions, ...second.velocities];
  };
  const normal = simulate(false),
    reversed = simulate(true);
  normal.forEach((value, index) => near(value, reversed[index]));
});

test('coincident ball contact is finite and conserves center of mass', () => {
  const simulation = world();
  const first = simulation.addBall(1, [0, 0, 0]);
  const second = simulation.addBall(1, [0, 0, 0]);
  simulation.step();
  near(pointDistance(first.positions, second.positions), 2);
  for (let axis = 0; axis < 3; axis += 1) near(first.positions[axis] + second.positions[axis], 0);
  allFinite(first);
  allFinite(second);
});

test('initially overlapping approaching balls do not convert depenetration into excess kinetic energy', () => {
  const simulation = world({ restitution: 0.5, substeps: 3 });
  const first = simulation.addBall(1, [-0.5, 0, 0], [1, 0, 0]);
  const second = simulation.addBall(1, [0.5, 0, 0], [-1, 0, 0]);
  simulation.step();
  near(first.velocities[0] + second.velocities[0], 0);
  near(second.velocities[0] - first.velocities[0], 1);
  assert.ok((first.velocities[0] ** 2 + second.velocities[0] ** 2) / 2 <= 1 + 1e-10);
});

test('stationary overlapping balls recover contact without artificial kinetic energy', () => {
  const simulation = world({ restitution: 0.5 });
  const first = simulation.addBall(1, [-0.5, 0, 0]);
  const second = simulation.addBall(1, [0.5, 0, 0]);
  simulation.step();
  near(pointDistance(first.positions, second.positions), 2);
  for (const ball of [first, second]) {
    near(Math.hypot(...ball.velocities), 0);
  }
});

test('disjoint tetrahedra stay unchanged despite overlapping bounding spheres', () => {
  const simulation = world();
  const first = simulation.addSoftBody(tetraData);
  const second = simulation.addSoftBody(tetraData, [1.1, 0, 0]);
  const before = [first, second].map((body) => Array.from(body.positions));
  assert.ok(pointDistance(first.center, second.center) < first.radius + second.radius);
  simulation.step();
  for (const [index, body] of [first, second].entries()) {
    assert.deepEqual(Array.from(body.positions), before[index]);
    assert.ok(Array.from(body.velocities).every((value) => Math.abs(value) < 1e-10));
    allFinite(body);
  }
});

test('soft vertex and ball receive symmetric mass weighted contact correction', () => {
  const simulation = world();
  const pointData = { verts: [0, 0, 0], tetEdgeIds: [], tetIds: [], tetSurfaceTriIds: [] };
  const soft = simulation.addSoftBody(pointData, [0, 0, 0], { inverseMass: [2] });
  const ball = simulation.addBall(1, [0.5, 0, 0], [0, 0, 0], { inverseMass: 1 });
  simulation.step();
  near(soft.positions[0], -1 / 3);
  near(ball.positions[0], 2 / 3);
  near(pointDistance(soft.positions, ball.positions), 1);
  near(soft.positions[0] / 2 + ball.positions[0], 0.5);
  allFinite(soft);
  allFinite(ball);
});

test('pinned bodies remain fixed during contacts and integration', () => {
  const simulation = world({ gravity: [0, -10, 0] });
  const pinned = simulation.addBall(1, [0, 0, 0], [0, 0, 0], { inverseMass: 0 });
  const moving = simulation.addBall(1, [0.5, 0, 0]);
  simulation.step();
  assert.deepEqual(Array.from(pinned.positions), [0, 0, 0]);
  assert.ok(pointDistance(pinned.positions, moving.positions) >= 2 - 1e-10);
});

test('all free bodies advance through the same substep clock', () => {
  const simulation = world({ dt: 0.12, substeps: 4, gravity: [0, -10, 0] });
  const soft = simulation.addSoftBody(tetraData, [-10, 10, 0], { velocity: [1, 0, 0] });
  const ball = simulation.addBall(1, [10, 10, 0], [1, 0, 0]);
  const rest = Array.from(soft.positions);
  simulation.step();
  // Symplectic Euler n substeps: delta = v0*dt + a*dt²*(n+1)/(2n).
  const fall = (-10 * 0.12 ** 2 * 5) / 8;
  near(ball.positions[0], 10.12);
  near(ball.positions[1], 10 + fall);
  near(ball.velocities[1], -1.2);
  for (let index = 0; index < soft.positions.length; index += 3) {
    near(soft.positions[index], rest[index] + 0.12);
    near(soft.positions[index + 1], rest[index + 1] + fall);
    near(soft.velocities[index + 1], -1.2);
  }
});

test('zero timestep leaves positions, velocities, and multipliers unchanged', () => {
  const simulation = world();
  const soft = simulation.addSoftBody(tetraData);
  const ball = simulation.addBall(1, [4, 0, 0], [1, 2, 3]);
  soft.volumeLambdas[0] = 123;
  const before = [
    soft.positions,
    soft.velocities,
    soft.volumeLambdas,
    ball.positions,
    ball.velocities,
  ].map((values) => Array.from(values));
  assert.equal(simulation.step(0), false);
  const after = [
    soft.positions,
    soft.velocities,
    soft.volumeLambdas,
    ball.positions,
    ball.velocities,
  ].map((values) => Array.from(values));
  assert.deepEqual(after, before);
});

test('reset restores translated rest state and clears velocity and multipliers', () => {
  const simulation = world();
  const soft = simulation.addSoftBody({ ...tetraData, tetEdgeIds: [0, 1] }, [2, 3, 4]);
  const ball = simulation.addBall(1, [-4, 2, 1]);
  const rest = Array.from(soft.positions);
  soft.positions.fill(9);
  soft.velocities.fill(3);
  soft.edgeLambdas.fill(8);
  soft.volumeLambdas.fill(9);
  ball.positions.fill(8);
  ball.velocities.fill(4);
  simulation.reset();
  assert.deepEqual(Array.from(soft.positions), rest);
  assert.deepEqual(Array.from(ball.positions), [-4, 2, 1]);
  for (const values of [soft.velocities, soft.edgeLambdas, soft.volumeLambdas, ball.velocities]) {
    assert.ok(Array.from(values).every((value) => value === 0));
  }
});

test('bounds derive from the translated and deformed current vertices', () => {
  const simulation = world();
  const body = simulation.addSoftBody(tetraData, [5, -3, 2]);
  body.positions[3] += 4;
  body.updateBounds();
  for (let index = 0; index < body.positions.length; index += 3) {
    assert.ok(
      pointDistance(body.positions.slice(index, index + 3), body.center) <= body.radius + 1e-12,
    );
  }
});

for (const restitution of [0, 0.5, 1]) {
  test(`ball wall restitution ${restitution} preserves tangential velocity`, () => {
    const simulation = world({ bounds: 10, restitution, solverIterations: 3 });
    const ball = simulation.addBall(1, [8.95, 0, 0], [1, 0.2, 0]);
    simulation.step();
    near(ball.positions[0], 9);
    near(ball.velocities[0], -restitution);
    near(ball.velocities[1], 0.2);
    const normalEnergyRatio = ball.velocities[0] ** 2;
    near(normalEnergyRatio, restitution ** 2);
  });

  test(`ball pair restitution ${restitution} conserves momentum without added energy`, () => {
    const simulation = world({ restitution, solverIterations: 3 });
    const first = simulation.addBall(1, [-1.1, 0, 0], [2, 0, 0], { inverseMass: 1 });
    const second = simulation.addBall(1, [1.1, 0, 0], [-2, 0, 0], { inverseMass: 3 });
    simulation.step();
    near(first.velocities[0] + second.velocities[0] / 3, 4 / 3);
    near(second.velocities[0] - first.velocities[0], 4 * restitution);
    const energyAfter = (first.velocities[0] ** 2 + second.velocities[0] ** 2 / 3) / 2;
    assert.ok(energyAfter <= 8 / 3 + 1e-10);
  });
}

test('sphere obstacle includes rigid radius and resolves exact center overlap', () => {
  const simulation = world();
  const ball = simulation.addBall(1, [2, 3, 4], [1, 2, 3]);
  simulation.obstacles.push({ shape: 'sphere', position: [2, 3, 4], radius: 2 });
  simulation.settleObstacleContacts();
  near(pointDistance(ball.positions, [2, 3, 4]), 3);
  assert.deepEqual(Array.from(ball.velocities), [0, 0, 0]);
  allFinite(ball);
});

test('sphere versus box corner uses rounded closest point contact', () => {
  const simulation = world();
  const ball = simulation.addBall(1, [1.3, 1.4, 0]);
  simulation.obstacles.push({ shape: 'box', position: [0, 0, 0], size: [2, 2, 2] });
  simulation.settleObstacleContacts();
  near(ball.positions[0], 1.6);
  near(ball.positions[1], 1.8);
  near(ball.positions[2], 0);
});

test('box obstacle exits the nearest face with rigid radius clearance', () => {
  const simulation = world();
  const ball = simulation.addBall(1, [0, 0, 0]);
  simulation.obstacles.push({ shape: 'box', position: [0, 0, 0], size: [4, 2, 6] });
  simulation.settleObstacleContacts();
  near(ball.positions[0], 0);
  near(Math.abs(ball.positions[1]), 2);
  near(ball.positions[2], 0);
});

test('paused obstacle editing revisits mutually overlapping contacts', () => {
  const simulation = world();
  const pointData = { verts: [1, 0.5, 0], tetEdgeIds: [], tetIds: [], tetSurfaceTriIds: [] };
  const soft = simulation.addSoftBody(pointData);
  simulation.obstacles.push(
    { shape: 'sphere', position: [0, 0, 0], radius: 2 },
    { shape: 'sphere', position: [3, 0, 0], radius: 2 },
  );
  simulation.settleObstacleContacts();
  for (const obstacle of simulation.obstacles) {
    assert.ok(pointDistance(soft.positions, obstacle.position) >= obstacle.radius - 1e-5);
  }
  assert.deepEqual(Array.from(soft.velocities), [0, 0, 0]);
  allFinite(soft);
});

test('editing a distant obstacle preserves velocities of unaffected bodies', () => {
  const simulation = world();
  const ball = simulation.addBall(1, [0, 0, 0], [1, 2, 3]);
  const soft = simulation.addSoftBody(tetraData, [10, 0, 0], { velocity: [2, 3, 4] });
  simulation.obstacles.push({ shape: 'sphere', position: [50, 0, 0], radius: 2 });
  const before = simulation.bodies.map((body) => Array.from(body.velocities));
  simulation.settleObstacleContacts();
  assert.deepEqual(
    simulation.bodies.map((body) => Array.from(body.velocities)),
    before,
  );
  simulation.step();
  near(ball.positions[0], 0.1);
  near(soft.positions[0], 10.2);
});

test('obstacle editing clears only corrected soft vertex velocities', () => {
  const simulation = world();
  const pointData = { verts: [0, 0, 0, 5, 0, 0], tetEdgeIds: [], tetIds: [], tetSurfaceTriIds: [] };
  const soft = simulation.addSoftBody(pointData, [0, 0, 0], { velocity: [1, 2, 3] });
  soft.predictedVelocities.set(soft.velocities);
  simulation.obstacles.push({ shape: 'sphere', position: [0, 0, 0], radius: 1 });
  simulation.settleObstacleContacts();
  assert.deepEqual(Array.from(soft.positions), [1, 0, 0, 5, 0, 0]);
  assert.deepEqual(Array.from(soft.velocities), [0, 0, 0, 1, 2, 3]);
  assert.deepEqual(Array.from(soft.predictedVelocities), [0, 0, 0, 1, 2, 3]);
  assert.deepEqual(Array.from(soft.previousPositions), Array.from(soft.positions));
});

test('zero timestep editing changes only particles requiring collision correction', () => {
  const simulation = world({ dt: 0 });
  const collided = simulation.addBall(1, [0, 0, 0], [1, 2, 3]);
  const unaffected = simulation.addBall(1, [10, 0, 0], [3, 2, 1]);
  simulation.obstacles.push({ shape: 'sphere', position: [0, 0, 0], radius: 1 });
  assert.equal(simulation.step(), false);
  assert.deepEqual(Array.from(collided.positions), [0, 0, 0]);
  simulation.settleObstacleContacts();
  near(pointDistance(collided.positions, [0, 0, 0]), 2);
  assert.deepEqual(Array.from(collided.velocities), [0, 0, 0]);
  assert.deepEqual(Array.from(unaffected.positions), [10, 0, 0]);
  assert.deepEqual(Array.from(unaffected.velocities), [3, 2, 1]);
});

test('production bunny mesh retains its vertex, edge, tetrahedron, and surface contracts', () => {
  assert.equal(bunnyData.verts.length / 3, 333);
  assert.equal(bunnyData.tetEdgeIds.length / 2, 1907);
  assert.equal(bunnyData.tetIds.length / 4, 1351);
  assert.equal(bunnyData.tetSurfaceTriIds.length / 3, 456);
  for (const ids of [bunnyData.tetEdgeIds, bunnyData.tetIds, bunnyData.tetSurfaceTriIds]) {
    assert.ok(ids.every((index) => Number.isInteger(index) && index >= 0 && index < 333));
  }
  const simulation = world();
  const body = simulation.addSoftBody(bunnyData);
  assert.ok(Array.from(body.restVolumes).every((volume) => volume > 0));
});

for (const mode of ['pbd', 'xpbd']) {
  test(`production bunny preserves its free rest shape under ${mode}`, () => {
    const simulation = world({ mode, dt: 0.03, substeps: 3, solverIterations: 2 });
    const body = simulation.addSoftBody(bunnyData, [0, 0, 0], { velocity: [0.1, 0.2, -0.1] });
    for (let frame = 0; frame < 100; frame += 1) simulation.step();
    for (let index = 0; index < body.positions.length; index += 3) {
      near(body.positions[index], body.restPositions[index] + 0.3, 1e-8);
      near(body.positions[index + 1], body.restPositions[index + 1] + 0.6, 1e-8);
      near(body.positions[index + 2], body.restPositions[index + 2] - 0.3, 1e-8);
    }
    allFinite(body);
  });

  test(`production bunny stress remains finite at dt .1 and ten substeps under ${mode}`, () => {
    const simulation = world({
      mode,
      bounds: 10,
      dt: 0.1,
      substeps: 10,
      solverIterations: 2,
      edgeCompliance: 1e-6,
      volumeCompliance: 1e-8,
    });
    for (const offset of [
      [-0.3, 0, 0],
      [0.3, 0, 0],
      [0, 0, 0.3],
    ]) {
      simulation.addSoftBody(bunnyData, offset, { velocity: [0.1, -0.1, 0.1] });
    }
    simulation.addBall(1, [0, 0, 0], [0.1, 0, 0]);
    simulation.addBall(1, [0.5, 0, 0], [-0.1, 0, 0]);
    simulation.obstacles.push(
      { shape: 'box', position: [0, -3, 0], size: [4, 2, 4] },
      { shape: 'sphere', position: [-4, -3, 0], radius: 1.5 },
    );
    for (let frame = 0; frame < 1000; frame += 1) {
      simulation.step();
      for (const body of simulation.bodies) {
        allFinite(body);
        const limit = body.type === 'ball' ? 10 - body.radius : 10;
        assert.ok(Array.from(body.positions).every((value) => Math.abs(value) <= limit + 1e-10));
      }
    }
  });
}
