/* C++20/WebAssembly adapter. All integration, constraints and contacts live in native/src/. */
(function (root, factory) {
  'use strict';
  const node = typeof module === 'object' && module.exports;
  const api = factory(node ? require('../../wasm/pbd_core.js') : root.createPBDModule);
  if (node) module.exports = api;
  else root.PBDPhysics = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function (factory) {
  'use strict';
  let native, initialization;
  const worldCleanup = new FinalizationRegistry((pointer) => native.destroyWorld(pointer));
  const solverCleanup = new FinalizationRegistry((pointer) => native.destroySolver(pointer));

  async function init(options = {}) {
    if (!initialization) {
      initialization = (async () => {
        if (typeof factory !== 'function')
          throw new Error('wasm/pbd_core.js를 불러오지 못했습니다');
        const module = await factory(options);
        const functions = new Map();
        // Release C++ exception storage before creating a JavaScript error.
        native = new Proxy(module, {
          get(target, key) {
            const value = target[key];
            if (typeof value !== 'function') return value;
            const cached = functions.get(key);
            if (cached?.original === value) return cached.wrapped;
            const wrapped = (...args) => {
              try {
                return value(...args);
              } catch (error) {
                if (typeof error !== 'number') throw error;
                let type, message;
                try {
                  [type, message] = module.getExceptionMessage(error);
                } finally {
                  module.decrementExceptionRefcount(error);
                }
                const ErrorType = type.includes('invalid_argument')
                  ? TypeError
                  : type.includes('range_error')
                    ? RangeError
                    : Error;
                throw new ErrorType(message);
              }
            };
            functions.set(key, { original: value, wrapped });
            return wrapped;
          },
        });
        return api;
      })().catch((error) => {
        initialization = null;
        throw error;
      });
    }
    return initialization;
  }
  function ready() {
    if (!native) throw new Error('먼저 await PBDPhysics.init()을 호출하세요');
  }
  function settings(options = {}) {
    return {
      dt: options.dt ?? 1 / 60,
      substeps: options.substeps ?? 3,
      solverIterations: options.solverIterations ?? 2,
      mode: options.mode ?? 'xpbd',
      edgeCompliance: options.edgeCompliance ?? 1e-6,
      volumeCompliance: options.volumeCompliance ?? 1e-8,
      pbdStiffness: options.pbdStiffness ?? 1,
      bounds: options.bounds ?? 10,
      gravity: Array.from(options.gravity ?? [0, 0, 0]),
      restitution: options.restitution ?? 0.5,
      damping: options.damping ?? 0,
    };
  }

  const SETTING_NUMBERS = [
    'dt',
    'substeps',
    'solverIterations',
    'edgeCompliance',
    'volumeCompliance',
    'pbdStiffness',
    'bounds',
    'restitution',
    'damping',
  ];
  function sameVector(current, previous) {
    if (
      !(Array.isArray(current) || ArrayBuffer.isView(current)) ||
      current.length !== 3 ||
      !previous
    )
      return false;
    for (let axis = 0; axis < 3; axis++)
      if (typeof current[axis] !== 'number' || current[axis] !== previous[axis]) return false;
    return true;
  }
  function sameSettings(current, previous) {
    for (const name of SETTING_NUMBERS)
      if (typeof current[name] !== 'number' || current[name] !== previous[name]) return false;
    return (
      typeof current.mode === 'string' &&
      current.mode === previous.mode &&
      sameVector(current.gravity, previous.gravity)
    );
  }
  function sameObstacle(current, previous) {
    return (
      current != null &&
      typeof current.shape === 'string' &&
      current.shape === previous.shape &&
      typeof current.radius === 'number' &&
      current.radius === previous.radius &&
      typeof current.bumper === 'boolean' &&
      current.bumper === previous.bumper &&
      typeof current.bumperStrength === 'number' &&
      current.bumperStrength === previous.bumperStrength &&
      sameVector(current.position, previous.position) &&
      sameVector(current.size, previous.size)
    );
  }
  function sameObstacles(obstacles, snapshot, edited, replacement) {
    if (obstacles.length !== snapshot.length) return false;
    // Index explicitly so a hole in the public array cannot skip validation.
    for (let index = 0; index < obstacles.length; index++) {
      const obstacle = obstacles[index];
      if (
        snapshot[index].owner !== obstacle ||
        !sameObstacle(obstacle === edited ? replacement : obstacle, snapshot[index])
      )
        return false;
    }
    return true;
  }
  // Only ordinary numeric vectors use the fast path. Optional or custom inputs
  // still pass through the existing native conversion/validation on every use.
  function copyInputVector(value) {
    return Array.isArray(value) || ArrayBuffer.isView(value) ? Array.from(value) : null;
  }

  class Body {
    constructor(world, pointer, type) {
      this._world = world;
      this._ptr = pointer;
      this.type = type;
      this._dragTarget = null;
    }
    _check() {
      this._world._check();
    }
    get vertexCount() {
      return this.inverseMass.length;
    }
    get radius() {
      this._check();
      return native.bodyRadius(this._ptr);
    }
    get dragTarget() {
      return this._dragTarget;
    }
    set dragTarget(value) {
      this._check();
      native.bodyDrag(this._ptr, value);
      this._dragTarget = value === null ? null : Array.from(value);
    }
    updateBounds() {
      this._check();
      native.bodyCommand(this._ptr, 0, 0, {});
    }
    resetLambdas() {
      this._check();
      native.bodyCommand(this._ptr, 1, 0, {});
    }
    reset() {
      this._check();
      native.bodyCommand(this._ptr, 2, 0, {});
      this._dragTarget = null;
    }
    solveEdges(h, options) {
      this._check();
      native.bodyCommand(this._ptr, 3, h, settings(options));
    }
    solveVolumes(h, options) {
      this._check();
      native.bodyCommand(this._ptr, 4, h, settings(options));
    }
  }
  // Compatibility boundary: these are writable borrowed views, not owning snapshots.
  // C++ const getters do not make JS TypedArrays read-only. Topology/rest arrays must
  // stay unchanged after construction; edits to live particle arrays can bypass command
  // validation and require updateBounds()/solver.prepare() before geometric queries.
  // Retrieve fresh views after native calls: memory growth detaches old ArrayBuffers.
  // A stable C++ vector address does not keep an old JS view alive across memory growth.
  // World disposal expires every view even if the ArrayBuffer is still attached.
  [
    'positions',
    'restPositions',
    'previousPositions',
    'velocities',
    'predictedVelocities',
    'inverseMass',
    'edgeIndices',
    'tetIndices',
    'surfaceIndices',
    'restLengths',
    'restVolumes',
    'edgeLambdas',
    'volumeLambdas',
    'tetCorrectionLimits',
    'center',
    'boundsMin',
    'boundsMax',
  ].forEach((name, field) => {
    Object.defineProperty(Body.prototype, name, {
      get() {
        this._check();
        return native.bodyArray(this._ptr, field);
      },
    });
  });

  class PhysicsWorld {
    constructor(options = {}) {
      ready();
      const initialSettings = settings(options);
      Object.assign(this, initialSettings);
      this._ptr = native.createWorld({
        ...initialSettings,
        softContactOptions: options.softContactOptions,
      });
      // These snapshots own their vectors; public mirrors remain mutable for compatibility.
      this._settingsSnapshot = settings(initialSettings);
      this._obstacleSnapshot = [];
      this.backend = 'C++17 / WebAssembly';
      this.bodies = [];
      this.softBodies = [];
      this.balls = [];
      this.obstacles = [];
      this._bodyMap = new Map();
      this._obstacleIds = new WeakMap();
      this._obstacleMap = new Map();
      this._nextObstacle = 1;
      this.softContactStats = { broadPhasePairs: 0 };
      this.bumperHits = [];
      worldCleanup.register(this, this._ptr, this);
    }
    _check() {
      if (!this._ptr) throw new Error('PhysicsWorld has been disposed');
    }
    // Native validation completes before native settings or JS mirrors commit.
    configure(options = {}) {
      this._check();
      const next = settings({ ...this, ...options });
      if (!sameSettings(next, this._settingsSnapshot)) this._commitSettings(next);
      Object.assign(this, next);
      return this;
    }
    _commitSettings(next) {
      const snapshot = settings(next);
      native.configure(this._ptr, next);
      this._settingsSnapshot = snapshot;
    }
    _add(pointer, type) {
      const body = new Body(this, pointer, type);
      this.bodies.push(body);
      (type === 'soft' ? this.softBodies : this.balls).push(body);
      this._bodyMap.set(pointer, body);
      return body;
    }
    addSoftBody(data, offset = [0, 0, 0], options = {}) {
      this._check();
      return this._add(native.addSoft(this._ptr, data, offset, options), 'soft');
    }
    addBall(radius, position, velocity = [0, 0, 0], options = {}) {
      this._check();
      return this._add(native.addBall(this._ptr, radius, position, velocity, options), 'ball');
    }
    getObstacleLimits(obstacle) {
      this._check();
      return [0, 1, 2].map((axis) =>
        Math.max(
          0,
          this.bounds -
            (obstacle.shape === 'sphere' ? obstacle.radius : obstacle.size[axis] * 0.5) -
            2,
        ),
      );
    }
    _prepareObstacle(options) {
      const next = {
        shape: 'box',
        position: [0, -3, 0],
        size: [4, 2, 4],
        radius: 2,
        bumper: true,
        bumperStrength: 4,
        ...options,
      };
      next.position = Array.from(next.position);
      next.size = Array.from(next.size);
      if (!['box', 'sphere'].includes(next.shape)) throw new TypeError('Unknown obstacle shape');
      if (
        next.position.length !== 3 ||
        next.size.length !== 3 ||
        !next.position.every(Number.isFinite) ||
        !next.size.every((value) => Number.isFinite(value) && value > 0) ||
        !Number.isFinite(next.radius) ||
        next.radius <= 0
      )
        throw new TypeError('Invalid obstacle geometry');
      if (
        typeof next.bumper !== 'boolean' ||
        !Number.isFinite(next.bumperStrength) ||
        next.bumperStrength < 0 ||
        next.bumperStrength > 12
      )
        throw new RangeError('Invalid bumper settings');
      const limits = this.getObstacleLimits(next);
      next.position = next.position.map((value, axis) =>
        Math.max(-limits[axis], Math.min(limits[axis], value)),
      );
      return next;
    }
    // Configure native input first; original JS object identity survives successful edits.
    _writeObstacles(obstacles, edited = null, replacement = null) {
      if (sameObstacles(obstacles, this._obstacleSnapshot, edited, replacement)) return;
      const nextMap = new Map();
      const inputs = obstacles.map((obstacle) => {
        if (!this._obstacleIds.has(obstacle)) this._obstacleIds.set(obstacle, this._nextObstacle++);
        const nativeId = this._obstacleIds.get(obstacle);
        nextMap.set(nativeId, obstacle);
        return { ...(obstacle === edited ? replacement : obstacle), nativeId };
      });
      const snapshot = inputs.map((input, index) => ({
        owner: obstacles[index],
        shape: input.shape,
        position: copyInputVector(input.position),
        size: copyInputVector(input.size),
        radius: input.radius,
        bumper: input.bumper,
        bumperStrength: input.bumperStrength,
      }));
      native.setObstacles(this._ptr, inputs);
      this._obstacleMap = nextMap;
      this._obstacleSnapshot = snapshot;
    }
    addObstacle(options = {}) {
      this._check();
      const obstacle = this._prepareObstacle(options);
      this._writeObstacles([...this.obstacles, obstacle]);
      this.obstacles.push(obstacle);
      return obstacle;
    }
    updateObstacle(obstacle, options) {
      this._check();
      if (!this.obstacles.includes(obstacle))
        throw new TypeError('Obstacle belongs to another world');
      const next = this._prepareObstacle({ ...obstacle, ...options });
      this._writeObstacles(this.obstacles, obstacle, next);
      Object.assign(obstacle, next);
      return obstacle;
    }
    removeObstacle(obstacle) {
      this._check();
      const index = this.obstacles.indexOf(obstacle);
      if (index < 0) throw new TypeError('Obstacle belongs to another world');
      this._writeObstacles(this.obstacles.filter((item) => item !== obstacle));
      this.obstacles.splice(index, 1);
    }
    _syncInputs() {
      this._check();
      if (!sameSettings(this, this._settingsSnapshot)) this._commitSettings(settings(this));
      this._writeObstacles(this.obstacles);
    }
    _syncOutputs() {
      this.softContactStats = native.worldStats(this._ptr);
      this.bumperHits = native.worldHits(this._ptr).map((hit) => ({
        ...hit,
        body: this._bodyMap.get(hit.body),
        obstacle: this._obstacleMap.get(hit.obstacle),
      }));
    }
    get simulationTime() {
      this._check();
      return native.worldTime(this._ptr);
    }
    step(dt = this.dt) {
      this._syncInputs();
      const advanced = native.step(this._ptr, dt);
      if (advanced) this._syncOutputs();
      return advanced;
    }
    settleObstacleContacts() {
      this._syncInputs();
      native.settle(this._ptr);
    }
    moveBody(body, target) {
      this._check();
      if (body._world !== this) throw new TypeError('Body belongs to another world');
      native.moveBody(body._ptr, target);
    }
    reset() {
      this._check();
      native.reset(this._ptr);
      for (const body of this.bodies) body._dragTarget = null;
      this._syncOutputs();
    }
    dispose() {
      if (!this._ptr) return;
      worldCleanup.unregister(this);
      native.destroyWorld(this._ptr);
      this._ptr = 0;
    }
  }

  class SoftContactSolver {
    constructor(options = {}) {
      ready();
      this._options = { ...options };
      this._ptr = native.createSolver(this._options);
      this._generation = 0;
      this._bodies = new Map();
      this.stats = native.solverStats(this._ptr);
      solverCleanup.register(this, this._ptr, this);
    }
    _checkHandle() {
      if (!this._ptr) throw new Error('SoftContactSolver has been disposed');
    }
    _check() {
      this._checkHandle();
      for (const body of this._bodies.values()) body._check();
    }
    _stats() {
      this.stats = native.solverStats(this._ptr);
    }
    // A new owner set gets a candidate native cache. Identity, not pointer value alone,
    // distinguishes a new body whose allocator has reused a previous body's address.
    // Failed new-batch preparation preserves the old batch; same-owner refit retains
    // the native solver's basic exception guarantee for allocation failures.
    prepare(bodies) {
      this._checkHandle();
      if (!Array.isArray(bodies)) throw new TypeError('Expected an array of soft bodies');
      const nextBodies = new Map();
      for (const body of bodies) {
        if (!(body instanceof Body) || body.type !== 'soft')
          throw new TypeError('Expected a SoftBody');
        body._check();
        nextBodies.set(body._ptr, body);
      }
      const sameOwners =
        nextBodies.size === this._bodies.size &&
        Array.from(nextBodies).every(([pointer, body]) => this._bodies.get(pointer) === body);
      const handles = bodies.map((body) => body._ptr);
      if (sameOwners) native.prepareSolver(this._ptr, handles);
      else {
        const candidate = native.createSolver(this._options);
        try {
          native.prepareSolver(candidate, handles);
        } catch (error) {
          native.destroySolver(candidate);
          throw error;
        }
        const previous = this._ptr;
        solverCleanup.unregister(this);
        solverCleanup.register(this, candidate, this);
        this._ptr = candidate;
        native.destroySolver(previous);
      }
      this._bodies = nextBodies;
      this._generation++;
      this._stats();
      return this;
    }
    generateContacts(a, b) {
      this._check();
      a._check();
      b._check();
      if (!this._bodies.has(a._ptr) || !this._bodies.has(b._ptr))
        throw new Error('Call prepare before generating contacts');
      const result = native.generateContacts(this._ptr, a._ptr, b._ptr).map((c) => ({
        ...c,
        a: this._bodies.get(c.a),
        b: this._bodies.get(c.b),
        _solver: this,
        _preparation: this._generation,
      }));
      this._stats();
      return result;
    }
    projectContacts(contacts, contactsOut = []) {
      this._check();
      if (contacts.some((c) => c._solver !== this))
        throw new TypeError('Contacts belong to another solver');
      if (contacts.some((contact) => contact._preparation !== this._generation))
        throw new Error('Expired contact batch');
      const solved = native.projectContacts(this._ptr, contacts);
      for (const i of solved) contactsOut.push(contacts[i]);
      this._stats();
      return solved.length;
    }
    solvePair(a, b, contactsOut = []) {
      this.prepare([a, b]);
      const contacts = this.generateContacts(a, b);
      this.projectContacts(contacts, contactsOut);
      return contacts;
    }
    queryPoint(body, point) {
      this._check();
      body._check();
      if (!this._bodies.has(body._ptr)) throw new Error('Call prepare before querying contacts');
      const result = native.queryPoint(this._ptr, body._ptr, point);
      this._stats();
      return result;
    }
    getBodyInfo(body) {
      this._check();
      body._check();
      if (this._bodies.get(body._ptr) !== body) return null;
      return native.bodyInfo(this._ptr, body._ptr);
    }
    dispose() {
      if (!this._ptr) return;
      solverCleanup.unregister(this);
      native.destroySolver(this._ptr);
      this._ptr = 0;
      this._bodies.clear();
    }
  }
  function tetraVolume(p, a = 0, b = 1, c = 2, d = 3) {
    ready();
    return native.tetraVolume(p, a, b, c, d);
  }
  function closestPointTriangle(p, a, b, c) {
    ready();
    return native.closestPointTriangle(p, a, b, c);
  }
  const api = { init, PhysicsWorld, SoftContactSolver, tetraVolume, closestPointTriangle };
  return api;
});
