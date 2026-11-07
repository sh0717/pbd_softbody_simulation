/* Typed-array PBD / XPBD core. No rendering, browser, or Python dependency. */
(function (root, factory) {
    'use strict';
    const api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.PBDPhysics = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
    'use strict';

    function vector(value, fallback = [0, 0, 0]) {
        const result = Float64Array.from(value || fallback);
        if (result.length !== 3 || !result.every(Number.isFinite)) throw new TypeError('Expected a finite vec3');
        return result;
    }
    function indices(values, width, vertexCount) {
        const input = values || [];
        if (input.length % width || !Array.from(input).every(i => Number.isInteger(i) && i >= 0 && i < vertexCount)) {
            throw new TypeError('Invalid mesh indices');
        }
        return Uint32Array.from(input);
    }
    function masses(value, count) {
        const result = new Float64Array(count);
        if (value === undefined) result.fill(1);
        else if (typeof value === 'number') result.fill(value);
        else {
            if (value.length !== count) throw new TypeError('Invalid inverseMass length');
            result.set(value);
        }
        if (!result.every(w => Number.isFinite(w) && w >= 0)) throw new TypeError('Invalid inverseMass');
        return result;
    }
    function tetraVolume(p, i0 = 0, i1 = 1, i2 = 2, i3 = 3) {
        i0 *= 3; i1 *= 3; i2 *= 3; i3 *= 3;
        const ax = p[i1] - p[i0], ay = p[i1 + 1] - p[i0 + 1], az = p[i1 + 2] - p[i0 + 2];
        const bx = p[i2] - p[i0], by = p[i2 + 1] - p[i0 + 1], bz = p[i2 + 2] - p[i0 + 2];
        const cx = p[i3] - p[i0], cy = p[i3 + 1] - p[i0 + 1], cz = p[i3 + 2] - p[i0 + 2];
        return (ax * (by * cz - bz * cy) + ay * (bz * cx - bx * cz) + az * (bx * cy - by * cx)) / 6;
    }

    class SoftBody {
        constructor(data, offset = [0, 0, 0], options = {}) {
            const vertices = data.verts || data.positions;
            if (!vertices || !vertices.length || vertices.length % 3 || !Array.from(vertices).every(Number.isFinite)) {
                throw new TypeError('Invalid mesh vertices');
            }
            this.type = 'soft';
            this.positions = Float64Array.from(vertices);
            this.vertexCount = this.positions.length / 3;
            const translation = vector(offset);
            for (let i = 0; i < this.positions.length; i++) this.positions[i] += translation[i % 3];
            this.restPositions = this.positions.slice();
            this.previousPositions = this.positions.slice();
            this.velocities = new Float64Array(this.positions.length);
            this.predictedVelocities = new Float64Array(this.positions.length);
            const velocity = vector(options.velocity);
            for (let i = 0; i < this.velocities.length; i++) this.velocities[i] = velocity[i % 3];
            this.inverseMass = masses(options.inverseMass, this.vertexCount);
            this.edgeIndices = indices(data.tetEdgeIds || data.edgeIndices, 2, this.vertexCount);
            this.tetIndices = indices(data.tetIds || data.tetIndices, 4, this.vertexCount);
            this.restLengths = new Float64Array(this.edgeIndices.length / 2);
            this.restVolumes = new Float64Array(this.tetIndices.length / 4);
            this.edgeLambdas = new Float64Array(this.restLengths.length);
            this.volumeLambdas = new Float64Array(this.restVolumes.length);
            this.tetCorrectionLimits = new Float64Array(this.restVolumes.length);
            for (let e = 0; e < this.restLengths.length; e++) {
                const i = this.edgeIndices[e * 2] * 3, j = this.edgeIndices[e * 2 + 1] * 3;
                this.restLengths[e] = Math.hypot(this.positions[j] - this.positions[i], this.positions[j + 1] - this.positions[i + 1], this.positions[j + 2] - this.positions[i + 2]);
            }
            for (let t = 0; t < this.restVolumes.length; t++) {
                const o = t * 4;
                this.restVolumes[t] = tetraVolume(this.positions, this.tetIndices[o], this.tetIndices[o + 1], this.tetIndices[o + 2], this.tetIndices[o + 3]);
                let diameterSquared = 0;
                for (let a = 0; a < 4; a++) {
                    for (let b = a + 1; b < 4; b++) {
                        const i = this.tetIndices[o + a] * 3, j = this.tetIndices[o + b] * 3;
                        const x = this.positions[i] - this.positions[j], y = this.positions[i + 1] - this.positions[j + 1], z = this.positions[i + 2] - this.positions[j + 2];
                        diameterSquared = Math.max(diameterSquared, x * x + y * y + z * z);
                    }
                }
                this.tetCorrectionLimits[t] = Math.sqrt(diameterSquared);
            }
            this.center = new Float64Array(3);
            this.radius = 0;
            this.dragTarget = null;
            this.updateBounds();
        }

        updateBounds() {
            const p = this.positions, c = this.center;
            let minX = Infinity, minY = Infinity, minZ = Infinity, maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
            for (let i = 0; i < p.length; i += 3) {
                minX = Math.min(minX, p[i]); maxX = Math.max(maxX, p[i]);
                minY = Math.min(minY, p[i + 1]); maxY = Math.max(maxY, p[i + 1]);
                minZ = Math.min(minZ, p[i + 2]); maxZ = Math.max(maxZ, p[i + 2]);
            }
            c[0] = (minX + maxX) * 0.5; c[1] = (minY + maxY) * 0.5; c[2] = (minZ + maxZ) * 0.5;
            let radiusSquared = 0;
            for (let i = 0; i < p.length; i += 3) {
                const x = p[i] - c[0], y = p[i + 1] - c[1], z = p[i + 2] - c[2];
                radiusSquared = Math.max(radiusSquared, x * x + y * y + z * z);
            }
            this.radius = Math.sqrt(radiusSquared);
        }

        resetLambdas() { this.edgeLambdas.fill(0); this.volumeLambdas.fill(0); }

        solveEdges(h, settings) {
            const p = this.positions, ids = this.edgeIndices, w = this.inverseMass;
            const alpha = settings.mode === 'xpbd' ? settings.edgeCompliance / (h * h) : 0;
            const stiffness = settings.mode === 'pbd' ? settings.pbdStiffness : 1;
            for (let e = 0; e < this.restLengths.length; e++) {
                const a = ids[e * 2], b = ids[e * 2 + 1], wa = w[a], wb = w[b];
                if (wa + wb === 0) continue;
                const i = a * 3, j = b * 3;
                let x = p[j] - p[i], y = p[j + 1] - p[i + 1], z = p[j + 2] - p[i + 2];
                const length = Math.sqrt(x * x + y * y + z * z);
                if (length > 1e-15) { x /= length; y /= length; z /= length; }
                else {
                    const restLength = this.restLengths[e];
                    if (restLength <= 1e-15) continue;
                    x = (this.restPositions[j] - this.restPositions[i]) / restLength;
                    y = (this.restPositions[j + 1] - this.restPositions[i + 1]) / restLength;
                    z = (this.restPositions[j + 2] - this.restPositions[i + 2]) / restLength;
                }
                const delta = (-(length - this.restLengths[e]) - alpha * this.edgeLambdas[e]) / (wa + wb + alpha) * stiffness;
                this.edgeLambdas[e] += delta;
                p[i] -= wa * delta * x; p[i + 1] -= wa * delta * y; p[i + 2] -= wa * delta * z;
                p[j] += wb * delta * x; p[j + 1] += wb * delta * y; p[j + 2] += wb * delta * z;
            }
        }

        solveVolumes(h, settings) {
            const p = this.positions, ids = this.tetIndices, w = this.inverseMass;
            const alpha = settings.mode === 'xpbd' ? settings.volumeCompliance / (h * h) : 0;
            const stiffness = settings.mode === 'pbd' ? settings.pbdStiffness : 1;
            for (let t = 0; t < this.restVolumes.length; t++) {
                const o = t * 4, a = ids[o], b = ids[o + 1], c = ids[o + 2], d = ids[o + 3];
                const i = a * 3, j = b * 3, k = c * 3, l = d * 3;
                const ax = p[j] - p[i], ay = p[j + 1] - p[i + 1], az = p[j + 2] - p[i + 2];
                const bx = p[k] - p[i], by = p[k + 1] - p[i + 1], bz = p[k + 2] - p[i + 2];
                const cx = p[l] - p[i], cy = p[l + 1] - p[i + 1], cz = p[l + 2] - p[i + 2];
                // V and its gradients use the same physical /6 convention. Signed V repairs inversion.
                const g1x = (by * cz - bz * cy) / 6, g1y = (bz * cx - bx * cz) / 6, g1z = (bx * cy - by * cx) / 6;
                const g2x = (cy * az - cz * ay) / 6, g2y = (cz * ax - cx * az) / 6, g2z = (cx * ay - cy * ax) / 6;
                const g3x = (ay * bz - az * by) / 6, g3y = (az * bx - ax * bz) / 6, g3z = (ax * by - ay * bx) / 6;
                const g0x = -(g1x + g2x + g3x), g0y = -(g1y + g2y + g3y), g0z = -(g1z + g2z + g3z);
                const constraint = ax * g1x + ay * g1y + az * g1z - this.restVolumes[t];
                const n0 = g0x * g0x + g0y * g0y + g0z * g0z, n1 = g1x * g1x + g1y * g1y + g1z * g1z;
                const n2 = g2x * g2x + g2y * g2y + g2z * g2z, n3 = g3x * g3x + g3y * g3y + g3z * g3z;
                const denominator = w[a] * n0 + w[b] * n1 + w[c] * n2 + w[d] * n3;
                // Fully collapsed tetrahedra have no usable gradient. Do not divide by zero.
                if (denominator <= 1e-300) continue;
                let delta = (-constraint - alpha * this.volumeLambdas[t]) / (denominator + alpha) * stiffness;
                // A near-collapsed nonlinear tetrahedron can produce an enormous Newton step.
                // Bound each projection to one rest diameter, scaling lambda with the same step.
                const maximumGradient = Math.max(w[a] * w[a] * n0, w[b] * w[b] * n1, w[c] * w[c] * n2, w[d] * w[d] * n3);
                const limit = this.tetCorrectionLimits[t];
                if (delta * delta * maximumGradient > limit * limit) delta *= limit / (Math.abs(delta) * Math.sqrt(maximumGradient));
                this.volumeLambdas[t] += delta;
                p[i] += w[a] * delta * g0x; p[i + 1] += w[a] * delta * g0y; p[i + 2] += w[a] * delta * g0z;
                p[j] += w[b] * delta * g1x; p[j + 1] += w[b] * delta * g1y; p[j + 2] += w[b] * delta * g1z;
                p[k] += w[c] * delta * g2x; p[k + 1] += w[c] * delta * g2y; p[k + 2] += w[c] * delta * g2z;
                p[l] += w[d] * delta * g3x; p[l + 1] += w[d] * delta * g3y; p[l + 2] += w[d] * delta * g3z;
            }
        }

        reset() {
            this.positions.set(this.restPositions); this.previousPositions.set(this.restPositions);
            this.velocities.fill(0); this.predictedVelocities.fill(0); this.resetLambdas();
            this.dragTarget = null; this.updateBounds();
        }
    }

    class Ball {
        constructor(radius, position, velocity = [0, 0, 0], options = {}) {
            if (!(Number.isFinite(radius) && radius > 0)) throw new TypeError('Invalid ball radius');
            this.type = 'ball'; this.vertexCount = 1; this.radius = radius;
            this.positions = vector(position); this.restPositions = this.positions.slice();
            this.previousPositions = this.positions.slice(); this.velocities = vector(velocity);
            this.predictedVelocities = this.velocities.slice(); this.inverseMass = masses(options.inverseMass, 1);
            this.center = this.positions; this.dragTarget = null;
        }
        reset() {
            this.positions.set(this.restPositions); this.previousPositions.set(this.restPositions);
            this.velocities.fill(0); this.predictedVelocities.fill(0); this.dragTarget = null;
        }
    }

    class PhysicsWorld {
        constructor(options = {}) {
            this.dt = options.dt ?? 1 / 60; this.substeps = options.substeps ?? 3;
            this.solverIterations = options.solverIterations ?? 2; this.mode = options.mode ?? 'xpbd';
            this.edgeCompliance = options.edgeCompliance ?? 1e-6; this.volumeCompliance = options.volumeCompliance ?? 1e-8;
            this.pbdStiffness = options.pbdStiffness ?? 1; this.bounds = options.bounds ?? 10;
            this.gravity = vector(options.gravity); this.restitution = options.restitution ?? 0.5; this.damping = options.damping ?? 0;
            this.bodies = []; this.softBodies = []; this.balls = []; this.obstacles = [];
            this._contacts = [];
            this._validate(this.dt);
        }
        _validate(dt) {
            if (!(Number.isFinite(dt) && dt >= 0)) throw new RangeError('dt must be finite and nonnegative');
            if (!(Number.isInteger(this.substeps) && this.substeps > 0 && Number.isInteger(this.solverIterations) && this.solverIterations > 0)) {
                throw new RangeError('substeps and solverIterations must be positive integers');
            }
            if (this.mode !== 'xpbd' && this.mode !== 'pbd') throw new RangeError('Unknown solver mode');
            for (const key of ['edgeCompliance', 'volumeCompliance', 'damping']) {
                if (!(Number.isFinite(this[key]) && this[key] >= 0)) throw new RangeError(`${key} must be nonnegative`);
            }
            if (!(Number.isFinite(this.bounds) && this.bounds > 0)) throw new RangeError('bounds must be positive');
            if (!(Number.isFinite(this.restitution) && this.restitution >= 0 && this.restitution <= 1)) throw new RangeError('Invalid restitution');
            if (!(Number.isFinite(this.pbdStiffness) && this.pbdStiffness >= 0 && this.pbdStiffness <= 1)) throw new RangeError('Invalid PBD stiffness');
        }
        addSoftBody(data, offset = [0, 0, 0], options = {}) {
            const body = new SoftBody(data, offset, options); this.softBodies.push(body); this.bodies.push(body); return body;
        }
        addBall(radius, position, velocity = [0, 0, 0], options = {}) {
            const body = new Ball(radius, position, velocity, options); this.balls.push(body); this.bodies.push(body); return body;
        }
        _predict(body, h) {
            const p = body.positions, v = body.velocities, w = body.inverseMass;
            body.previousPositions.set(p);
            if (body.type === 'soft') { body.resetLambdas(); body.updateBounds(); }
            const drag = body.dragTarget, center = body.center, gain = body.type === 'soft' ? 0.4 : 0.3;
            const attenuation = Math.exp(-this.damping * h);
            for (let i = 0; i < w.length; i++) {
                for (let axis = 0; axis < 3; axis++) {
                    const k = i * 3 + axis;
                    if (!w[i]) { v[k] = 0; continue; }
                    v[k] = drag ? gain * (drag[axis] - center[axis]) : (v[k] + this.gravity[axis] * h) * attenuation;
                    p[k] += h * v[k];
                }
            }
            body.predictedVelocities.set(v);
        }
        _record(a, ai, b, bi, nx, ny, nz) {
            const av = a.predictedVelocities, bv = b ? b.predictedVelocities : null;
            const k = ai * 3, l = bi * 3;
            const incoming = (av[k] - (bv ? bv[l] : 0)) * nx + (av[k + 1] - (bv ? bv[l + 1] : 0)) * ny + (av[k + 2] - (bv ? bv[l + 2] : 0)) * nz;
            this._contacts.push({a, ai, b, bi, nx, ny, nz, incoming});
        }
        _projectParticlePair(a, ai, b, bi, radius) {
            const i = ai * 3, j = bi * 3, p = a.positions, q = b.positions;
            const wa = a.inverseMass[ai], wb = b.inverseMass[bi];
            if (wa + wb === 0) return;
            let x = p[i] - q[j], y = p[i + 1] - q[j + 1], z = p[i + 2] - q[j + 2];
            const distanceSquared = x * x + y * y + z * z;
            if (distanceSquared > radius * radius) return;
            const distance = Math.sqrt(distanceSquared);
            if (distance > 1e-15) { x /= distance; y /= distance; z /= distance; }
            else { x = 1; y = 0; z = 0; }
            const correction = Math.max(0, radius - distance) / (wa + wb);
            p[i] += wa * correction * x; p[i + 1] += wa * correction * y; p[i + 2] += wa * correction * z;
            q[j] -= wb * correction * x; q[j + 1] -= wb * correction * y; q[j + 2] -= wb * correction * z;
            this._record(a, ai, b, bi, x, y, z);
        }
        _softPair(a, b) {
            // Conservative bounding-sphere contact; no tetrahedron narrow phase or self collision.
            let x = a.center[0] - b.center[0], y = a.center[1] - b.center[1], z = a.center[2] - b.center[2];
            const radius = a.radius + b.radius, distanceSquared = x * x + y * y + z * z;
            if (distanceSquared >= radius * radius) return;
            let massA = 0, massB = 0;
            for (const w of a.inverseMass) { if (!w) { massA = Infinity; break; } massA += 1 / w; }
            for (const w of b.inverseMass) { if (!w) { massB = Infinity; break; } massB += 1 / w; }
            const wa = 1 / massA, wb = 1 / massB;
            if (wa + wb === 0) return;
            const distance = Math.sqrt(distanceSquared);
            if (distance > 1e-15) { x /= distance; y /= distance; z /= distance; }
            else { x = 1; y = 0; z = 0; }
            const correction = (radius - distance) / (wa + wb);
            const velocityA = this._bulkVelocity(a, a.predictedVelocities);
            const velocityB = this._bulkVelocity(b, b.predictedVelocities);
            const incoming = (velocityA[0] - velocityB[0]) * x + (velocityA[1] - velocityB[1]) * y + (velocityA[2] - velocityB[2]) * z;
            this._contacts.push({kind: 'body', a, b, wa, wb, nx: x, ny: y, nz: z, incoming});
            for (let i = 0; i < a.positions.length; i += 3) {
                a.positions[i] += wa * correction * x; a.positions[i + 1] += wa * correction * y; a.positions[i + 2] += wa * correction * z;
            }
            for (let i = 0; i < b.positions.length; i += 3) {
                b.positions[i] -= wb * correction * x; b.positions[i + 1] -= wb * correction * y; b.positions[i + 2] -= wb * correction * z;
            }
            a.updateBounds(); b.updateBounds();
        }
        _dynamicContacts() {
            for (const soft of this.softBodies) soft.updateBounds();
            for (let i = 0; i < this.balls.length; i++) {
                for (let j = i + 1; j < this.balls.length; j++) this._projectParticlePair(this.balls[i], 0, this.balls[j], 0, this.balls[i].radius + this.balls[j].radius);
            }
            for (const soft of this.softBodies) {
                for (const ball of this.balls) {
                    const dx = soft.center[0] - ball.positions[0], dy = soft.center[1] - ball.positions[1], dz = soft.center[2] - ball.positions[2];
                    const radius = soft.radius + ball.radius;
                    if (dx * dx + dy * dy + dz * dz > radius * radius) continue;
                    for (let i = 0; i < soft.vertexCount; i++) this._projectParticlePair(soft, i, ball, 0, ball.radius);
                }
            }
            // Refresh after soft-ball positional corrections, before soft-soft broad phase.
            for (const soft of this.softBodies) soft.updateBounds();
            for (let i = 0; i < this.softBodies.length; i++) {
                for (let j = i + 1; j < this.softBodies.length; j++) this._softPair(this.softBodies[i], this.softBodies[j]);
            }
        }
        _staticContacts(body, record = true) {
            const p = body.positions, radius = body.type === 'ball' ? body.radius : 0;
            const limit = Math.max(0, this.bounds - radius);
            for (let i = 0; i < body.vertexCount; i++) {
                if (!body.inverseMass[i]) continue;
                const k = i * 3;
                for (let axis = 0; axis < 3; axis++) {
                    if (p[k + axis] >= limit) {
                        p[k + axis] = limit;
                        if (record) this._record(body, i, null, 0, axis === 0 ? -1 : 0, axis === 1 ? -1 : 0, axis === 2 ? -1 : 0);
                    } else if (p[k + axis] <= -limit) {
                        p[k + axis] = -limit;
                        if (record) this._record(body, i, null, 0, axis === 0 ? 1 : 0, axis === 1 ? 1 : 0, axis === 2 ? 1 : 0);
                    }
                }
                for (const obstacle of this.obstacles) {
                    const center = obstacle.position;
                    if (obstacle.shape === 'sphere') {
                        const r = obstacle.radius + radius;
                        let x = p[k] - center[0], y = p[k + 1] - center[1], z = p[k + 2] - center[2];
                        const distanceSquared = x * x + y * y + z * z;
                        if (distanceSquared > r * r) continue;
                        const distance = Math.sqrt(distanceSquared);
                        if (distance > 1e-15) { x /= distance; y /= distance; z /= distance; }
                        else { x = 1; y = 0; z = 0; }
                        p[k] = center[0] + r * x; p[k + 1] = center[1] + r * y; p[k + 2] = center[2] + r * z;
                        if (record) this._record(body, i, null, 0, x, y, z);
                    } else if (obstacle.shape === 'box') {
                        // Exact sphere-AABB closest point outside; nearest exit face inside.
                        const hx = obstacle.size[0] * 0.5, hy = obstacle.size[1] * 0.5, hz = obstacle.size[2] * 0.5;
                        const x = p[k] - center[0], y = p[k + 1] - center[1], z = p[k + 2] - center[2];
                        const qx = Math.max(-hx, Math.min(hx, x)), qy = Math.max(-hy, Math.min(hy, y)), qz = Math.max(-hz, Math.min(hz, z));
                        let nx = x - qx, ny = y - qy, nz = z - qz;
                        const distanceSquared = nx * nx + ny * ny + nz * nz;
                        if (distanceSquared > radius * radius) continue;
                        if (distanceSquared > 1e-30) {
                            const distance = Math.sqrt(distanceSquared), correction = radius - distance;
                            nx /= distance; ny /= distance; nz /= distance;
                            p[k] += correction * nx; p[k + 1] += correction * ny; p[k + 2] += correction * nz;
                        } else {
                            const ex = hx - Math.abs(x), ey = hy - Math.abs(y), ez = hz - Math.abs(z);
                            nx = 0; ny = 0; nz = 0;
                            if (ex <= ey && ex <= ez) { nx = x >= 0 ? 1 : -1; p[k] = center[0] + nx * (hx + radius); }
                            else if (ey <= ez) { ny = y >= 0 ? 1 : -1; p[k + 1] = center[1] + ny * (hy + radius); }
                            else { nz = z >= 0 ? 1 : -1; p[k + 2] = center[2] + nz * (hz + radius); }
                        }
                        if (record) this._record(body, i, null, 0, nx, ny, nz);
                    }
                }
            }
        }
        _restitute() {
            // Targets derive from incoming substep velocities, never forces scaled by 1/h.
            for (const contact of this._contacts) {
                const {a, ai, b, bi, nx, ny, nz, incoming} = contact;
                const target = incoming < 0 ? -this.restitution * incoming : incoming;
                if (contact.kind === 'body') {
                    const velocityA = this._bulkVelocity(a, a.velocities);
                    const velocityB = this._bulkVelocity(b, b.velocities);
                    const current = (velocityA[0] - velocityB[0]) * nx + (velocityA[1] - velocityB[1]) * ny + (velocityA[2] - velocityB[2]) * nz;
                    const impulse = (target - current) / (contact.wa + contact.wb);
                    for (let k = 0; k < a.velocities.length; k += 3) {
                        a.velocities[k] += contact.wa * impulse * nx;
                        a.velocities[k + 1] += contact.wa * impulse * ny;
                        a.velocities[k + 2] += contact.wa * impulse * nz;
                    }
                    for (let k = 0; k < b.velocities.length; k += 3) {
                        b.velocities[k] -= contact.wb * impulse * nx;
                        b.velocities[k + 1] -= contact.wb * impulse * ny;
                        b.velocities[k + 2] -= contact.wb * impulse * nz;
                    }
                    continue;
                }
                const i = ai * 3, j = bi * 3, av = a.velocities, bv = b ? b.velocities : null;
                const wa = a.inverseMass[ai], wb = b ? b.inverseMass[bi] : 0;
                if (wa + wb === 0) continue;
                const current = (av[i] - (bv ? bv[j] : 0)) * nx + (av[i + 1] - (bv ? bv[j + 1] : 0)) * ny + (av[i + 2] - (bv ? bv[j + 2] : 0)) * nz;
                // Remove normal depenetration/h velocity before setting restitution. Exact target
                // prevents stationary overlap from becoming an artificial high-speed launch.
                const impulse = (target - current) / (wa + wb);
                av[i] += wa * impulse * nx; av[i + 1] += wa * impulse * ny; av[i + 2] += wa * impulse * nz;
                if (bv) { bv[j] -= wb * impulse * nx; bv[j + 1] -= wb * impulse * ny; bv[j + 2] -= wb * impulse * nz; }
            }
        }
        _bulkVelocity(body, velocities) {
            let x = 0, y = 0, z = 0, mass = 0;
            for (let i = 0; i < body.vertexCount; i++) {
                if (!body.inverseMass[i]) return [0, 0, 0];
                const m = 1 / body.inverseMass[i], k = i * 3;
                x += m * velocities[k]; y += m * velocities[k + 1]; z += m * velocities[k + 2]; mass += m;
            }
            return [x / mass, y / mass, z / mass];
        }
        step(dt = this.dt) {
            this._validate(dt);
            if (dt === 0) return false;
            const h = dt / this.substeps;
            for (let substep = 0; substep < this.substeps; substep++) {
                this._contacts.length = 0;
                for (const body of this.bodies) this._predict(body, h);
                for (let iteration = 0; iteration < this.solverIterations; iteration++) {
                    for (const body of this.softBodies) { body.solveEdges(h, this); body.solveVolumes(h, this); }
                    this._dynamicContacts();
                    for (const body of this.bodies) this._staticContacts(body);
                }
                for (const body of this.bodies) {
                    const p = body.positions, previous = body.previousPositions, v = body.velocities;
                    for (let i = 0; i < p.length; i++) v[i] = body.inverseMass[Math.floor(i / 3)] ? (p[i] - previous[i]) / h : 0;
                    if (body.type === 'soft') body.updateBounds();
                }
                this._restitute();
            }
            return true;
        }
        settleObstacleContacts() {
            const before = this.bodies.map(body => body.positions.slice());
            // Bounded sweeps resolve compatible overlapping obstacles after paused UI edits.
            for (let iteration = 0; iteration < 8; iteration++) {
                for (const body of this.bodies) this._staticContacts(body, false);
            }
            for (let b = 0; b < this.bodies.length; b++) {
                const body = this.bodies[b], original = before[b];
                body.previousPositions.set(body.positions);
                for (let i = 0; i < body.positions.length; i += 3) {
                    if (body.positions[i] === original[i] && body.positions[i + 1] === original[i + 1] && body.positions[i + 2] === original[i + 2]) continue;
                    // UI obstacle edits clear only particles displaced by the edit. Unaffected
                    // objects keep moving when an obstacle or another body is added during Run.
                    body.velocities[i] = 0; body.velocities[i + 1] = 0; body.velocities[i + 2] = 0;
                    body.predictedVelocities[i] = 0; body.predictedVelocities[i + 1] = 0; body.predictedVelocities[i + 2] = 0;
                }
                if (body.type === 'soft') body.updateBounds();
            }
        }
        reset() { for (const body of this.bodies) body.reset(); }
    }

    return {PhysicsWorld, SoftBody, Ball, tetraVolume};
});
