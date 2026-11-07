// Run: node --test tests/unit/bumpers.test.mjs
// Bumpers are explicitly powered obstacles; their launch energy is intentional.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
await require('../../src/simulation/physics.js').init();
const { PhysicsWorld } = require('../../src/simulation/physics.js');

function near(actual, expected, epsilon = 1e-10) {
  assert.ok(Math.abs(actual - expected) <= epsilon, `${actual} != ${expected}`);
}

function world(options = {}) {
  return new PhysicsWorld({
    bounds: 100,
    gravity: [0, 0, 0],
    damping: 0,
    restitution: 0,
    dt: 0.1,
    substeps: 1,
    solverIterations: 1,
    ...options,
  });
}

function sphere(options = {}) {
  return {
    shape: 'sphere',
    position: [0, 0, 0],
    radius: 1,
    bumper: true,
    bumperStrength: 4,
    ...options,
  };
}

function contactBall(simulation, inverseMass = 1, velocity = [0, 0, 0]) {
  return simulation.addBall(1, [2, 0, 0], velocity, { inverseMass });
}

function place(ball, position) {
  ball.positions.set(position);
  ball.velocities.fill(0);
}

test('a powered sphere adds its configured launch speed to incoming normal restitution', () => {
  const simulation = world({ restitution: 0.5 });
  const obstacle = sphere();
  simulation.obstacles.push(obstacle);
  const ball = contactBall(simulation, 1, [-1, 0, 0]);
  simulation.step();
  near(ball.velocities[0], 4.5);
  near(ball.velocities[1], 0);
  assert.equal(simulation.bumperHits.length, 1);
  assert.equal(simulation.bumperHits[0].body, ball);
  assert.equal(simulation.bumperHits[0].obstacle, obstacle);
  near(simulation.bumperHits[0].strength, 4);
  assert.ok(Number.isFinite(simulation.bumperHits[0].time));
});

for (const substeps of [1, 3, 10]) {
  test(`one contact entry receives one kick with ${substeps} substeps`, () => {
    const simulation = world({ substeps, solverIterations: 5 });
    simulation.obstacles.push(sphere());
    const ball = contactBall(simulation);
    simulation.step();
    near(ball.velocities[0], 4);
    assert.equal(simulation.bumperHits.length, 1);
    near(simulation.simulationTime, 0.1);
  });
}

test('a box bumper preserves tangential velocity', () => {
  const simulation = world();
  simulation.obstacles.push({
    shape: 'box',
    position: [0, 0, 0],
    size: [2, 2, 2],
    bumper: true,
    bumperStrength: 4,
  });
  const ball = contactBall(simulation, 1, [0, 0.2, 0]);
  simulation.step();
  near(ball.velocities[0], 4);
  near(ball.velocities[1], 0.2);
  assert.equal(simulation.bumperHits.length, 1);
});

test('continuous contact never repeats the entry kick even after cooldown', () => {
  const simulation = world();
  simulation.obstacles.push(sphere());
  const ball = contactBall(simulation);
  simulation.step();
  assert.equal(simulation.bumperHits.length, 1);
  for (let frame = 0; frame < 5; frame += 1) {
    place(ball, [2, 0, 0]);
    simulation.step();
    assert.equal(simulation.bumperHits.length, 0);
    near(ball.velocities[0], 0);
  }
});

test('leaving contact and waiting for cooldown rearms the same obstacle', () => {
  const simulation = world();
  simulation.obstacles.push(sphere());
  const ball = contactBall(simulation);
  simulation.step();
  place(ball, [5, 0, 0]);
  for (let frame = 0; frame < 3; frame += 1) simulation.step();
  place(ball, [2, 0, 0]);
  simulation.step();
  assert.equal(simulation.bumperHits.length, 1);
  near(ball.velocities[0], 4);
});

test('a released contact cannot retrigger before the per body cooldown expires', () => {
  const simulation = world({ dt: 0.01 });
  simulation.obstacles.push(sphere());
  const ball = contactBall(simulation);
  simulation.step();
  place(ball, [5, 0, 0]);
  for (let frame = 0; frame < 5; frame += 1) simulation.step();
  place(ball, [2, 0, 0]);
  simulation.step();
  assert.equal(simulation.bumperHits.length, 0);
  near(ball.velocities[0], 0);
});

test('cooldowns belong to each body and obstacle pair', () => {
  const simulation = world();
  const firstObstacle = sphere();
  const secondObstacle = sphere({ position: [10, 0, 0], bumperStrength: 6 });
  simulation.obstacles.push(firstObstacle, secondObstacle);
  const first = contactBall(simulation);
  const second = simulation.addBall(1, [-2, 0, 0]);
  simulation.step();
  assert.equal(simulation.bumperHits.length, 2);
  near(first.velocities[0], 4);
  near(second.velocities[0], -4);
  place(first, [12, 0, 0]);
  place(second, [-5, 0, 0]);
  simulation.step();
  assert.equal(simulation.bumperHits.length, 1);
  assert.equal(simulation.bumperHits[0].obstacle, secondObstacle);
  near(first.velocities[0], 6);
});

test('a brief contact detection gap does not rearm a cooled bumper', () => {
  const simulation = world({ dt: 0.01 });
  simulation.obstacles.push(sphere());
  const ball = contactBall(simulation);
  simulation.step();
  for (let frame = 0; frame < 30; frame += 1) {
    place(ball, [2, 0, 0]);
    simulation.step();
  }
  place(ball, [5, 0, 0]);
  simulation.step();
  place(ball, [2, 0, 0]);
  simulation.step();
  assert.equal(simulation.bumperHits.length, 0);
  near(ball.velocities[0], 0);
});

test('soft body launch changes only contacted vertices and reports one body event', () => {
  const simulation = world();
  simulation.obstacles.push(sphere());
  const soft = simulation.addSoftBody({
    verts: [1, 0, 0, 0, 1, 0, 3, 0, 0],
    tetEdgeIds: [],
    tetIds: [],
    tetSurfaceTriIds: [],
  });
  simulation.step();
  assert.deepEqual(Array.from(soft.velocities), [4, 0, 0, 0, 4, 0, 0, 0, 0]);
  assert.equal(simulation.bumperHits.length, 1);
});

test('pinned contacts never receive a powered impulse', () => {
  const simulation = world();
  simulation.obstacles.push(sphere());
  const ball = contactBall(simulation, 0);
  simulation.step();
  assert.deepEqual(Array.from(ball.positions), [2, 0, 0]);
  assert.deepEqual(Array.from(ball.velocities), [0, 0, 0]);
  assert.equal(simulation.bumperHits.length, 0);
});

test('paused editing and zero timestep never launch bodies or advance simulation time', () => {
  const simulation = world({ dt: 0 });
  simulation.obstacles.push(sphere());
  const ball = simulation.addBall(1, [0, 0, 0]);
  simulation.settleObstacleContacts();
  near(Math.hypot(...ball.positions), 2);
  assert.deepEqual(Array.from(ball.velocities), [0, 0, 0]);
  assert.equal(simulation.step(), false);
  near(simulation.simulationTime, 0);
  assert.deepEqual(simulation.bumperHits, []);
});

test('reset clears bumper events and cooldowns and rearms contacts', () => {
  const simulation = world();
  simulation.obstacles.push(sphere());
  const ball = contactBall(simulation);
  simulation.step();
  simulation.reset();
  near(simulation.simulationTime, 0);
  assert.deepEqual(simulation.bumperHits, []);
  assert.equal(simulation.obstacles.length, 1);
  simulation.step();
  assert.equal(simulation.bumperHits.length, 1);
  near(ball.velocities[0], 4);
});

for (const strength of [0, -1, NaN, Infinity]) {
  test(`invalid or zero bumper strength ${strength} does not inject energy`, () => {
    const simulation = world();
    simulation.obstacles.push(sphere({ bumperStrength: strength }));
    const ball = contactBall(simulation);
    simulation.step();
    assert.deepEqual(Array.from(ball.velocities), [0, 0, 0]);
    assert.deepEqual(simulation.bumperHits, []);
  });
}

test('bumper strength is capped at the supported maximum', () => {
  const simulation = world();
  simulation.obstacles.push(sphere({ bumperStrength: 100 }));
  const ball = contactBall(simulation);
  simulation.step();
  near(ball.velocities[0], 12);
  near(simulation.bumperHits[0].strength, 12);
});

test('ordinary obstacles keep passive contact behavior', () => {
  const simulation = world();
  simulation.obstacles.push(sphere({ bumper: false }));
  const ball = contactBall(simulation);
  simulation.step();
  assert.deepEqual(Array.from(ball.velocities), [0, 0, 0]);
  assert.deepEqual(simulation.bumperHits, []);
});
