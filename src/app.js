import { PhysicsScene } from './rendering/scene.js';
import { ControlPanel } from './ui/controls.js';
import { PickingController } from './ui/picking.js';
import { ExperimentController } from './experiment-controller.mjs';
import { runCleanup } from './lifecycle.mjs';

const MAX_FRAME_ELAPSED_SECONDS = 0.1;
const MAX_CATCH_UP_STEPS = 6;
const STEP_TOLERANCE_SECONDS = 1e-12;
const STATUS_SAMPLE_INTERVAL_MS = 500;
const PHYSICS_HISTORY_WEIGHT = 0.9;
const PHYSICS_SAMPLE_WEIGHT = 0.1;

let cleanup = () => {};
let failed = false;
function fail(error) {
  if (failed) return;
  failed = true;
  try {
    cleanup();
  } catch (cleanupError) {
    console.error(cleanupError);
  }
  document.getElementById('error-banner').hidden = false;
  document.getElementById('error-banner').textContent =
    `실행 오류: ${error.message}. 페이지를 다시 불러와 주세요.`;
  document.getElementById('simulation-status').textContent = '실행할 수 없습니다.';
  console.error(error);
}

// App owns construction, frame scheduling and disposal; ExperimentController owns user commands.
async function start() {
  const $ = (id) => document.getElementById(id);
  const { THREE, PBDPhysics, PBD_BUNNY_DATA } = window;
  if (!THREE || !PBDPhysics || !PBD_BUNNY_DATA) throw new Error('필수 파일을 불러오지 못했습니다');
  $('simulation-status').textContent = '물리 엔진을 불러오는 중입니다.';
  const coreOptions = {};
  // 배포 번들은 Boids처럼 Wasm을 내장하여 file://에서도 네트워크 요청 없이 초기화합니다.
  // 빌드하지 않은 개발 원본에는 이 상수가 없으므로 기존 HTTP 로딩 경로를 사용합니다.
  if (typeof PBD_CORE_WASM_BASE64 === 'string') {
    coreOptions.wasmBinary = Uint8Array.from(atob(PBD_CORE_WASM_BASE64), (character) =>
      character.charCodeAt(0),
    );
  }
  await PBDPhysics.init(coreOptions);
  let world;
  let view;
  let ui;
  let picking;
  let experiment;
  let animationFrame = 0;
  let disposed = false;
  const events = new AbortController();
  cleanup = dispose;
  world = new PBDPhysics.PhysicsWorld({
    dt: 1 / 60,
    substeps: 3,
    solverIterations: 2,
    mode: 'xpbd',
    bounds: 10,
    gravity: [0, -9.81, 0],
    restitution: 0.25,
    damping: 0.25,
  });
  // Scaled rest geometry is shared by simulation and rendering.
  const bunnyData = { ...PBD_BUNNY_DATA, verts: PBD_BUNNY_DATA.verts.map((value) => value * 3) };
  view = new PhysicsScene({
    THREE,
    viewport: $('viewport'),
    bounds: world.bounds,
    bunnyData,
  });
  let accumulatedSeconds = 0;
  let previousFrameMs = null;
  let physicsMs = 0;
  let lastStatusUpdateMs = 0;
  let fpsSampleStartMs = 0;
  let fpsFrames = 0;
  let fps = 0;
  let droppedSteps = 0;
  let bumperHitCount = 0;

  experiment = new ExperimentController({
    world,
    view,
    bunnyData,
    onFatal: fail,
    onTimingReset: () => {
      accumulatedSeconds = 0;
    },
    onRestart: () => {
      droppedSteps = 0;
      bumperHitCount = 0;
    },
  });
  const getState = () => ({
    selectedBody: experiment.selectedBody,
    selectedObstacle: experiment.selectedObstacle,
    running: experiment.running,
    fps,
    physicsMs,
    droppedSteps,
    bumperHitCount,
  });
  ui = new ControlPanel({
    world,
    getState,
    obstacleLimits: (obstacle) => world.getObstacleLimits(obstacle),
    actions: experiment.actions,
  });
  experiment.ui = ui;
  picking = new PickingController({
    THREE,
    view,
    world,
    isRunning: () => experiment.running,
    selection: experiment.selection,
    onObstacleMoved: (obstacle, position) => experiment.moveObstacle(obstacle, position),
    onBodyMoved: (body, position) => experiment.moveBody(body, position),
  });
  experiment.picking = picking;
  view.renderer.domElement.addEventListener(
    'webglcontextlost',
    (event) => {
      event.preventDefault();
      fail(new Error('WebGL 컨텍스트를 잃었습니다'));
    },
    { signal: events.signal },
  );
  document.addEventListener(
    'visibilitychange',
    () => {
      accumulatedSeconds = 0;
      previousFrameMs = null;
    },
    { signal: events.signal },
  );

  experiment.addObstacle('box', { position: [0, -3, 0], size: [7, 1.2, 4.8] });
  experiment.addObstacle('box', { position: [-4.2, -4.8, -2.5], size: [3.2, 3.2, 3.2] });
  experiment.addSoft([-2, -2.25, 0]);
  experiment.addSoft([2.2, -2.25, 0.5]);
  experiment.addBall([0.5, 2.3, 1.4]);
  experiment.selectObstacle(null);
  ui.updateMaterial();
  ui.updateStatus();

  function stepPhysics(nowMs) {
    const startedMs = performance.now();
    world.step();
    for (const hit of world.bumperHits) {
      view.flashObstacle(hit.obstacle, nowMs);
      bumperHitCount++;
    }
    const elapsedMs = performance.now() - startedMs;
    physicsMs = physicsMs
      ? physicsMs * PHYSICS_HISTORY_WEIGHT + elapsedMs * PHYSICS_SAMPLE_WEIGHT
      : elapsedMs;
  }

  // Scheduling is measured in seconds; requestAnimationFrame and status samples use ms.
  // Skipped steps are counted, while substep/integrator order remains inside PhysicsWorld.
  function advancePhysics(nowMs) {
    const elapsedSeconds =
      previousFrameMs === null
        ? 0
        : Math.min((nowMs - previousFrameMs) / 1000, MAX_FRAME_ELAPSED_SECONDS);
    previousFrameMs = nowMs;
    if (!experiment.running || world.dt === 0) {
      accumulatedSeconds = 0;
      return;
    }
    accumulatedSeconds += elapsedSeconds;
    let steps = 0;
    while (accumulatedSeconds + STEP_TOLERANCE_SECONDS >= world.dt && steps < MAX_CATCH_UP_STEPS) {
      stepPhysics(nowMs);
      accumulatedSeconds -= world.dt;
      steps++;
    }
    if (steps === MAX_CATCH_UP_STEPS && accumulatedSeconds >= world.dt) {
      droppedSteps += Math.floor(accumulatedSeconds / world.dt);
      accumulatedSeconds %= world.dt;
    }
    if (steps) experiment.syncBodies();
  }

  function updateFrameStatistics(nowMs) {
    fpsFrames++;
    if (nowMs - fpsSampleStartMs >= STATUS_SAMPLE_INTERVAL_MS) {
      fps = (fpsFrames * 1000) / Math.max(1, nowMs - fpsSampleStartMs);
      fpsSampleStartMs = nowMs;
      fpsFrames = 0;
    }
    if (nowMs - lastStatusUpdateMs >= STATUS_SAMPLE_INTERVAL_MS) {
      ui.updateStatus();
      lastStatusUpdateMs = nowMs;
    }
  }

  function frame(nowMs) {
    if (disposed) return;
    try {
      advancePhysics(nowMs);
      view.render(nowMs, experiment.selectedObstacle);
      updateFrameStatistics(nowMs);
      animationFrame = requestAnimationFrame(frame);
    } catch (error) {
      fail(error);
    }
  }
  function dispose() {
    if (disposed) return;
    disposed = true;
    runCleanup([
      () => cancelAnimationFrame(animationFrame),
      () => events.abort(),
      () => picking?.dispose(),
      () => ui?.dispose(),
      () => experiment?.dispose(),
      () => view?.dispose(),
      () => world?.dispose(),
    ]);
  }
  // Preserve the public diagnostic surface for repeatable browser checks.
  window.softLab = {
    world,
    scene: view.scene,
    camera: view.camera,
    renderer: view.renderer,
    controls: view.controls,
    bodyViews: view.bodyViews,
    obstacleViews: view.obstacleViews,
    addSoft: (offset) => experiment.addSoft(offset),
    addBall: (position) => experiment.addBall(position),
    addObstacle: (shape, options) => experiment.addObstacle(shape, options),
    setMode: (mode) => experiment.setMode(mode),
    setRunning: (value) => experiment.setRunning(value),
    syncBodies: () => experiment.syncBodies(),
    restoreCamera: () => view.restoreCamera(),
    dispose,
    get running() {
      return experiment.running;
    },
    get selectedBody() {
      return experiment.selectedBody;
    },
    get selectedObstacle() {
      return experiment.selectedObstacle;
    },
    get droppedSteps() {
      return droppedSteps;
    },
  };
  window.addEventListener(
    'pagehide',
    (event) => {
      if (!event.persisted) dispose();
    },
    { signal: events.signal },
  );
  animationFrame = requestAnimationFrame(frame);
}

start().catch(fail);
