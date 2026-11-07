// Compare this change's preserved runtime with the rebuilt current runtime.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { createRequire } from 'node:module';
import path from 'node:path';
import { preparePreservedRuntime } from './preserved-runtime.mjs';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const { runtime, archive, label } = await preparePreservedRuntime(root);
const require = createRequire(import.meta.url);
const beforeApi = await require(
  path.join(runtime, label === 'portfolio' ? 'src/simulation/physics.js' : 'physics.js'),
).init();
const afterApi = await require('../src/simulation/physics.js').init();
const bunny = require('../src/data/bunny-data.js');
assert.deepEqual(
  bunny,
  require(path.join(runtime, label === 'portfolio' ? 'src/data/bunny-data.js' : 'bunny-data.js')),
  'preserve mesh vertices and topology',
);

function difference(before, after, label) {
  assert.equal(after.length, before.length, `${label} length`);
  let maximum = 0;
  for (let index = 0; index < before.length; index++) {
    assert.ok(
      Number.isFinite(before[index]) && Number.isFinite(after[index]),
      `${label}[${index}] finite`,
    );
    maximum = Math.max(maximum, Math.abs(before[index] - after[index]));
  }
  assert.equal(maximum, 0, `${label} numerical regression`);
  return maximum;
}

function createScene(api, mode, count) {
  const world = new api.PhysicsWorld({
    dt: 0.03,
    substeps: 3,
    solverIterations: 2,
    mode,
    gravity: [0, -9.81, 0],
  });
  for (let index = 0; index < count; index++) {
    world.addSoftBody(bunny, [((index % 3) - 1) * 0.65, 0, (Math.floor(index / 3) - 0.5) * 0.65], {
      velocity: [0.1, 0.1, 0],
    });
  }
  world.addBall(1, [0, 0, 0], [0.1, 0, 0]);
  world.obstacles = [
    { shape: 'box', position: [0, -3, 0], size: [4, 2, 4], bumper: true, bumperStrength: 4 },
    { shape: 'sphere', position: [-4, -3, 0], radius: 1.5 },
  ];
  return world;
}

function hits(world) {
  return world.bumperHits.map(({ body, obstacle, ...hit }) => ({
    ...hit,
    body: world.bodies.indexOf(body),
    obstacle: world.obstacles.indexOf(obstacle),
  }));
}

const frames = 60;
const scenarios = [];
for (const mode of ['pbd', 'xpbd']) {
  for (const count of [2, 6]) {
    const before = createScene(beforeApi, mode, count);
    const after = createScene(afterApi, mode, count);
    let positionDifference = 0;
    let velocityDifference = 0;
    let diagnosticDifference = 0;
    try {
      for (let frame = 0; frame < frames; frame++) {
        before.step();
        after.step();
        for (let index = 0; index < before.bodies.length; index++) {
          positionDifference = Math.max(
            positionDifference,
            difference(before.bodies[index].positions, after.bodies[index].positions, 'positions'),
          );
          velocityDifference = Math.max(
            velocityDifference,
            difference(
              before.bodies[index].velocities,
              after.bodies[index].velocities,
              'velocities',
            ),
          );
        }
        diagnosticDifference = Math.max(
          diagnosticDifference,
          difference(
            Object.values(before.softContactStats),
            Object.values(after.softContactStats),
            'contact diagnostics',
          ),
        );
        assert.deepEqual(hits(after), hits(before));
        assert.equal(after.simulationTime, before.simulationTime);
      }
      scenarios.push({
        mode,
        softBodies: count,
        balls: 1,
        frames,
        dt: 0.03,
        substeps: 3,
        solverIterations: 2,
        positionDifference,
        velocityDifference,
        diagnosticDifference,
      });
    } finally {
      before.dispose();
      after.dispose();
    }
  }
}
const result = {
  node: process.version,
  baselineArchiveSHA256: createHash('sha256')
    .update(await readFile(archive))
    .digest('hex'),
  scenarios,
};
await mkdir(path.join(root, 'artifacts'), { recursive: true });
await writeFile(
  path.join(root, `artifacts/${label}-equivalence.json`),
  `${JSON.stringify(result, null, 2)}\n`,
);
console.log(JSON.stringify(result, null, 2));
