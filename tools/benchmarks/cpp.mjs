// Compare the same local-contact algorithm in the preserved JS core and production Wasm.
import { createRequire } from 'node:module';
import { performance } from 'node:perf_hooks';
import { cpus } from 'node:os';
import { writeFileSync } from 'node:fs';
const require = createRequire(import.meta.url);
const wasm = await require('../../src/simulation/physics.js').init();
const reference = require('../../tests/fixtures/physics-reference.js');
const bunny = require('../../src/data/bunny-data.js');
const frames = 120,
  warmupFrames = 60;
function scene(api, count) {
  const world = new api.PhysicsWorld({ dt: 0.03, substeps: 3, solverIterations: 2, mode: 'xpbd' });
  for (let i = 0; i < count; i++)
    world.addSoftBody(bunny, [((i % 3) - 1) * 0.65, 0, (Math.floor(i / 3) - 0.5) * 0.65], {
      velocity: [0.1, 0.1, 0],
    });
  world.addBall(1, [0, 0, 0], [0.1, 0, 0]);
  world.obstacles = [
    { shape: 'box', position: [0, -3, 0], size: [4, 2, 4] },
    { shape: 'sphere', position: [-4, -3, 0], radius: 1.5 },
  ];
  return world;
}
function measure(api, backend, count) {
  const warmup = scene(api, count);
  for (let i = 0; i < warmupFrames; i++) warmup.step();
  warmup.dispose?.();
  const world = scene(api, count),
    samples = [];
  for (let i = 0; i < frames; i++) {
    const start = performance.now();
    world.step();
    samples.push(performance.now() - start);
  }
  samples.sort((a, b) => a - b);
  let maxEdgeError = 0,
    volumeError = 0,
    volumeCount = 0;
  for (const body of world.softBodies) {
    const p = body.positions,
      edges = body.edgeIndices,
      lengths = body.restLengths,
      tets = body.tetIndices,
      volumes = body.restVolumes;
    for (let e = 0; e < lengths.length; e++) {
      const a = edges[e * 2] * 3,
        b = edges[e * 2 + 1] * 3;
      maxEdgeError = Math.max(
        maxEdgeError,
        Math.abs(Math.hypot(p[a] - p[b], p[a + 1] - p[b + 1], p[a + 2] - p[b + 2]) - lengths[e]),
      );
    }
    for (let t = 0; t < volumes.length; t++) {
      volumeError += Math.abs(
        reference.tetraVolume(p, ...tets.slice(t * 4, t * 4 + 4)) - volumes[t],
      );
      volumeCount++;
    }
  }
  const result = {
    backend,
    bunnies: count,
    balls: 1,
    obstacles: 2,
    medianMs: samples[60],
    p95Ms: samples[114],
    finite: world.bodies.every(
      (b) => b.positions.every(Number.isFinite) && b.velocities.every(Number.isFinite),
    ),
    maxEdgeError,
    meanAbsoluteVolumeError: volumeError / volumeCount,
  };
  world.dispose?.();
  return result;
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
  scope:
    'Node CPU wall time, includes JS/Wasm bridge, excludes rendering. Same initial scene and contact algorithm. Floating point differences can diverge trajectories.',
  results: [1, 2, 6].flatMap((count) => [
    measure(reference, 'JavaScript reference', count),
    measure(wasm, 'C++20 / Wasm', count),
  ]),
};
writeFileSync(
  new URL('../../tests/artifacts/cpp-benchmark.json', import.meta.url),
  JSON.stringify(report, null, 2) + '\n',
);
console.log(JSON.stringify(report, null, 2));
if (report.results.some((r) => !r.finite)) process.exitCode = 1;
