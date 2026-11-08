// Node와 브라우저가 같은 입력으로 전체 스텝의 위치·속도·승수·통계를 비교합니다.
export function runParallelScenario(core, mode = 'xpbd', count = 5, frames = 60) {
  const world = core.createWorld({
    mode,
    bounds: 4,
    gravity: [0, -9.81, 0],
    substeps: 3,
    solverIterations: 4,
  });
  const data = {
    verts: [0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1],
    tetEdgeIds: [0, 1, 0, 2, 0, 3, 1, 2, 1, 3, 2, 3],
    tetIds: [0, 1, 2, 3],
    tetSurfaceTriIds: [],
  };
  const bodies = [];
  const snapshots = [];
  let workers;

  try {
    for (let index = 0; index < count; index++) {
      const body = core.addSoft(world, data, [index * 0.7 - 2, -3.5, 0], {
        inverseMass: [0, 1, 2, 1],
      });

      core.bodyArray(body, 0)[11] = 1.4;
      bodies.push(body);
    }

    bodies.push(core.addBall(world, 0.6, [0, -2.5, 0], [0.1, 0, 0], {}));
    core.setObstacles(world, [
      {
        nativeId: 1,
        shape: 'box',
        position: [0, -3.8, 0],
        size: [3, 0.5, 3],
        bumper: true,
        bumperStrength: 4,
      },
    ]);
    core.step(world, 0);

    const beforeWorkers = core.worldWorkerCount?.(world) ?? 0;

    if (beforeWorkers !== 0) throw new Error('zero dt created workers');

    for (let frame = 0; frame < frames; frame++) {
      if (frame === 30) core.reset(world);

      core.step(world, 1 / 60);
      snapshots.push({
        time: core.worldTime(world),
        stats: core.worldStats(world),
        hits: core.worldHits(world).map((hit) => ({ ...hit, body: bodies.indexOf(hit.body) })),
        bodies: bodies.map((body, index) => {
          const fields = index < count ? [0, 2, 3, 4, 11, 12, 14, 15, 16] : [0, 2, 3, 4];
          return fields.map((field) => Array.from(core.bodyArray(body, field)));
        }),
      });
    }

    workers = core.worldWorkerCount?.(world) ?? 0;
  } finally {
    core.destroyWorld(world);
  }

  return { snapshots, workers };
}
