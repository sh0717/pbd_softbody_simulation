// Compare an explicit preserved runtime against the rebuilt deployment.
// Numerical assertions and observational CPU timing are intentionally separate.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { createRequire } from 'node:module';
import { arch, cpus, platform } from 'node:os';
import path from 'node:path';
import { performance } from 'node:perf_hooks';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../../', import.meta.url));
const require = createRequire(import.meta.url);
const options = new Map(
  process.argv.slice(2).map((argument) => {
    const separator = argument.indexOf('=');
    assert.ok(separator > 2, 'Use --baseline=build/<capture> and --frames=<positive integer>');
    return [argument.slice(2, separator), argument.slice(separator + 1)];
  }),
);
for (const key of options.keys()) {
  assert.ok(
    ['baseline', 'output', 'frames', 'warmup', 'rounds', 'equivalence-frames'].includes(key),
    `Unknown option: ${key}`,
  );
}
assert.ok(options.get('baseline'), 'A preserved --baseline=<directory> is required');
function positiveInteger(name, fallback) {
  const value = Number(options.get(name) ?? fallback);
  assert.ok(Number.isSafeInteger(value) && value > 0, `Invalid --${name}`);
  return value;
}
const frames = positiveInteger('frames', 120);
const warmupFrames = positiveInteger('warmup', 60);
const rounds = positiveInteger('rounds', 5);
const equivalenceFrames = positiveInteger('equivalence-frames', 60);
const baseline = path.resolve(root, options.get('baseline'));
const runtime = path.join(baseline, 'runtime');
const output = path.resolve(root, options.get('output') ?? 'artifacts/runtime-comparison.json');
const sha256 = (bytes) => createHash('sha256').update(bytes).digest('hex');
const manifest = JSON.parse(await readFile(path.join(baseline, 'manifest.json'), 'utf8'));
assert.equal(
  sha256(await readFile(path.join(baseline, 'sources.zip'))),
  manifest.archiveSha256,
  'Baseline source archive changed',
);
const runtimeFiles = manifest.files.filter(({ path: file }) => /^(src|wasm)\//.test(file));
assert.ok(runtimeFiles.length > 0, 'Baseline must describe src/ and wasm/ files');
for (const { path: file, sha256: expected } of runtimeFiles) {
  assert.ok(!path.isAbsolute(file) && !file.split(/[\\/]/).includes('..'), 'Invalid manifest path');
  assert.equal(
    sha256(await readFile(path.join(runtime, file))),
    expected,
    `Baseline changed: ${file}`,
  );
}
const observedFiles = [
  'src/simulation/physics.js',
  'src/data/bunny-data.js',
  'wasm/pbd_core.js',
  'wasm/pbd_core.wasm',
  'wasm/build-manifest.json',
];
async function runtimeHashes(directory) {
  return Object.fromEntries(
    await Promise.all(
      observedFiles.map(async (file) => [file, sha256(await readFile(path.join(directory, file)))]),
    ),
  );
}
const loadedHashes = { before: await runtimeHashes(runtime), after: await runtimeHashes(root) };
const beforeApi = await require(path.join(runtime, 'src/simulation/physics.js')).init();
const afterApi = await require('../../src/simulation/physics.js').init();
const bunny = require('../../src/data/bunny-data.js');
assert.deepEqual(bunny, require(path.join(runtime, 'src/data/bunny-data.js')));

const scenarios = [
  { name: 'ball-and-ten-obstacles', softBodies: 0, balls: 1, obstacles: 10 },
  { name: 'single-soft-body', softBodies: 1, balls: 0, obstacles: 2 },
  { name: 'two-soft-bodies-and-ball', softBodies: 2, balls: 1, obstacles: 2 },
  { name: 'six-soft-bodies-and-ball', softBodies: 6, balls: 1, obstacles: 2 },
  { name: 'six-soft-bodies-only', softBodies: 6, balls: 0, obstacles: 2 },
];
const settings = { dt: 0.03, substeps: 3, solverIterations: 2, gravity: [0, -9.81, 0] };
function createScene(api, scenario, mode) {
  const world = new api.PhysicsWorld({ ...settings, mode });
  try {
    for (let index = 0; index < scenario.softBodies; index++) {
      world.addSoftBody(
        bunny,
        [((index % 3) - 1) * 0.65, 0, (Math.floor(index / 3) - 0.5) * 0.65],
        { velocity: [0.1, 0.1, 0] },
      );
    }
    if (scenario.balls) world.addBall(1, [0, 0, 0], [0.1, 0, 0]);
    for (let index = 0; index < scenario.obstacles; index++) {
      world.addObstacle({
        shape: index % 2 ? 'sphere' : 'box',
        position: index < 2 ? [index ? -4 : 0, -3, 0] : [(index % 5) * 2 - 4, -6, 4],
        size: [4, 2, 4],
        radius: 1.5,
        bumper: index % 2 === 0,
        bumperStrength: 4,
      });
    }
    return world;
  } catch (error) {
    world.dispose();
    throw error;
  }
}

const particleFields = [
  'positions',
  'restPositions',
  'previousPositions',
  'velocities',
  'predictedVelocities',
  'inverseMass',
  'center',
  'boundsMin',
  'boundsMax',
];
const softFields = [
  'edgeIndices',
  'tetIndices',
  'surfaceIndices',
  'restLengths',
  'restVolumes',
  'edgeLambdas',
  'volumeLambdas',
  'tetCorrectionLimits',
];
function compareState(before, after) {
  assert.equal(after.bodies.length, before.bodies.length);
  for (let index = 0; index < before.bodies.length; index++) {
    const previous = before.bodies[index];
    const current = after.bodies[index];
    assert.equal(current.type, previous.type);
    const fields = previous.type === 'soft' ? [...particleFields, ...softFields] : particleFields;
    for (const field of fields) {
      const a = previous[field];
      const b = current[field];
      assert.equal(b.length, a.length, field);
      for (let element = 0; element < a.length; element++) {
        assert.ok(Number.isFinite(a[element]) && Number.isFinite(b[element]), `${field}: finite`);
        assert.equal(b[element], a[element], `body ${index} ${field}[${element}]`);
      }
    }
  }
  const hits = (world) =>
    world.bumperHits.map(({ body, obstacle, ...hit }) => ({
      ...hit,
      body: world.bodies.indexOf(body),
      obstacle: world.obstacles.indexOf(obstacle),
    }));
  assert.deepEqual(after.softContactStats, before.softContactStats);
  assert.deepEqual(hits(after), hits(before));
  assert.equal(after.simulationTime, before.simulationTime);
}
function editScene(world, frame) {
  // Exercise both explicit commands and the retained writable compatibility mirror.
  if (frame === 10) world.configure({ damping: 0.03 });
  if (frame === 20) world.obstacles[0].position[0] += 0.25;
  if (frame === 30) world.updateObstacle(world.obstacles[1], { radius: 1.25 });
  if (frame === 40) world.gravity[0] = 0.1;
  if (frame === 45) world.reset();
}
const equivalence = [];
for (const mode of ['pbd', 'xpbd']) {
  for (const scenario of scenarios) {
    const before = createScene(beforeApi, scenario, mode);
    const after = createScene(afterApi, scenario, mode);
    let observedBumperHits = 0;
    try {
      compareState(before, after);
      for (let frame = 0; frame < equivalenceFrames; frame++) {
        editScene(before, frame);
        editScene(after, frame);
        assert.equal(
          after.step(frame === 25 ? 0 : settings.dt),
          before.step(frame === 25 ? 0 : settings.dt),
        );
        compareState(before, after);
        if (frame !== 25) observedBumperHits += after.bumperHits.length;
      }
      equivalence.push({
        ...scenario,
        mode,
        frames: equivalenceFrames,
        maximumDifference: 0,
        observedBumperHits,
      });
      console.log(`Equivalent: ${mode}/${scenario.name} (${equivalenceFrames} frames)`);
    } finally {
      before.dispose();
      after.dispose();
    }
  }
}

function measure(api, scenario) {
  const warmup = createScene(api, scenario, 'xpbd');
  try {
    for (let frame = 0; frame < warmupFrames; frame++) warmup.step();
  } finally {
    warmup.dispose();
  }
  const world = createScene(api, scenario, 'xpbd');
  const samples = [];
  try {
    for (let frame = 0; frame < frames; frame++) {
      const start = performance.now();
      world.step();
      samples.push(performance.now() - start);
    }
    for (const body of world.bodies) {
      assert.ok(body.positions.every(Number.isFinite) && body.velocities.every(Number.isFinite));
    }
    return samples;
  } finally {
    world.dispose();
  }
}
function distribution(samples) {
  const sorted = samples.slice().sort((a, b) => a - b);
  return {
    medianMs: sorted[Math.floor(sorted.length * 0.5)],
    p95Ms: sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * 0.95))],
  };
}
const timing = [];
for (const scenario of scenarios) {
  const samples = { before: [], after: [] };
  const trials = [];
  for (let round = 0; round < rounds; round++) {
    const trial = {};
    for (const name of round % 2 ? ['after', 'before'] : ['before', 'after']) {
      const measured = measure(name === 'before' ? beforeApi : afterApi, scenario);
      samples[name].push(...measured);
      trial[name] = distribution(measured);
    }
    trials.push(trial);
  }
  const before = distribution(samples.before);
  const after = distribution(samples.after);
  const result = {
    ...scenario,
    before,
    after,
    medianChangePercent: (after.medianMs / before.medianMs - 1) * 100,
    trials,
    samplesMs: samples,
  };
  timing.push(result);
  console.log(
    `${scenario.name}: ${before.medianMs.toFixed(3)} -> ${after.medianMs.toFixed(3)} ms (${result.medianChangePercent.toFixed(2)}%)`,
  );
}
assert.deepEqual(
  await runtimeHashes(runtime),
  loadedHashes.before,
  'Baseline changed during comparison',
);
assert.deepEqual(
  await runtimeHashes(root),
  loadedHashes.after,
  'Current runtime changed during comparison',
);
const report = {
  createdAt: new Date().toISOString(),
  node: process.version,
  platform: `${platform()} ${arch()}`,
  cpu: cpus()[0]?.model,
  loadedHashes,
  meshSha256: sha256(await readFile(path.join(root, 'src/data/bunny-data.js'))),
  beforeBuild: JSON.parse(await readFile(path.join(runtime, 'wasm/build-manifest.json'), 'utf8')),
  afterBuild: JSON.parse(await readFile(path.join(root, 'wasm/build-manifest.json'), 'utf8')),
  settings,
  frames,
  warmupFrames,
  rounds,
  scope:
    'CPU wall time of world.step including JS/Wasm bridge; excludes rendering, GPU, creation, and teardown. Alternating trial order; timing is observational, not an assertion.',
  baselineArchiveSha256: manifest.archiveSha256,
  beforeWasmSha256: sha256(await readFile(path.join(runtime, 'wasm/pbd_core.wasm'))),
  afterWasmSha256: sha256(await readFile(path.join(root, 'wasm/pbd_core.wasm'))),
  beforeAdapterSha256: sha256(await readFile(path.join(runtime, 'src/simulation/physics.js'))),
  afterAdapterSha256: sha256(await readFile(path.join(root, 'src/simulation/physics.js'))),
  equivalence,
  timing,
};
await mkdir(path.dirname(output), { recursive: true });
await writeFile(output, `${JSON.stringify(report, null, 2)}\n`);
console.log(`Report: ${path.relative(root, output)}`);
