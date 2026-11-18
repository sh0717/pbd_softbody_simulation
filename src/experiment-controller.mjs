const SOFT_COLORS = [0x76dfbe, 0x78b1e2, 0xafa0e7, 0xe5b487];
const MAX_BODIES_PER_KIND = 10;
const MAX_OBSTACLES = 10;
const IMPULSE_SPEED_GAIN = 7;
const IMPULSE_DIRECTIONS = {
  up: [0, 1, 0],
  down: [0, -1, 0],
  left: [-1, 0, 0],
  right: [1, 0, 0],
  forward: [0, 0, 1],
  backward: [0, 0, -1],
};
const DEMO_POSES = {
  drop: {
    softCenters: [
      [-1.5, 3.2, 0],
      [1.6, 4, 0.4],
    ],
    ballCenter: [4.8, 3, 3.6],
    gravity: true,
  },
  collision: {
    softCenters: [
      [-0.45, 1.6, 0],
      [0.45, 1.6, 0.2],
    ],
    ballCenter: [4.8, 3, 3.6],
    gravity: false,
  },
  bumper: {
    softCenters: [
      [-5, 0.9, -2],
      [5, 0.9, -2],
    ],
    gravity: true,
  },
};

/** Owns selection and user commands. PhysicsWorld owns all physical state. */
export class ExperimentController {
  constructor({ world, view, bunnyData, onTimingReset, onRestart, onFatal = () => {} }) {
    this.world = world;
    this.view = view;
    this.bunnyData = bunnyData;
    this.onTimingReset = onTimingReset;
    this.onRestart = onRestart;
    this.onFatal = onFatal;
    this.selectedBody = null;
    this.selectedObstacle = null;
    this.running = false;
    this.nextObstacleId = 1;
    this.ui = null;
    this.picking = null;
    this.disposed = false;
    this.failed = false;

    const names = [
      'addSoft',
      'addBall',
      'addObstacle',
      'editObstacle',
      'selectObstacle',
      'setMode',
      'setRunning',
      'applyImpulse',
      'setMaterial',
      'restart',
      'loadPreset',
      'setTimeStep',
      'setSubsteps',
      'setIterations',
      'setGravity',
      'setBumper',
      'setBumperStrength',
      'removeObstacle',
      'restoreCamera',
      'setWireframe',
    ];

    this.actions = Object.freeze(
      Object.fromEntries(names.map((name) => [name, this[name].bind(this)])),
    );

    const controller = this;

    this.selection = Object.freeze({
      get body() {
        return controller.selectedBody;
      },

      get obstacle() {
        return controller.selectedObstacle;
      },
      selectObstacle: (obstacle) => controller.selectObstacle(obstacle),
      selectBody: (body) => controller.selectBody(body),
      setDragTarget: (value) => controller.setDragTarget(value),
    });
  }

  assertAlive() {
    if (this.disposed) throw new Error('실험이 종료되었습니다.');

    if (this.failed) throw new Error('실험 오류로 실행이 중단되었습니다.');
  }

  // Once the world has changed, a view or UI failure must not leave a
  // partially published experiment running. The app owns final cleanup.
  finishCommittedChange(operation) {
    try {
      return operation();
    } catch (error) {
      this.running = false;
      this.failed = true;

      try {
        this.onFatal(error);
      } finally {
        throw error;
      }
    }
  }

  syncBodies() {
    this.assertAlive();

    for (const body of this.world.bodies) this.view.syncBody(body);
  }

  settle() {
    this.world.settleObstacleContacts();
    this.syncBodies();
  }

  addSoft(offset) {
    this.assertAlive();

    if (this.world.softBodies.length >= MAX_BODIES_PER_KIND) return;

    const body = this.world.addSoftBody(
      this.bunnyData,
      offset || [(Math.random() - 0.5) * 10, 1, (Math.random() - 0.5) * 8],
    );

    this.finishCommittedChange(() => {
      this.view.addBody(
        body,
        SOFT_COLORS[(this.world.softBodies.length - 1) % SOFT_COLORS.length],
        this.ui.isWireframe(),
      );

      if (this.world.obstacles.length) this.settle();

      this.ui.updateCounts();
    });

    return body;
  }

  addBall(position) {
    this.assertAlive();

    if (this.world.balls.length >= MAX_BODIES_PER_KIND) return;

    const body = this.world.addBall(
      1,
      position || [(Math.random() - 0.5) * 10, 3, (Math.random() - 0.5) * 8],
      [0.4, 0, 0.15],
    );

    this.finishCommittedChange(() => {
      this.view.addBody(body, 0xf3be7a, false);

      if (this.world.obstacles.length) this.settle();

      this.ui.updateCounts();
    });

    return body;
  }

  clearBodySelection() {
    if (this.selectedBody) {
      this.selectedBody.dragTarget = null;
      this.view.selectBody(this.selectedBody, false);
    }

    this.selectedBody = null;
  }

  selectObstacle(obstacle) {
    this.assertAlive();
    this.clearBodySelection();
    this.selectedObstacle = obstacle;
    this.view.selectObstacle(obstacle);
    this.ui.syncObstacle();
  }

  selectBody(body) {
    this.selectObstacle(null);
    this.selectedBody = body;
    this.view.selectBody(body, true);
  }

  setDragTarget(value) {
    this.assertAlive();

    if (this.selectedBody) this.selectedBody.dragTarget = value;
  }

  addObstacle(shape = 'box', options = {}) {
    this.assertAlive();

    if (shape !== 'box' || (options.shape !== undefined && options.shape !== 'box')) {
      throw new TypeError('장애물은 Box만 사용할 수 있습니다.');
    }

    if (this.world.obstacles.length >= MAX_OBSTACLES) return;

    const obstacle = this.world.addObstacle({
      identifier: this.nextObstacleId,
      shape,
      position: [0, -3, 0],
      size: [4, 2, 4],
      bumper: true,
      bumperStrength: 4,
      ...options,
    });

    this.nextObstacleId++;
    this.finishCommittedChange(() => {
      this.view.addObstacle(obstacle);
      this.view.syncObstacle(obstacle);
      this.selectObstacle(obstacle);
      this.ui.refreshObstacles();
      this.settle();
    });

    return obstacle;
  }

  editObstacle(name, value) {
    this.assertAlive();

    if (!this.selectedObstacle) return;

    const options = {};

    if (!/^(position|size)-[xyz]$/.test(name)) {
      throw new TypeError('장애물의 위치와 박스 크기만 편집할 수 있습니다.');
    }

    const [field, axis] = name.split('-');

    options[field] = Array.from(this.selectedObstacle[field]);
    options[field]['xyz'.indexOf(axis)] = value;
    this.world.updateObstacle(this.selectedObstacle, options);
    this.finishCommittedChange(() => {
      this.view.syncObstacle(this.selectedObstacle);
      this.ui.syncObstacle();
      this.settle();
    });
  }

  moveObstacle(obstacle, position) {
    this.assertAlive();
    this.world.updateObstacle(obstacle, { position });
    this.finishCommittedChange(() => {
      this.view.syncObstacle(obstacle);
      this.ui.syncObstacle();
      this.settle();
    });
  }

  moveBody(body, position) {
    this.assertAlive();
    this.world.moveBody(body, position);
    this.finishCommittedChange(() => this.settle());
  }

  setMaterial(softness) {
    this.assertAlive();
    this.world.configure({
      edgeCompliance: 10 ** (-8 + softness * 5),
      volumeCompliance: 10 ** (-11 + softness * 6),
      pbdStiffness: 1 - softness * 0.85,
    });
  }

  setMode(mode) {
    this.assertAlive();
    this.world.configure({ mode });
    this.finishCommittedChange(() => {
      this.ui.updateMode();

      for (const body of this.world.softBodies) body.resetLambdas();

      this.ui.updateMaterial();
    });
  }

  setRunning(value) {
    this.assertAlive();
    this.running = value;
    this.finishCommittedChange(() => {
      this.onTimingReset();
      this.ui.updateRunning();
    });
  }

  applyImpulse(direction) {
    this.assertAlive();

    if (!Object.hasOwn(IMPULSE_DIRECTIONS, direction))
      throw new RangeError('알 수 없는 충격량 방향입니다.');

    const axis = IMPULSE_DIRECTIONS[direction];
    const targets = this.selectedBody ? [this.selectedBody] : this.world.bodies;

    this.finishCommittedChange(() => {
      if (this.selectedBody) this.picking.endDrag();

      // 질량이 다른 토끼와 공에 같은 속도 변화를 주도록 충격량을 계산합니다.
      for (const body of targets) {
        const magnitude = body.movableMass * IMPULSE_SPEED_GAIN;
        body.applyImpulse(axis.map((component) => component * magnitude));
      }
    });
  }

  restart() {
    this.assertAlive();
    this.resetMotion();
  }

  resetMotion(stageBodies) {
    this.picking.endDrag();
    this.world.reset();
    this.finishCommittedChange(() => {
      stageBodies?.();
      this.settle();
      this.onTimingReset();
      this.onRestart();
      this.view.resetFlashes();
    });
  }

  loadPreset(name) {
    this.assertAlive();

    if (!Object.hasOwn(DEMO_POSES, name)) throw new RangeError('알 수 없는 실험입니다.');

    const pose = DEMO_POSES[name];
    const [first, second] = this.world.softBodies;
    const [ball] = this.world.balls;

    if (!first || !second || !ball) return false;

    const ballCenter = name === 'bumper' ? this.getBumperDropCenter(ball) : pose.ballCenter;

    if (!ballCenter) return false;

    // Replay starts from native rest poses and keeps the configured solver and box layout.
    this.setRunning(false);
    this.resetMotion(() => {
      this.selectObstacle(null);
      this.setGravity(pose.gravity);
      this.ui.updateGravity();
      this.world.moveBody(first, pose.softCenters[0]);
      this.world.moveBody(second, pose.softCenters[1]);
      this.world.moveBody(ball, ballCenter);
    });

    return true;
  }

  getBumperDropCenter(ball) {
    for (const obstacle of this.world.obstacles) {
      if (!obstacle.bumper || obstacle.bumperStrength <= 0) continue;

      const [x, y, z] = obstacle.position;
      const surface = y + obstacle.size[1] * 0.5;
      const dropHeight = Math.min(2.2, this.world.bounds - 2 * ball.radius - surface);

      if (dropHeight > 0.25) return [x, surface + ball.radius + dropHeight, z];
    }

    return null;
  }

  setTimeStep(value) {
    this.assertAlive();
    this.world.configure({ dt: value });
    this.finishCommittedChange(() => this.onTimingReset());
  }

  setSubsteps(value) {
    this.assertAlive();
    this.world.configure({ substeps: value });
  }

  setIterations(value) {
    this.assertAlive();
    this.world.configure({ solverIterations: value });
  }

  setGravity(enabled) {
    this.assertAlive();
    this.world.configure({ gravity: [0, enabled ? -9.81 : 0, 0] });
  }

  restoreCamera() {
    this.assertAlive();
    this.view.restoreCamera();
  }

  setWireframe(enabled) {
    this.assertAlive();
    this.view.setWireframe(enabled);
  }

  setBumper(value) {
    this.assertAlive();

    if (!this.selectedObstacle) return;

    this.world.updateObstacle(this.selectedObstacle, { bumper: value });
    this.finishCommittedChange(() => this.ui.syncObstacle());
  }

  setBumperStrength(value) {
    this.assertAlive();

    if (!this.selectedObstacle) return;

    this.world.updateObstacle(this.selectedObstacle, { bumperStrength: value });
    this.finishCommittedChange(() => this.ui.syncObstacle());
  }

  removeObstacle() {
    this.assertAlive();

    if (!this.selectedObstacle) return;

    this.picking.endDrag();
    this.world.removeObstacle(this.selectedObstacle);
    this.finishCommittedChange(() => {
      this.view.removeObstacle(this.selectedObstacle);
      this.selectObstacle(this.world.obstacles.at(-1) || null);
      this.ui.refreshObstacles();
    });
  }

  dispose() {
    if (this.disposed) return;

    this.disposed = true;
    this.running = false;
    this.selectedBody = null;
    this.selectedObstacle = null;
    this.ui = null;
    this.picking = null;
    this.world = null;
    this.view = null;
  }
}
