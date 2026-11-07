import assert from 'node:assert/strict';
import test from 'node:test';
import { createRequire } from 'node:module';
import { ExperimentController } from '../../src/experiment-controller.mjs';

const require = createRequire(import.meta.url);
const api = await require('../../src/simulation/physics.js').init();
const tetra = {
  verts: [0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1],
  tetIds: [0, 1, 2, 3],
};

function fixture() {
  const world = new api.PhysicsWorld({ gravity: [0, 0, 0] });
  const calls = [];
  let timingResets = 0;
  let restarts = 0;
  const view = Object.fromEntries(
    [
      'syncBody',
      'addBody',
      'selectBody',
      'selectObstacle',
      'addObstacle',
      'syncObstacle',
      'replaceObstacleShape',
      'removeObstacle',
      'resetFlashes',
      'restoreCamera',
      'setWireframe',
    ].map((name) => [name, (...args) => calls.push([name, ...args])]),
  );
  const ui = {
    isWireframe: () => false,
    ...Object.fromEntries(
      [
        'updateCounts',
        'syncObstacle',
        'refreshObstacles',
        'updateMode',
        'updateMaterial',
        'updateRunning',
      ].map((name) => [name, () => calls.push([name])]),
    ),
  };
  const controller = new ExperimentController({
    world,
    view,
    bunnyData: tetra,
    onTimingReset: () => timingResets++,
    onRestart: () => restarts++,
  });
  controller.ui = ui;
  controller.picking = { endDrag: () => calls.push(['endDrag']) };
  return {
    controller,
    world,
    calls,
    get timingResets() {
      return timingResets;
    },
    get restarts() {
      return restarts;
    },
    dispose() {
      controller.dispose();
      world.dispose();
    },
  };
}

test('bound commands keep body and obstacle selection exclusive and release the previous drag', () => {
  const f = fixture();
  try {
    const { addSoft, addObstacle } = f.controller.actions;
    const body = addSoft([0, 0, 0]);
    f.controller.selection.selectBody(body);
    f.controller.selection.setDragTarget([1, 2, 3]);
    assert.deepEqual(body.dragTarget, [1, 2, 3]);
    const obstacle = addObstacle('box', { position: [5, 5, 5], size: [1, 1, 1] });
    assert.equal(f.controller.selection.body, null);
    assert.equal(f.controller.selection.obstacle, obstacle);
    assert.equal(body.dragTarget, null);
    f.controller.selection.selectBody(body);
    assert.equal(f.controller.selection.obstacle, null);
    assert.equal(f.controller.selection.body, body);
  } finally {
    f.dispose();
  }
});

test('experiment commands reject non-box obstacles without changing the experiment', () => {
  const f = fixture();
  try {
    const box = f.controller.addObstacle('box');
    const nextId = f.controller.nextObstacleId;
    f.calls.length = 0;
    for (const add of [
      () => f.controller.actions.addObstacle('sphere'),
      () => f.controller.actions.addObstacle('box', { shape: 'sphere' }),
    ]) {
      assert.throws(add, TypeError);
      assert.deepEqual(f.world.obstacles, [box]);
      assert.equal(f.controller.nextObstacleId, nextId);
      assert.equal(f.controller.selection.obstacle, box);
      assert.equal(f.calls.length, 0);
    }
    assert.equal(f.controller.actions.setObstacleShape, undefined);
    for (const name of ['radius', 'shape', 'size-q', 'size-x-extra']) {
      assert.throws(() => f.controller.actions.editObstacle(name, 2), TypeError);
    }
    assert.equal(f.calls.length, 0);
  } finally {
    f.dispose();
  }
});

test('a rejected obstacle edit preserves physics and the displayed geometry', () => {
  const f = fixture();
  try {
    const obstacle = f.controller.addObstacle('box', { position: [5, 5, 5], size: [1, 1, 1] });
    const before = JSON.stringify(obstacle);
    f.calls.length = 0;
    assert.throws(() => f.controller.actions.editObstacle('size-x', -1), TypeError);
    assert.equal(JSON.stringify(obstacle), before);
    assert.equal(
      f.calls.length,
      0,
      'a rejected edit must not publish a new view or settle a different state',
    );
    f.controller.actions.editObstacle('size-x', 2);
    assert.equal(obstacle.size[0], 2);
    assert.ok(f.calls.some(([name, value]) => name === 'syncObstacle' && value === obstacle));
  } finally {
    f.dispose();
  }
});

test('time commands reset only accepted scheduling changes; restart preserves edited obstacles', () => {
  const f = fixture();
  try {
    const body = f.controller.addSoft([0, 0, 0]);
    const obstacle = f.controller.addObstacle('box', { position: [5, 5, 5], size: [1, 1, 1] });
    f.controller.actions.editObstacle('size-x', 2);
    assert.throws(() => f.controller.actions.setTimeStep(-1), RangeError);
    assert.equal(f.timingResets, 0);
    f.controller.actions.setTimeStep(0);
    assert.equal(f.world.dt, 0);
    assert.equal(f.timingResets, 1);
    f.controller.actions.setRunning(true);
    assert.equal(f.controller.running, true);
    assert.equal(f.timingResets, 2);
    f.world.moveBody(body, [0, 2, 0]);
    f.controller.actions.restart();
    assert.deepEqual(Array.from(body.positions), tetra.verts);
    assert.equal(f.world.simulationTime, 0);
    assert.equal(obstacle.size[0], 2);
    assert.equal(f.world.obstacles[0], obstacle);
    assert.equal(f.timingResets, 3);
    assert.equal(f.restarts, 1);
  } finally {
    f.dispose();
  }
});

test('retained command callbacks reject after repeated controller disposal', () => {
  const f = fixture();
  const actions = f.controller.actions;
  try {
    f.controller.addSoft([0, 0, 0]);
    const count = f.world.bodies.length;
    f.controller.dispose();
    f.controller.dispose();
    assert.equal(f.controller.selection.body, null);
    assert.equal(f.controller.selection.obstacle, null);
    for (const action of [
      () => actions.addBall(),
      () => actions.setRunning(true),
      () => actions.restart(),
      () => actions.setMode('pbd'),
    ])
      assert.throws(action, /종료/);
    assert.equal(f.world.bodies.length, count);
  } finally {
    f.dispose();
  }
});
