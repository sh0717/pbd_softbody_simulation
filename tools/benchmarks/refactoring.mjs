// Compare the preserved pre-refactor Wasm and current Wasm on identical CPU workloads.
// Capture baseline-runtime/ before changing sources; see docs/refactoring-workflow.md.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { cpus } from 'node:os';
import { performance } from 'node:perf_hooks';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
// This optional experiment consumes a local capture, not a runtime dependency.
const baselineFile = fileURLToPath(
  new URL('../../build/refactoring/baseline-runtime/physics.js', import.meta.url),
);
if (!existsSync(baselineFile)) {
  throw new Error(
    'Local baseline capture required; see docs/refactoring-workflow.md. Use npm run check for public-clone validation.',
  );
}
const before = await require(baselineFile).init();
const after = await require('../../src/simulation/physics.js').init();
const bunny = require('../../src/data/bunny-data.js');
const frames = 120,
  warmupFrames = 60,
  rounds = 3;

function scene(api, count) {
  const world = new api.PhysicsWorld({ dt: 0.03, substeps: 3, solverIterations: 2, mode: 'xpbd' });
  for (let i = 0; i < count; i++) {
    world.addSoftBody(bunny, [((i % 3) - 1) * 0.65, 0, (Math.floor(i / 3) - 0.5) * 0.65], {
      velocity: [0.1, 0.1, 0],
    });
  }
  world.addBall(1, [0, 0, 0], [0.1, 0, 0]);
  world.obstacles = [
    { shape: 'box', position: [0, -3, 0], size: [4, 2, 4] },
    { shape: 'sphere', position: [-4, -3, 0], radius: 1.5 },
  ];
  return world;
}

function measure(api, count) {
  const warmup = scene(api, count);
  try {
    for (let frame = 0; frame < warmupFrames; frame++) warmup.step();
  } finally {
    warmup.dispose();
  }
  const world = scene(api, count),
    samples = [];
  try {
    for (let frame = 0; frame < frames; frame++) {
      const start = performance.now();
      world.step();
      samples.push(performance.now() - start);
    }
    const state = world.bodies.flatMap((body) => [...body.positions, ...body.velocities]);
    assert.ok(state.every(Number.isFinite), 'non-finite simulation state');
    return { samples, state, time: world.simulationTime, stats: { ...world.softContactStats } };
  } finally {
    world.dispose();
  }
}

function distribution(samples) {
  const sorted = samples.slice().sort((a, b) => a - b);
  return {
    medianMs: sorted[Math.floor(sorted.length * 0.5)],
    p95Ms: sorted[Math.floor(sorted.length * 0.95)],
  };
}

const results = [];
for (const count of [1, 2, 6]) {
  const samples = { before: [], after: [] },
    trials = [];
  let maximumStateDifference = 0;
  for (let round = 0; round < rounds; round++) {
    const trial = {};
    for (const name of round % 2 ? ['after', 'before'] : ['before', 'after']) {
      trial[name] = measure(name === 'before' ? before : after, count);
      samples[name].push(...trial[name].samples);
    }
    assert.equal(trial.before.time, trial.after.time);
    assert.deepEqual(trial.before.stats, trial.after.stats, 'contact diagnostics changed');
    assert.equal(trial.before.state.length, trial.after.state.length);
    trial.before.state.forEach((value, index) => {
      const difference = Math.abs(value - trial.after.state[index]);
      maximumStateDifference = Math.max(maximumStateDifference, difference);
      assert.ok(
        difference <= 1e-10 * Math.max(1, Math.abs(value)),
        `state regression: bunnies=${count}, round=${round}, index=${index}, difference=${difference}`,
      );
    });
    trials.push({
      round,
      before: distribution(trial.before.samples),
      after: distribution(trial.after.samples),
    });
  }
  const baseline = distribution(samples.before),
    refactored = distribution(samples.after);
  results.push({
    bunnies: count,
    balls: 1,
    obstacles: 2,
    baseline,
    refactored,
    medianRatio: refactored.medianMs / baseline.medianMs,
    maximumStateDifference,
    diagnosticsEqual: true,
    trials,
  });
}
function sha256(path) {
  return createHash('sha256')
    .update(readFileSync(new URL(path, import.meta.url)))
    .digest('hex');
}
const report = {
  date: new Date().toISOString(),
  runtime: process.version,
  cpu: cpus()[0].model,
  dt: 0.03,
  substeps: 3,
  iterations: 2,
  mode: 'xpbd',
  frames,
  warmupFrames,
  rounds,
  scope:
    'CPU wall time including JS/Wasm bridge, excluding rendering/GPU. Alternating before/after order. Timing is observational. Assertions compare final-frame positions/velocities and final-step contact diagnostics; intermediate frames are not compared.',
  baselineWasmSHA256: sha256('../../build/refactoring/baseline-runtime/wasm/pbd_core.wasm'),
  refactoredWasmSHA256: sha256('../../wasm/pbd_core.wasm'),
  results,
};
writeFileSync(
  new URL('../../tests/artifacts/refactoring-benchmark.json', import.meta.url),
  JSON.stringify(report, null, 2) + '\n',
);
console.log(JSON.stringify(report, null, 2));
