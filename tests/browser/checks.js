// Run this function in DevTools after pbd_model.html is ready.
// Mutates the demonstration scene; reload afterwards.
async function runSoftLabChecks() {
  const app = window.softLab,
    world = app.world,
    results = [];
  const $ = (id) => document.getElementById(id);
  const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const check = (name, condition) => {
    if (!condition) throw new Error(name);
    results.push(name);
  };
  const input = (id, value, type = 'input') => {
    $(id).value = String(value);
    $(id).dispatchEvent(new Event(type, { bubbles: true }));
  };
  app.setRunning(false);
  const state = () => world.bodies.flatMap((body) => Array.from(body.positions));
  const initial = state();
  $('run-button').click();
  // Hidden packaged windows may compose at 1 FPS. Wait for the observable
  // physics result rather than imposing an unrelated wall-clock speed limit.
  const progressDeadline = performance.now() + 20000;
  while (state().every((value, index) => value === initial[index])) {
    if (performance.now() >= progressDeadline)
      throw new Error('Run/Pause physics progress timed out');
    await wait(25);
  }
  $('run-button').click();
  check('Run/Pause advances and pauses', !app.running && state().some((v, i) => v !== initial[i]));
  const paused = state();
  await wait(80);
  check('Paused positions remain unchanged', JSON.stringify(state()) === JSON.stringify(paused));
  input('Slider01', 0);
  $('run-button').click();
  const frozen = state();
  await wait(100);
  check(
    'dt=0 leaves finite positions unchanged',
    world.dt === 0 && JSON.stringify(state()) === JSON.stringify(frozen),
  );
  $('run-button').click();
  input('Slider01', 30);
  input('Slider02', 10);
  input('iterations', 8);
  check(
    'Physics controls update solver',
    world.dt === 0.03 && world.substeps === 10 && world.solverIterations === 8,
  );
  input('Slider02', 3);
  input('iterations', 2);
  $('mode-pbd').click();
  check('PBD mode toggles', world.mode === 'pbd');
  $('mode-xpbd').click();
  check('XPBD mode toggles', world.mode === 'xpbd');
  const obstacle = world.obstacles[0];
  input('obstacle-list', obstacle.identifier, 'change');
  check(
    'Obstacle list selects and opens properties',
    app.selectedObstacle === obstacle && !$('obstacle-properties').hidden,
  );
  check(
    'New obstacles start with the pinball bumper enabled',
    obstacle.bumper && obstacle.bumperStrength === 4,
  );
  $('obstacle-bumper').checked = false;
  $('obstacle-bumper').dispatchEvent(new Event('change'));
  check(
    'Bumper toggle disables powered response and its strength control',
    !obstacle.bumper && $('bumper-strength').disabled,
  );
  $('obstacle-bumper').checked = true;
  $('obstacle-bumper').dispatchEvent(new Event('change'));
  input('bumper-strength', 9);
  check(
    'Bumper strength changes independently from geometry',
    obstacle.bumperStrength === 9 && !$('bumper-strength').disabled,
  );
  const hitsBeforeEdit = world.bumperHits.length,
    timeBeforeEdit = world.simulationTime;
  input('size-x-number', 999);
  check('Size clamps to documented maximum', obstacle.size[0] === 12);
  input('size-x-number', 4);
  const oldSize = obstacle.size.slice();
  check(
    'Obstacle controls offer boxes only',
    !$('add-sphere') &&
      !$('obstacle-shape') &&
      !$('sphere-size') &&
      world.obstacles.every(
        (item) =>
          item.shape === 'box' &&
          !('radius' in item) &&
          app.obstacleViews.get(item).geometry.type === 'BoxGeometry',
      ),
  );
  input('size-y-number', 2.3);
  check(
    'Box dimensions edit independently',
    obstacle.size[1] === 2.3 && obstacle.size[0] === oldSize[0] && obstacle.size[2] === oldSize[2],
  );
  input('position-x-number', 999);
  check(
    'Position clamps to box half extent plus wall clearance',
    obstacle.position[0] === world.bounds - obstacle.size[0] * 0.5 - 2,
  );
  check(
    'Paused obstacle edits never fire a bumper or advance physics time',
    world.bumperHits.length === hitsBeforeEdit && world.simulationTime === timeBeforeEdit,
  );
  const saved = obstacle.position.slice();
  $('restart-button').click();
  check(
    'Restart retains obstacle layout',
    world.obstacles.includes(obstacle) && obstacle.position.every((v, i) => v === saved[i]),
  );
  check(
    'Restart clears velocities',
    world.bodies.every((b) => b.velocities.every((v) => v === 0)),
  );
  check(
    'Restart rearms powered contacts and clears simulation time',
    world.bumperHits.length === 0 && world.simulationTime === 0,
  );
  check(
    'Contact diagnostics are visible',
    !!$('contact-status') && $('contact-status').textContent.includes('후보 검사'),
  );
  $('wireframe').checked = true;
  $('wireframe').dispatchEvent(new Event('change'));
  check(
    'Wireframe changes rendered soft meshes',
    world.softBodies.every((b) => app.bodyViews.get(b).material.wireframe),
  );
  $('gravity').checked = false;
  $('gravity').dispatchEvent(new Event('change'));
  check('Gravity switch updates acceleration', world.gravity[1] === 0);
  app.camera.position.set(20, 4, 1);
  $('camera-reset').click();
  check('Camera reset restores default view', Math.abs(app.camera.position.x - 10.5) < 1e-6);
  while (world.obstacles.length) {
    input('obstacle-list', world.obstacles[0].identifier, 'change');
    $('remove-obstacle').click();
  }
  check(
    'Deleting all obstacles clears selection',
    app.selectedObstacle === null && $('obstacle-properties').hidden,
  );
  for (let i = 0; i < 12; i++) $('add-box').click();
  check(
    'Box obstacle cap is ten with the add button disabled',
    world.obstacles.length === 10 &&
      $('add-box').disabled &&
      world.obstacles.every((item) => item.shape === 'box'),
  );
  while (world.obstacles.length) {
    input('obstacle-list', world.obstacles[0].identifier, 'change');
    $('remove-obstacle').click();
  }
  for (let i = 0; i < 12; i++) {
    $('add-soft').click();
    $('add-ball').click();
  }
  check(
    'Soft and ball object caps are ten',
    world.softBodies.length === 10 &&
      world.balls.length === 10 &&
      $('add-soft').disabled &&
      $('add-ball').disabled,
  );
  check(
    'All state remains finite',
    world.bodies.every(
      (b) => b.positions.every(Number.isFinite) && b.velocities.every(Number.isFinite),
    ),
  );
  return {
    passed: results.length,
    checks: results,
    browser: navigator.userAgent,
    viewport: [innerWidth, innerHeight],
  };
}
