// Paste into DevTools on pbd_model.html, with the tab visible.
// Each check changes the scene. Reload before/after running it.
function runSoftContactScene() {
  const app = window.softLab,
    world = app.world;
  app.setRunning(false);
  world.gravity.fill(0);

  for (const obstacle of world.obstacles) obstacle.bumper = false;

  world.balls[0].positions.set([8, 8, 8]);
  world.softBodies.forEach((body, index) => {
    // Deliberate solid overlap, clear of the static obstacles and the parked ball.
    world.moveBody(body, [index ? 0.3 : -0.3, 3, 0]);
    body.velocities.fill(0);

    for (let i = 0; i < body.positions.length; i += 3) body.velocities[i] = index ? -0.3 : 0.3;

    body.updateBounds();
  });

  let peakContacts = 0,
    peakCandidates = 0,
    totalProjections = 0;
  const samples = [];

  for (let frame = 0; frame < 60; frame++) {
    const start = performance.now();

    world.step();
    samples.push(performance.now() - start);
    peakContacts = Math.max(peakContacts, world.softContactStats.contacts || 0);
    peakCandidates = Math.max(peakCandidates, world.softContactStats.candidateTests || 0);
    totalProjections += world.softContactStats.contacts || 0;
  }

  app.syncBodies();
  samples.sort((a, b) => a - b);

  return {
    frames: 60,
    dt: world.dt,
    substeps: world.substeps,
    iterations: world.solverIterations,
    gravity: Array.from(world.gravity),
    peakContacts,
    peakCandidates,
    totalProjections,
    medianPhysicsMs: samples[30],
    finite: world.bodies.every((body) => body.positions.every(Number.isFinite)),
  };
}

async function runBumperVisualCheck() {
  const app = window.softLab,
    world = app.world;
  app.setRunning(false);
  document.getElementById('restart-button').click();
  // Move other bodies through the owning world instead of replacing adapter arrays.
  world.softBodies.forEach((body, i) => world.moveBody(body, [i ? 6 : -6, 5, -6]));

  try {
    const ball = world.balls[0],
      obstacle = world.obstacles[0];
    ball.positions.set([0, -1.4, 0]);
    ball.velocities.set([0, -1, 0]);
    app.syncBodies();
    app.setRunning(true);
    await new Promise((resolve) => setTimeout(resolve, 70));
    app.setRunning(false);

    return {
      launched: ball.velocities[1] > 3,
      velocity: Array.from(ball.velocities),
      visualFlash: app.obstacleViews.get(obstacle).userData.flashUntil > performance.now(),
      strength: obstacle.bumperStrength,
      obstaclePosition: Array.from(obstacle.position),
      simulationTime: world.simulationTime,
      finite: ball.positions.every(Number.isFinite),
    };
  } finally {
    app.setRunning(false);
    document.getElementById('restart-button').click();
  }
}
