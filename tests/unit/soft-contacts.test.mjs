// Run: node --test tests/unit/soft-contacts.test.mjs
import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);

await require('../../src/simulation/physics.js').init();

const { PhysicsWorld } = require('../../src/simulation/physics.js');
const { SoftContactSolver } = require('../../src/simulation/soft-contacts.js');
const bunnyData = require('../../src/data/bunny-data.js');
const tetraVertices = [0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1];

function simulation(options = {}) {
  return new PhysicsWorld({
    bounds: 100,
    gravity: [0, 0, 0],
    damping: 0,
    restitution: 0.5,
    dt: 0.1,
    substeps: 1,
    solverIterations: 1,
    edgeCompliance: 0,
    volumeCompliance: 0,
    ...options,
  });
}

function mesh(verts = tetraVertices, tetIds = [0, 1, 2, 3]) {
  return { verts, tetIds, tetEdgeIds: [], tetSurfaceTriIds: [] };
}

function near(actual, expected, epsilon = 1e-10) {
  assert.ok(Math.abs(actual - expected) <= epsilon, `${actual} != ${expected}`);
}

function finite(body) {
  assert.ok(Array.from(body.positions).every(Number.isFinite));
  assert.ok(Array.from(body.velocities).every(Number.isFinite));
}

function contacts(solver, first, second) {
  solver.prepare([first, second]);

  return solver.generateContacts(first, second);
}

function vertex(body, id) {
  return Array.from(body.positions.slice(id * 3, id * 3 + 3));
}

// Independent oracle: compare a point and the opposite vertex against each
// oriented face plane. The production broad phase and barycentric code are unused.
function side(a, b, c, p) {
  const u = b.map((value, axis) => value - a[axis]);
  const v = c.map((value, axis) => value - a[axis]);
  const q = p.map((value, axis) => value - a[axis]);

  return (
    (u[1] * v[2] - u[2] * v[1]) * q[0] +
    (u[2] * v[0] - u[0] * v[2]) * q[1] +
    (u[0] * v[1] - u[1] * v[0]) * q[2]
  );
}

function bruteContains(body, point) {
  const ids = body.tetIndices;

  for (let offset = 0; offset < ids.length; offset += 4) {
    const tet = [0, 1, 2, 3].map((index) => vertex(body, ids[offset + index]));

    if (Math.abs(side(tet[0], tet[1], tet[2], tet[3])) < 1e-16) continue;

    const faces = [
      [1, 2, 3, 0],
      [0, 3, 2, 1],
      [0, 1, 3, 2],
      [0, 2, 1, 3],
    ];

    if (
      faces.every(([i, j, k, opposite]) => {
        const reference = side(tet[i], tet[j], tet[k], tet[opposite]);
        const candidate = side(tet[i], tet[j], tet[k], point);
        return reference > 0 ? candidate >= 0 : candidate <= 0;
      })
    )
      return true;
  }

  return false;
}

function weightedPositionSum(bodies) {
  const result = [0, 0, 0];

  for (const body of bodies) {
    for (let index = 0; index < body.vertexCount; index += 1) {
      assert.ok(body.inverseMass[index] > 0);

      for (let axis = 0; axis < 3; axis += 1)
        result[axis] += body.positions[index * 3 + axis] / body.inverseMass[index];
    }
  }

  return result;
}

test('overlapping bounding spheres alone never produce a local contact', () => {
  const world = simulation();
  const first = world.addSoftBody(mesh());
  const second = world.addSoftBody(mesh(), [1.1, 0, 0]);

  assert.ok(
    Math.hypot(...first.center.map((value, axis) => value - second.center[axis])) <
      first.radius + second.radius,
  );
  assert.equal(contacts(new SoftContactSolver(), first, second).length, 0);
});

for (const ids of [
  [0, 1, 2, 3],
  [0, 2, 1, 3],
]) {
  test(`point tetrahedron contact respects signed orientation ${ids.join(',')}`, () => {
    const world = simulation();
    const source = world.addSoftBody(mesh([0.1, 0.2, 0.3], []));
    const target = world.addSoftBody(mesh(tetraVertices, ids));
    const found = contacts(new SoftContactSolver(), source, target);

    assert.equal(found.length, 1);

    const contact = found[0];

    assert.equal(contact.a, source);
    assert.equal(contact.b, target);
    assert.equal(contact.ai, 0);

    near(contact.depth, 0.1);
    near(contact.nx, -1);
    near(contact.ny, 0);
    near(contact.nz, 0);
    near(
      contact.weights.reduce((sum, value) => sum + value, 0),
      1,
    );

    assert.ok(contact.weights.every((value) => value >= 0));
    assert.deepEqual(
      Array.from(contact.indices).sort((a, b) => a - b),
      [0, 2, 3],
    );
  });
}

test('shallow projection changes the penetrated point and only its contacted exterior triangle', () => {
  const world = simulation();
  const source = world.addSoftBody(mesh([0.1, 0.2, 0.3, 10, 10, 10], []), [0, 0, 0], {
    inverseMass: [2, 1],
  });
  const target = world.addSoftBody(mesh());
  const solver = new SoftContactSolver();
  const found = contacts(solver, source, target);

  assert.equal(found.length, 1);

  const before = weightedPositionSum([source, target]);
  const sourceFar = vertex(source, 1),
    targetFar = vertex(target, 1);

  solver.projectContacts(found, []);

  assert.deepEqual(vertex(source, 1), sourceFar);
  assert.deepEqual(vertex(target, 1), targetFar);
  assert.ok(source.positions[0] < 0.1);

  const after = weightedPositionSum([source, target]);
  after.forEach((value, axis) => near(value, before[axis]));

  // Effective triangle inverse mass is sum(barycentricWeight² * inverseMass).
  const expectedDenominator = 2 + 0.5 ** 2 + 0.2 ** 2 + 0.3 ** 2;

  near(source.positions[0], 0.1 - (2 * 0.1) / expectedDenominator);
  finite(source);
  finite(target);
});

test('pinned target triangle lets only the penetrating particle move', () => {
  const world = simulation();
  const source = world.addSoftBody(mesh([0.1, 0.2, 0.3], []));
  const target = world.addSoftBody(mesh(), [0, 0, 0], { inverseMass: 0 });
  const solver = new SoftContactSolver(),
    original = Array.from(target.positions);
  solver.projectContacts(contacts(solver, source, target), []);
  near(source.positions[0], 0);

  assert.deepEqual(Array.from(target.positions), original);

  finite(source);
});

test('fully pinned penetrating configurations remain finite and fixed', () => {
  const world = simulation();
  const source = world.addSoftBody(mesh([0.1, 0.2, 0.3], []), [0, 0, 0], { inverseMass: 0 });
  const target = world.addSoftBody(mesh(), [0, 0, 0], { inverseMass: 0 });
  const before = [source, target].map((body) => Array.from(body.positions));
  const solver = new SoftContactSolver();
  solver.projectContacts(contacts(solver, source, target), []);
  [source, target].forEach((body, index) => {
    assert.deepEqual(Array.from(body.positions), before[index]);

    finite(body);
  });
});

test('a shared tetrahedron face is never treated as the external exit surface', () => {
  const world = simulation();
  const source = world.addSoftBody(mesh([0.2, 0.2, 0.01], []));
  const target = world.addSoftBody(mesh([...tetraVertices, 0, 0, -1], [0, 1, 2, 3, 0, 2, 1, 4]));
  const found = contacts(new SoftContactSolver(), source, target);

  assert.equal(found.length, 1);
  assert.ok(found[0].depth >= 0.2 - 1e-10);
  assert.notDeepEqual(
    Array.from(found[0].indices).sort((a, b) => a - b),
    [0, 1, 2],
  );
});

test('spatial candidates match brute force for negative coordinates and cell boundaries without duplicate contacts', () => {
  const world = simulation();
  const vertices = [],
    ids = [],
    points = [];

  for (const x of [-3.1, -1.1, 0.9, 2.9]) {
    for (const y of [-2.2, -0.2, 1.8]) {
      const offset = vertices.length / 3;

      vertices.push(...tetraVertices.map((value, axis) => value + [x, y, -0.3][axis % 3]));
      ids.push(offset, offset + 1, offset + 2, offset + 3);
      points.push(x + 0.1, y + 0.2, 0); // Integer x/y/z query boundaries.
    }
  }

  let state = 271828;

  for (let point = 0; point < 300; point += 1) {
    for (let axis = 0; axis < 3; axis += 1) {
      state = (1664525 * state + 1013904223) >>> 0;
      points.push((state / 4294967296) * 8 - 4);
    }
  }

  const source = world.addSoftBody(mesh(points, []));
  const target = world.addSoftBody(mesh(vertices, ids));
  const expected = [];

  for (let index = 0; index < source.vertexCount; index += 1)
    if (bruteContains(target, vertex(source, index))) expected.push(index);

  const found = contacts(new SoftContactSolver(), source, target);
  const actual = found
    .filter((contact) => contact.a === source)
    .map((contact) => contact.ai)
    .sort((a, b) => a - b);

  assert.deepEqual(actual, expected);
  assert.equal(new Set(actual).size, actual.length);
});

test('refitting changes contact candidates after current geometry moves between cells', () => {
  const world = simulation();
  const source = world.addSoftBody(mesh([0.1, 0.2, 0.3], []));
  const target = world.addSoftBody(mesh());
  const solver = new SoftContactSolver();

  assert.equal(contacts(solver, source, target).length, 1);

  for (let index = 0; index < target.positions.length; index += 3) target.positions[index] -= 10;

  target.updateBounds();

  assert.equal(contacts(solver, source, target).length, 0);

  source.positions[0] -= 10;
  source.updateBounds();

  assert.equal(contacts(solver, source, target).length, 1);
});

test('large tetrahedra use a bounded overflow fallback without losing containment', () => {
  const world = simulation();
  const target = world.addSoftBody(mesh());
  const solver = new SoftContactSolver({ cellSize: 0.001, maxCellsPerTet: 2 });
  solver.prepare([target]);

  assert.equal(solver.getBodyInfo(target).overflowTets, 1);
  assert.equal(solver.getBodyInfo(target).hashCellCount, 0);
  assert.deepEqual(solver.queryPoint(target, [0.1, 0.2, 0.3]), [0]);
  assert.deepEqual(solver.queryPoint(target, [0.8, 0.8, 0.8]), []);
});

for (const separation of [2, 5000, 1_000_000]) {
  test(`dense, sparse numeric, and distant string cell paths agree at separation ${separation}`, () => {
    const world = simulation();
    const shifted = tetraVertices.map((value, axis) => value + (axis % 3 === 0 ? separation : 0));
    const target = world.addSoftBody(
      mesh([...tetraVertices, ...shifted], [0, 1, 2, 3, 4, 5, 6, 7]),
    );
    const solver = new SoftContactSolver({ cellSize: 0.25, maxCellsPerTet: 512 });
    solver.prepare([target]);

    assert.equal(solver.getBodyInfo(target).overflowTets, 0);

    const queries = [
      [0.1, 0.2, 0.3],
      [separation + 0.1, 0.2, 0.3],
      [separation + 0.9, 0.9, 0.9],
      [separation * 0.5, 0.4, 0.4],
    ];
    const expected = [[0], [1], [], []];

    queries.forEach((point, index) => {
      assert.equal(bruteContains(target, point), expected[index].length > 0);
      assert.deepEqual(solver.queryPoint(target, point), expected[index]);
    });

    // Refit the same cached entry across negative coordinates and key ranges.
    for (let index = 0; index < target.positions.length; index += 3)
      target.positions[index] -= separation;

    target.updateBounds();
    solver.prepare([target]);
    queries.forEach((point, index) => {
      const moved = [point[0] - separation, point[1], point[2]];

      assert.equal(bruteContains(target, moved), expected[index].length > 0);
      assert.deepEqual(solver.queryPoint(target, moved), expected[index]);
    });
  });
}

test('a shared face query returns each containing tetrahedron exactly once', () => {
  const world = simulation();
  const target = world.addSoftBody(mesh([...tetraVertices, 0, 0, -1], [0, 1, 2, 3, 0, 2, 1, 4]));
  const solver = new SoftContactSolver({ cellSize: 0.25 });
  solver.prepare([target]);

  assert.deepEqual(
    solver.queryPoint(target, [0.2, 0.2, 0]).sort((a, b) => a - b),
    [0, 1],
  );
  assert.equal(solver.getBodyInfo(target).surfaceTriangleCount, 6);
});

test('a supplied incomplete surface is rejected instead of treating it as a closed exterior', () => {
  const world = simulation();
  const target = world.addSoftBody({ ...mesh(), tetSurfaceTriIds: [0, 1, 2] });

  assert.throws(() => new SoftContactSolver().prepare([target]), TypeError);
});

test('fully degenerate tetrahedra never produce nonfinite corrections', () => {
  const world = simulation();
  const source = world.addSoftBody(mesh([0, 0, 0], []));
  const target = world.addSoftBody(mesh());
  target.positions.fill(0);
  target.updateBounds();

  const solver = new SoftContactSolver();
  solver.projectContacts(contacts(solver, source, target), []);
  finite(source);
  finite(target);
});

test('stationary local penetration never turns positional correction into launch velocity', () => {
  const world = simulation();
  const source = world.addSoftBody(mesh([0.1, 0.2, 0.3], []));
  const target = world.addSoftBody(mesh());

  world.step();

  for (const body of [source, target]) {
    finite(body);

    assert.ok(Array.from(body.velocities).every((value) => Math.abs(value) < 1e-10));
  }
});

test('local normal restitution preserves mass weighted momentum', () => {
  const world = simulation({ dt: 0.01, restitution: 0.5 });
  const source = world.addSoftBody(mesh([0.05, 0.2, 0.3], []), [0, 0, 0], { velocity: [1, 0, 0] });
  const target = world.addSoftBody(mesh());

  world.step();

  const surfaceVelocity =
    target.velocities[0] * 0.5 + target.velocities[6] * 0.2 + target.velocities[9] * 0.3;

  near(-(source.velocities[0] - surfaceVelocity), 0.5);

  let momentum = source.velocities[0];

  for (let index = 0; index < target.velocities.length; index += 3)
    momentum += target.velocities[index];

  near(momentum, 1);
  finite(source);
  finite(target);
});

test('deep initial bunny overlap produces finite local corrections without promising complete separation', () => {
  const world = simulation();
  const first = world.addSoftBody(bunnyData);
  const second = world.addSoftBody(bunnyData, [0.05, 0.03, -0.02]);
  const solver = new SoftContactSolver(),
    found = contacts(solver, first, second);

  assert.ok(found.length > 0);

  const before = weightedPositionSum([first, second]);

  solver.projectContacts(found, []);

  const after = weightedPositionSum([first, second]);
  after.forEach((value, axis) => near(value, before[axis], 1e-9));

  for (const body of [first, second]) {
    finite(body);

    assert.ok(Array.from(body.positions).every((value) => Math.abs(value) < 10));
  }
});

test('real bunny containment candidates agree with independent brute force', (context) => {
  const world = simulation();
  const target = world.addSoftBody(bunnyData, [-1, -1, -1]);
  const points = [];

  // Tet centers guarantee genuine interior examples; box samples add empty space.
  for (let offset = 0; offset < target.tetIndices.length; offset += 4 * 47) {
    const center = [0, 0, 0];

    for (let index = 0; index < 4; index += 1) {
      const value = vertex(target, target.tetIndices[offset + index]);

      center.forEach((_, axis) => (center[axis] += value[axis] / 4));
    }

    points.push(...center);
  }

  for (let index = 0; index < 50; index += 1) {
    points.push(
      -1.6 + (index % 5) * 0.3,
      -1.2 + (Math.floor(index / 5) % 5) * 0.3,
      -1.3 + Math.floor(index / 25) * 0.6,
    );
  }

  const source = world.addSoftBody(mesh(points, []));
  const oracleStart = performance.now();
  const expected = [];

  for (let index = 0; index < source.vertexCount; index += 1)
    if (bruteContains(target, vertex(source, index))) expected.push(index);

  const oracleMs = performance.now() - oracleStart;
  const queryStart = performance.now();
  const found = contacts(new SoftContactSolver(), source, target);
  const queryMs = performance.now() - queryStart;

  assert.deepEqual(
    found
      .filter((contact) => contact.a === source)
      .map((contact) => contact.ai)
      .sort((a, b) => a - b),
    expected,
  );

  context.diagnostic(
    JSON.stringify({
      points: source.vertexCount,
      inside: expected.length,
      tetrahedra: target.tetIndices.length / 4,
      oracleMs,
      prepareAndQueryMs: queryMs,
    }),
  );
});
