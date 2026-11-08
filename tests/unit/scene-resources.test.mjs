import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';

const source = await readFile(new URL('../../src/rendering/scene.js', import.meta.url), 'utf8');
const lifecycle = new URL('../../src/lifecycle.mjs', import.meta.url).href;
const { PhysicsScene } = await import(
  `data:text/javascript,${encodeURIComponent(source.replace("'../lifecycle.mjs'", JSON.stringify(lifecycle)))}`
);

for (const kind of ['body', 'obstacle']) {
  test(`${kind} geometry is released if material construction fails`, () => {
    const failure = new Error('material creation failed');
    const calls = [];
    const scene = Object.create(PhysicsScene.prototype);
    scene.THREE = {
      [kind === 'body' ? 'SphereGeometry' : 'BoxGeometry']: class {
        dispose() {
          calls.push('geometry');
        }
      },
      MeshStandardMaterial: class {
        constructor() {
          throw failure;
        }
      },
    };
    const create =
      kind === 'body'
        ? () => scene.addBody({ type: 'ball', radius: 1 }, 0, false)
        : () => scene.addObstacle({ shape: 'box' });
    assert.throws(create, (error) => error === failure);
    assert.deepEqual(calls, ['geometry']);
  });
}

test('body synchronization failure removes its mesh and attempts both resource releases', () => {
  const original = new Error('body synchronization failed');
  const calls = [];
  const scene = Object.create(PhysicsScene.prototype);
  scene.THREE = {
    SphereGeometry: class {
      dispose() {
        calls.push('geometry');
        throw new Error('geometry cleanup failed');
      }
    },
    MeshStandardMaterial: class {
      color = { convertSRGBToLinear() {} };
      dispose() {
        calls.push('material');
      }
    },
    Mesh: class {
      userData = {};
      position = {
        fromArray() {
          throw original;
        },
      };
    },
  };
  scene.scene = {
    add() {
      calls.push('add');
    },
    remove() {
      calls.push('remove');
    },
  };
  scene.bodyViews = new Map();
  const body = { type: 'ball', radius: 1, positions: [0, 0, 0] };
  assert.throws(
    () => scene.addBody(body, 0, false),
    (error) => error === original,
  );
  assert.deepEqual(calls, ['add', 'remove', 'geometry', 'material']);
  assert.equal(scene.bodyViews.has(body), false);
});
