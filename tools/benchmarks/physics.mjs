// Native solver CPU timing. Excludes renderer, browser, CDN load, and Python/JS FFI.
import { createRequire } from 'node:module';
import { performance } from 'node:perf_hooks';
import { cpus, platform, arch } from 'node:os';

const require = createRequire(import.meta.url);
await require('../../src/simulation/physics.js').init();
const { PhysicsWorld, tetraVolume } = require('../../src/simulation/physics.js');
const bunnyData = require('../../src/data/bunny-data.js');
const frameArgument = process.argv.find((arg) => arg.startsWith('--frames='));
const frames = frameArgument ? Number(frameArgument.split('=')[1]) : 120;
if (!Number.isInteger(frames) || frames <= 0)
  throw new Error('--frames must be a positive integer');

function createScene(count, obstacleCount, iterations, stressed = false) {
  const world = new PhysicsWorld({
    dt: 0.03,
    substeps: 3,
    solverIterations: iterations,
    mode: 'xpbd',
  });
  for (let i = 0; i < count; i++) {
    const offset = stressed
      ? [((i % 3) - 1) * 0.65, 0, (Math.floor(i / 3) - 0.5) * 0.65]
      : [0, 0, 0];
    world.addSoftBody(bunnyData, offset, { velocity: [0.1, 0.1, 0] });
  }
  world.addBall(1, stressed ? [0, 0, 0] : [4, 3, 0], [0.1, 0, 0]);
  world.obstacles = [
    { shape: 'box', position: [0, -3, 0], size: [4, 2, 4] },
    { shape: 'sphere', position: [-4, -3, 0], radius: 1.5 },
  ].slice(0, obstacleCount);
  return world;
}

function residuals(world) {
  let maxEdgeError = 0,
    sumVolumeError = 0,
    volumeCount = 0,
    finite = true;
  for (const body of world.bodies)
    finite &&= body.positions.every(Number.isFinite) && body.velocities.every(Number.isFinite);
  for (const body of world.softBodies) {
    const p = body.positions;
    for (let e = 0; e < body.restLengths.length; e++) {
      const a = body.edgeIndices[e * 2] * 3,
        b = body.edgeIndices[e * 2 + 1] * 3;
      const length = Math.hypot(p[b] - p[a], p[b + 1] - p[a + 1], p[b + 2] - p[a + 2]);
      maxEdgeError = Math.max(maxEdgeError, Math.abs(length - body.restLengths[e]));
    }
    for (let t = 0; t < body.restVolumes.length; t++) {
      const o = t * 4,
        ids = body.tetIndices;
      sumVolumeError += Math.abs(
        tetraVolume(p, ids[o], ids[o + 1], ids[o + 2], ids[o + 3]) - body.restVolumes[t],
      );
      volumeCount++;
    }
  }
  return { finite, maxEdgeError, meanAbsoluteVolumeError: sumVolumeError / volumeCount };
}

function measure(name, count, obstacles, iterations, stressed) {
  const world = createScene(count, obstacles, iterations, stressed);
  // JIT warmup uses a separate scene; measurement always starts from the same data.
  const warmup = createScene(count, obstacles, iterations, stressed);
  for (let i = 0; i < 60; i++) warmup.step();
  const samples = [];
  for (let i = 0; i < frames; i++) {
    const start = performance.now();
    world.step();
    samples.push(performance.now() - start);
  }
  samples.sort((a, b) => a - b);
  return {
    name,
    bunnies: count,
    balls: 1,
    obstacles,
    iterations,
    frames,
    medianMs: samples[Math.floor(samples.length * 0.5)],
    p95Ms: samples[Math.min(samples.length - 1, Math.floor(samples.length * 0.95))],
    ...residuals(world),
  };
}

const report = {
  runtime: process.version,
  platform: `${platform()} ${arch()}`,
  cpu: cpus()[0]?.model,
  dt: 0.03,
  substeps: 3,
  mode: 'xpbd',
  edgeCompliance: 1e-6,
  volumeCompliance: 1e-8,
  mesh: {
    vertices: bunnyData.verts.length / 3,
    edges: bunnyData.tetEdgeIds.length / 2,
    tetrahedra: bunnyData.tetIds.length / 4,
  },
  scope:
    'CPU physics only. Corrected algorithm; this is not numerical equivalence with legacy Python.',
  results: [
    measure('single bunny, no obstacles', 1, 0, 1, false),
    measure('single bunny, two obstacles', 1, 2, 1, false),
    measure('default quality, two obstacles', 1, 2, 2, false),
    measure('six bunnies, overlap stress', 6, 2, 2, true),
  ],
};
console.log(JSON.stringify(report, null, 2));
if (report.results.some((result) => !result.finite)) process.exitCode = 1;
