import assert from 'node:assert/strict';
import test from 'node:test';
import { ExperimentController } from '../../src/experiment-controller.mjs';

test('a view failure after a world commit stops commands and reports the original error', () => {
  const failure = new Error('view creation failed');
  const reported = [];
  const world = {
    balls: [],
    bodies: [],
    obstacles: [],

    addBall() {
      const body = {};

      this.balls.push(body);
      this.bodies.push(body);

      return body;
    },
  };
  const view = {
    addBody: () => {
      throw failure;
    },
  };
  const controller = new ExperimentController({
    world,
    view,
    onFatal: (error) => reported.push(error),
  });
  controller.ui = { updateCounts() {} };
  controller.running = true;

  assert.throws(
    () => controller.addBall([0, 0, 0]),
    (error) => error === failure,
  );
  assert.equal(
    world.balls.length,
    1,
    'the native owner must be disposed, not silently rolled back',
  );
  assert.equal(controller.running, false);
  assert.deepEqual(reported, [failure]);
  assert.throws(() => controller.syncBodies(), /실험.*종료|실험.*오류/);
});

test('invalid world input before a commit does not trigger fatal shutdown', () => {
  const reported = [];
  const invalid = new RangeError('invalid position');
  const world = {
    balls: [],
    addBall: () => {
      throw invalid;
    },
  };
  const controller = new ExperimentController({
    world,
    view: {},
    onFatal: (error) => reported.push(error),
  });
  controller.running = true;

  assert.throws(
    () => controller.addBall([NaN, 0, 0]),
    (error) => error === invalid,
  );
  assert.equal(controller.running, true);
  assert.deepEqual(reported, []);

  controller.assertAlive();
});

test('rejected obstacle input does not consume the next identifier', () => {
  const world = {
    obstacles: [],
    addObstacle: () => {
      throw new RangeError('invalid shape');
    },
  };
  const controller = new ExperimentController({ world, view: {} });
  controller.nextObstacleId = 17;

  assert.throws(() => controller.addObstacle('box'), /invalid shape/);
  assert.equal(controller.nextObstacleId, 17);
  assert.equal(controller.failed, false);
});

test('committed body and obstacle changes stop on a view synchronization failure', () => {
  for (const command of ['addSoft', 'addObstacle', 'editObstacle', 'removeObstacle']) {
    const failure = new Error(`${command} view failed`);
    const reported = [];
    const body = {};
    const obstacle = { position: [0, 0, 0], size: [1, 1, 1] };
    const world = {
      softBodies: [],
      obstacles: [],

      addSoftBody() {
        this.softBodies.push(body);

        return body;
      },

      addObstacle() {
        this.obstacles.push(obstacle);

        return obstacle;
      },

      updateObstacle() {},

      removeObstacle() {},
    };
    const failView = () => {
      throw failure;
    };
    const view = {
      addBody: failView,
      addObstacle: failView,
      syncObstacle: failView,
      removeObstacle: failView,
    };
    const controller = new ExperimentController({
      world,
      view,
      onFatal: (error) => reported.push(error),
    });
    controller.ui = { isWireframe: () => false };
    controller.picking = { endDrag() {} };
    controller.selectedObstacle = obstacle;
    controller.running = true;

    const run = {
      addSoft: () => controller.addSoft([0, 0, 0]),
      addObstacle: () => controller.addObstacle('box'),
      editObstacle: () => controller.editObstacle('size-x', 2),
      removeObstacle: () => controller.removeObstacle(),
    }[command];

    assert.throws(run, (error) => error === failure, command);
    assert.equal(controller.running, false, command);
    assert.deepEqual(reported, [failure], command);
    assert.throws(() => controller.assertAlive(), /실험.*오류/, command);
  }
});
