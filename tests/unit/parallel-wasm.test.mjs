import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { cpus } from 'node:os';
import { runParallelScenario } from '../browser/parallel-scenario.mjs';

const require = createRequire(import.meta.url);
const serial = await require('../../wasm/pbd_core.js')();
const parallel = await require('../../wasm/pbd_core_threaded.js')();
const expectedWorkers = Math.max(0, Math.min(3, cpus().length - 1));

test('std::thread Wasm matches sequential positions, velocities, multipliers and contacts exactly', async () => {
  for (const mode of ['pbd', 'xpbd']) {
    for (const count of [0, 1, 2, 5]) {
      const expected = runParallelScenario(serial, mode, count);
      const actual = runParallelScenario(parallel, mode, count);

      assert.deepEqual(actual.snapshots, expected.snapshots);
      assert.equal(expected.workers, 0);
      assert.equal(actual.workers, count > 1 ? expectedWorkers : 0);
      // 브라우저와 같은 종료 이벤트 처리 경계를 둡니다.
      await new Promise((resolve) => setImmediate(resolve));
    }
  }
});

test('threaded memory growth refreshes borrowed views and world disposal remains reusable', async () => {
  const api = require('../../src/simulation/physics.js');
  await api.init({ moduleFactory: async () => parallel });

  for (let iteration = 0; iteration < 5; iteration++) {
    const world = new api.PhysicsWorld();

    try {
      const data = {
        verts: [0, 0, 0, 1, 0, 0],
        tetEdgeIds: [0, 1],
        tetIds: [],
        tetSurfaceTriIds: [],
      };
      const body = world.addSoftBody(data, [0, 0, 0]);

      world.addSoftBody(data, [3, 0, 0]);
      world.step(0.01);

      assert.equal(world.constraintWorkerCount, expectedWorkers);

      if (iteration === 0) {
        const oldBuffer = body.positions.buffer;
        // 코어 할당으로 공유 메모리의 성장을 유도합니다.
        const allocationWorld = new api.PhysicsWorld();

        try {
          allocationWorld.addSoftBody(
            { verts: new Float64Array(1500000), tetEdgeIds: [], tetIds: [], tetSurfaceTriIds: [] },
            [0, 0, 0],
          );
        } finally {
          allocationWorld.dispose();
        }

        assert.notEqual(body.positions.buffer, oldBuffer);

        world.step(0.01);
      }

      assert.ok(Array.from(body.positions).every(Number.isFinite));
    } finally {
      world.dispose();
      world.dispose();
    }

    await new Promise((resolve) => setImmediate(resolve));
  }
});

test('module worker budget falls back while occupied and is returned after world disposal', async () => {
  const data = {
    verts: [0, 0, 0, 1, 0, 0],
    tetEdgeIds: [0, 1],
    tetIds: [],
    tetSurfaceTriIds: [],
  };
  const worlds = [];
  const create = () => {
    const world = parallel.createWorld({});
    worlds.push(world);
    parallel.addSoft(world, data, [0, 0, 0], {});
    parallel.addSoft(world, data, [3, 0, 0], {});
    parallel.step(world, 0.01);
    return world;
  };

  try {
    const first = create();
    const second = create();
    const availableAfterFirst = 3 - expectedWorkers;
    const secondWorkers = expectedWorkers <= availableAfterFirst ? expectedWorkers : 0;

    assert.equal(parallel.worldWorkerCount(first), expectedWorkers);
    assert.equal(parallel.worldWorkerCount(second), secondWorkers);
    parallel.destroyWorld(first);
    worlds.splice(worlds.indexOf(first), 1);
    await new Promise((resolve) => setImmediate(resolve));

    // 예약에 실패한 실행기는 계속 순차입니다. 새 실행기만 반환된 예산을 예약합니다.
    parallel.step(second, 0.01);
    assert.equal(parallel.worldWorkerCount(second), secondWorkers);

    const third = create();
    const availableForThird = 3 - secondWorkers;
    assert.equal(
      parallel.worldWorkerCount(third),
      expectedWorkers <= availableForThird ? expectedWorkers : 0,
    );
  } finally {
    for (const world of worlds) parallel.destroyWorld(world);
    await new Promise((resolve) => setImmediate(resolve));
  }
});
