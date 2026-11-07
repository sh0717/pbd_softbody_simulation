// Same-scene CPU comparison: historical whole-sphere response versus local point/tet response.
import { createRequire } from 'node:module';
import { performance } from 'node:perf_hooks';
import { cpus } from 'node:os';
const require = createRequire(import.meta.url);
await require('../../src/simulation/physics.js').init();
const current = require('../../src/simulation/physics.js');
const baseline = require('../../tests/fixtures/physics-sphere-baseline.js');
const { SoftContactSolver } = require('../../src/simulation/soft-contacts.js');
const bunny = require('../../src/data/bunny-data.js');
const argument = process.argv.find((value) => value.startsWith('--frames='));
const frames = argument ? Number(argument.split('=')[1]) : 120;
if (!Number.isInteger(frames) || frames < 1) throw new RangeError('Invalid frame count');

function scene(api, count, separated) {
  const world = new api.PhysicsWorld({ dt: 0.03, substeps: 3, solverIterations: 2, mode: 'xpbd' });
  for (let i = 0; i < count; i++) {
    const spacing = separated ? 2 : 0.65;
    world.addSoftBody(bunny, [((i % 3) - 1) * spacing, 0, (Math.floor(i / 3) - 0.5) * spacing], {
      velocity: [0.1, 0.1, 0],
    });
  }
  world.addBall(1, separated ? [4, 3, 0] : [0, 0, 0], [0.1, 0, 0]);
  world.obstacles = [
    { shape: 'box', position: [0, -3, 0], size: [4, 2, 4] },
    { shape: 'sphere', position: [-4, -3, 0], radius: 1.5 },
  ];
  return world;
}

function quality(world) {
  const ContactSolver = world.backend
    ? SoftContactSolver
    : require('../../tests/fixtures/soft-contacts.js').SoftContactSolver;
  const solver = new ContactSolver();
  solver.prepare(world.softBodies);
  let pointTetPenetrations = 0,
    maximumPointTetPenetration = 0,
    maxEdgeError = 0,
    absoluteVolumeError = 0,
    volumeCount = 0;
  for (let a = 0; a < world.softBodies.length; a++) {
    for (let b = a + 1; b < world.softBodies.length; b++) {
      for (const contact of solver.generateContacts(world.softBodies[a], world.softBodies[b])) {
        pointTetPenetrations++;
        maximumPointTetPenetration = Math.max(maximumPointTetPenetration, contact.depth);
      }
    }
  }
  for (const body of world.softBodies) {
    const p = body.positions;
    for (let e = 0; e < body.restLengths.length; e++) {
      const a = body.edgeIndices[e * 2] * 3,
        b = body.edgeIndices[e * 2 + 1] * 3;
      maxEdgeError = Math.max(
        maxEdgeError,
        Math.abs(
          Math.hypot(p[a] - p[b], p[a + 1] - p[b + 1], p[a + 2] - p[b + 2]) - body.restLengths[e],
        ),
      );
    }
    for (let t = 0; t < body.restVolumes.length; t++) {
      const o = t * 4,
        ids = body.tetIndices;
      absoluteVolumeError += Math.abs(
        current.tetraVolume(p, ids[o], ids[o + 1], ids[o + 2], ids[o + 3]) - body.restVolumes[t],
      );
      volumeCount++;
    }
  }
  return {
    finite: world.bodies.every(
      (body) => body.positions.every(Number.isFinite) && body.velocities.every(Number.isFinite),
    ),
    pointTetPenetrations,
    maximumPointTetPenetration,
    maxEdgeError,
    meanAbsoluteVolumeError: absoluteVolumeError / volumeCount,
  };
}

function measure(api, label, count, separated) {
  const warmup = scene(api, count, separated);
  for (let frame = 0; frame < 60; frame++) warmup.step();
  const world = scene(api, count, separated),
    samples = [];
  let candidateTests = 0,
    generatedContacts = 0,
    solvedContacts = 0,
    hashCells = 0,
    triangleTests = 0;
  for (let frame = 0; frame < frames; frame++) {
    const start = performance.now();
    world.step();
    samples.push(performance.now() - start);
    candidateTests += world.softContactStats?.candidateTests || 0;
    generatedContacts += world.softContactStats?.generatedContacts || 0;
    solvedContacts += world.softContactStats?.contacts || 0;
    hashCells += world.softContactStats?.hashCells || 0;
    triangleTests += world.softContactStats?.surfaceTriangleTests || 0;
  }
  samples.sort((a, b) => a - b);
  return {
    algorithm: label,
    bunnies: count,
    balls: 1,
    obstacles: 2,
    initialScene: separated ? 'separated' : 'overlapping',
    frames,
    medianMs: samples[Math.floor(frames / 2)],
    p95Ms: samples[Math.min(frames - 1, Math.floor(frames * 0.95))],
    averageFrameCounters: {
      candidateTests: candidateTests / frames,
      generatedContacts: generatedContacts / frames,
      solvedContacts: solvedContacts / frames,
      hashCells: hashCells / frames,
      surfaceTriangleTests: triangleTests / frames,
    },
    finalGeometry: quality(world),
  };
}

function sphereFalsePositive(api) {
  const world = new api.PhysicsWorld({ bounds: 100, substeps: 1, solverIterations: 1 });
  const data = {
    verts: [0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1],
    tetEdgeIds: [],
    tetIds: [0, 1, 2, 3],
  };
  const a = world.addSoftBody(data),
    b = world.addSoftBody(data, [1.1, 0, 0]);
  const originals = [a.positions.slice(), b.positions.slice()];
  world.step();
  return Math.max(
    ...world.softBodies.flatMap((body, i) =>
      [...body.positions].map((value, j) => Math.abs(value - originals[i][j])),
    ),
  );
}

const report = {
  runtime: process.version,
  cpu: cpus()[0]?.model,
  dt: 0.03,
  substeps: 3,
  iterations: 2,
  mode: 'xpbd',
  scope:
    'CPU physics only. Each measurement starts from identical geometry; state evolves during measured frames. Different contact algorithms produce different trajectories.',
  disjointTetrahedraWithOverlappingSpheres: {
    baselineMaximumVertexDisplacement: sphereFalsePositive(baseline),
    localMaximumVertexDisplacement: sphereFalsePositive(current),
  },
  results: [
    measure(baseline, 'whole bounding sphere baseline', 1, true),
    measure(current, 'local point-tet', 1, true),
    measure(baseline, 'whole bounding sphere baseline', 2, false),
    measure(current, 'local point-tet', 2, false),
    measure(baseline, 'whole bounding sphere baseline', 6, false),
    measure(current, 'local point-tet', 6, false),
  ],
};
console.log(JSON.stringify(report, null, 2));
if (report.results.some((result) => !result.finalGeometry.finite)) process.exitCode = 1;
