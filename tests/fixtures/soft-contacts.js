/* Local inter-body contacts: tet spatial hash, exterior-surface BVH, mass-weighted PBD. */
(function (root, factory) {
    'use strict';
    const api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.PBDSoftContacts = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
    'use strict';

    function faceKey(a, b, c) {
        if (a > b) [a, b] = [b, a];
        if (b > c) [b, c] = [c, b];
        if (a > b) [a, b] = [b, a];
        return `${a},${b},${c}`;
    }

    // Exact 51-bit packing for the common coordinate range, with collision-free string
    // fallback outside it. Numeric keys avoid allocating strings for every tet/cell.
    function cellKey(x, y, z) {
        if (x >= -65536 && x < 65536 && y >= -65536 && y < 65536 && z >= -65536 && z < 65536) {
            return (x + 65536) * 17179869184 + (y + 65536) * 131072 + z + 65536;
        }
        return `${x},${y},${z}`;
    }

    function buildTopology(body) {
        const ids = body.tetIndices, count = body.positions.length / 3, faces = new Map();
        for (let i = 0; i < ids.length; i += 4) {
            const tet = [ids[i], ids[i + 1], ids[i + 2], ids[i + 3]];
            if (new Set(tet).size !== 4 || tet.some(v => v < 0 || v >= count)) throw new TypeError('Invalid contact tetrahedron');
            for (let opposite = 0; opposite < 4; opposite++) {
                const face = tet.filter((_, j) => j !== opposite), key = faceKey(...face);
                const previous = faces.get(key);
                if (previous) {
                    previous.count++;
                    if (previous.count > 2) throw new TypeError('Non-manifold tetrahedral face');
                } else faces.set(key, {face, opposite: tet[opposite], count: 1});
            }
        }
        const boundary = [...faces.values()].filter(face => face.count === 1);
        const boundaryKeys = new Set(boundary.map(item => faceKey(...item.face)));
        if (body.surfaceIndices && body.surfaceIndices.length) {
            const supplied = body.surfaceIndices;
            if (supplied.length % 3) throw new TypeError('Invalid supplied surface indices');
            const suppliedKeys = new Set();
            for (let i = 0; i < supplied.length; i += 3) {
                const key = faceKey(supplied[i], supplied[i + 1], supplied[i + 2]);
                if (!boundaryKeys.has(key) || suppliedKeys.has(key)) throw new TypeError('Supplied surface differs from tetrahedron boundary');
                suppliedKeys.add(key);
            }
            if (suppliedKeys.size !== boundaryKeys.size) throw new TypeError('Supplied surface omits exterior faces');
        }
        const surfaceIndices = new Uint32Array(boundary.length * 3), oppositeVertices = new Uint32Array(boundary.length);
        for (let i = 0; i < boundary.length; i++) {
            surfaceIndices.set(boundary[i].face, i * 3); oppositeVertices[i] = boundary[i].opposite;
        }
        const surfaceVertices = Uint32Array.from(new Set(surfaceIndices));
        return {surfaceIndices, oppositeVertices, surfaceVertices};
    }

    function segmentClosest(px, py, pz, ax, ay, az, bx, by, bz) {
        const dx = bx - ax, dy = by - ay, dz = bz - az;
        const denominator = dx * dx + dy * dy + dz * dz;
        return denominator > 0 ? Math.max(0, Math.min(1, ((px - ax) * dx + (py - ay) * dy + (pz - az) * dz) / denominator)) : 0;
    }

    // Ericson Voronoi regions; degenerate triangles fall back to their closest edge.
    function triangleClosest(px, py, pz, ax, ay, az, bx, by, bz, cx, cy, cz, out) {
        const abx = bx - ax, aby = by - ay, abz = bz - az;
        const acx = cx - ax, acy = cy - ay, acz = cz - az;
        const apx = px - ax, apy = py - ay, apz = pz - az;
        const nx = aby * acz - abz * acy, ny = abz * acx - abx * acz, nz = abx * acy - aby * acx;
        const scaleSquared = Math.max(abx * abx + aby * aby + abz * abz, acx * acx + acy * acy + acz * acz);
        let u, v, w;
        if (nx * nx + ny * ny + nz * nz <= 1e-28 * scaleSquared * scaleSquared) {
            let best = Infinity;
            const vertices = [[ax, ay, az], [bx, by, bz], [cx, cy, cz]];
            const pairs = [[0, 1], [1, 2], [2, 0]];
            for (const [i, j] of pairs) {
                const a = vertices[i], b = vertices[j];
                const t = segmentClosest(px, py, pz, ...a, ...b);
                const x = a[0] + t * (b[0] - a[0]), y = a[1] + t * (b[1] - a[1]), z = a[2] + t * (b[2] - a[2]);
                const d2 = (px - x) ** 2 + (py - y) ** 2 + (pz - z) ** 2;
                if (d2 < best) {
                    best = d2; u = 0; v = 0; w = 0;
                    const bary = [0, 0, 0]; bary[i] = 1 - t; bary[j] = t;
                    [u, v, w] = bary;
                }
            }
        } else {
            const d1 = abx * apx + aby * apy + abz * apz, d2 = acx * apx + acy * apy + acz * apz;
            if (d1 <= 0 && d2 <= 0) { u = 1; v = 0; w = 0; }
            else {
                const bpx = px - bx, bpy = py - by, bpz = pz - bz;
                const d3 = abx * bpx + aby * bpy + abz * bpz, d4 = acx * bpx + acy * bpy + acz * bpz;
                const vc = d1 * d4 - d3 * d2;
                const cpx = px - cx, cpy = py - cy, cpz = pz - cz;
                const d5 = abx * cpx + aby * cpy + abz * cpz, d6 = acx * cpx + acy * cpy + acz * cpz;
                if (d3 >= 0 && d4 <= d3) { u = 0; v = 1; w = 0; }
                else if (vc <= 0 && d1 >= 0 && d3 <= 0) { v = d1 / (d1 - d3); u = 1 - v; w = 0; }
                else if (d6 >= 0 && d5 <= d6) { u = 0; v = 0; w = 1; }
                else {
                    const vb = d5 * d2 - d1 * d6, va = d3 * d6 - d5 * d4;
                    if (vb <= 0 && d2 >= 0 && d6 <= 0) { w = d2 / (d2 - d6); u = 1 - w; v = 0; }
                    else if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) {
                        w = (d4 - d3) / (d4 - d3 + d5 - d6); u = 0; v = 1 - w;
                    } else {
                        const inverse = 1 / (va + vb + vc); v = vb * inverse; w = vc * inverse; u = 1 - v - w;
                    }
                }
            }
        }
        out[0] = u; out[1] = v; out[2] = w;
        out[3] = u * ax + v * bx + w * cx; out[4] = u * ay + v * by + w * cy; out[5] = u * az + v * bz + w * cz;
        out[6] = (px - out[3]) ** 2 + (py - out[4]) ** 2 + (pz - out[5]) ** 2;
        return out;
    }

    function closestPointTriangle(point, a, b, c) {
        const result = triangleClosest(...point, ...a, ...b, ...c, new Float64Array(7));
        return {weights: result.slice(0, 3), point: result.slice(3, 6), distanceSquared: result[6]};
    }

    function boxDistanceSquared(node, x, y, z) {
        const dx = Math.max(node.minX - x, 0, x - node.maxX), dy = Math.max(node.minY - y, 0, y - node.maxY), dz = Math.max(node.minZ - z, 0, z - node.maxZ);
        return dx * dx + dy * dy + dz * dz;
    }

    class SoftContactSolver {
        constructor(options = {}) {
            this.cellSize = options.cellSize;
            this.maxCellsPerTet = options.maxCellsPerTet ?? 512;
            this.relativeEpsilon = options.relativeEpsilon ?? 1e-10;
            this.contactMargin = options.contactMargin ?? 0;
            if (this.cellSize !== undefined && !(Number.isFinite(this.cellSize) && this.cellSize > 0)) throw new RangeError('Invalid spatial hash cell size');
            if (!(Number.isInteger(this.maxCellsPerTet) && this.maxCellsPerTet > 0)) throw new RangeError('Invalid spatial hash cell limit');
            if (!(Number.isFinite(this.relativeEpsilon) && this.relativeEpsilon > 0 && Number.isFinite(this.contactMargin) && this.contactMargin >= 0)) throw new RangeError('Invalid contact tolerance');
            this._entries = new WeakMap(); this.stats = this._emptyStats();
        }
        _emptyStats() {
            return {candidateTests: 0, contacts: 0, generatedContacts: 0, maxPenetration: 0, overflowTets: 0, hashCells: 0, surfaceTriangleTests: 0};
        }
        _createEntry(body) {
            const topology = buildTopology(body), rest = body.restPositions || body.positions;
            let edgeLength = 0, edgeCount = 0;
            const ids = body.tetIndices;
            for (let o = 0; o < ids.length; o += 4) {
                for (let a = 0; a < 4; a++) {
                    for (let b = a + 1; b < 4; b++) {
                        const i = ids[o + a] * 3, j = ids[o + b] * 3;
                        const length = Math.hypot(rest[i] - rest[j], rest[i + 1] - rest[j + 1], rest[i + 2] - rest[j + 2]);
                        if (length > 0) { edgeLength += length; edgeCount++; }
                    }
                }
            }
            const cellSize = this.cellSize || Math.max(1e-12, edgeLength / (edgeCount || 1));
            const entry = {...topology, body, cellSize, hash: new Map(), version: 0, activeCells: 0, overflow: [], tetData: new Float64Array(ids.length / 4 * 13),
                tetBounds: new Float64Array(ids.length / 4 * 6), heads: new Int32Array(0), nodeTets: new Int32Array(Math.max(1, ids.length * 4)),
                nodeNext: new Int32Array(Math.max(1, ids.length * 4)), order: Array.from({length: topology.surfaceIndices.length / 3}, (_, i) => i)};
            entry.root = this._buildBVH(entry, 0, entry.order.length);
            this._entries.set(body, entry); return entry;
        }
        _buildBVH(entry, start, end) {
            if (start === end) return null;
            const node = {start, end, left: null, right: null};
            if (end - start <= 8) return node;
            const p = entry.body.restPositions || entry.body.positions, ids = entry.surfaceIndices;
            const centers = new Map();
            let minX = Infinity, minY = Infinity, minZ = Infinity, maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
            for (let i = start; i < end; i++) {
                const triangle = entry.order[i], o = triangle * 3, a = ids[o] * 3, b = ids[o + 1] * 3, c = ids[o + 2] * 3;
                const center = [(p[a] + p[b] + p[c]) / 3, (p[a + 1] + p[b + 1] + p[c + 1]) / 3, (p[a + 2] + p[b + 2] + p[c + 2]) / 3];
                centers.set(triangle, center);
                minX = Math.min(minX, center[0]); maxX = Math.max(maxX, center[0]);
                minY = Math.min(minY, center[1]); maxY = Math.max(maxY, center[1]);
                minZ = Math.min(minZ, center[2]); maxZ = Math.max(maxZ, center[2]);
            }
            const range = [maxX - minX, maxY - minY, maxZ - minZ], axis = range.indexOf(Math.max(...range));
            const sorted = entry.order.slice(start, end).sort((a, b) => centers.get(a)[axis] - centers.get(b)[axis]);
            for (let i = start; i < end; i++) entry.order[i] = sorted[i - start];
            const middle = (start + end) >> 1;
            node.left = this._buildBVH(entry, start, middle); node.right = this._buildBVH(entry, middle, end);
            return node;
        }
        _refitBVH(entry, node) {
            if (!node) return;
            if (node.left) {
                this._refitBVH(entry, node.left); this._refitBVH(entry, node.right);
                node.minX = Math.min(node.left.minX, node.right.minX); node.maxX = Math.max(node.left.maxX, node.right.maxX);
                node.minY = Math.min(node.left.minY, node.right.minY); node.maxY = Math.max(node.left.maxY, node.right.maxY);
                node.minZ = Math.min(node.left.minZ, node.right.minZ); node.maxZ = Math.max(node.left.maxZ, node.right.maxZ);
                return;
            }
            const p = entry.body.positions, ids = entry.surfaceIndices;
            node.minX = Infinity; node.minY = Infinity; node.minZ = Infinity;
            node.maxX = -Infinity; node.maxY = -Infinity; node.maxZ = -Infinity;
            for (let i = node.start; i < node.end; i++) {
                const offset = entry.order[i] * 3;
                for (let v = 0; v < 3; v++) {
                    const k = ids[offset + v] * 3;
                    node.minX = Math.min(node.minX, p[k]); node.maxX = Math.max(node.maxX, p[k]);
                    node.minY = Math.min(node.minY, p[k + 1]); node.maxY = Math.max(node.maxY, p[k + 1]);
                    node.minZ = Math.min(node.minZ, p[k + 2]); node.maxZ = Math.max(node.maxZ, p[k + 2]);
                }
            }
        }
        _refitHash(entry) {
            const p = entry.body.positions, ids = entry.body.tetIndices, data = entry.tetData, bounds = entry.tetBounds;
            const inverseCell = 1 / entry.cellSize, epsilon = this.relativeEpsilon;
            // Retain cell arrays across refits. Bound cached history for moving bodies.
            if (entry.hash.size > Math.max(20000, entry.activeCells * 4)) entry.hash.clear();
            entry.version++; entry.activeCells = 0; entry.overflow.length = 0;
            let minX = Infinity, minY = Infinity, minZ = Infinity, maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
            for (let i = 0; i < p.length; i += 3) {
                minX = Math.min(minX, p[i]); minY = Math.min(minY, p[i + 1]); minZ = Math.min(minZ, p[i + 2]);
                maxX = Math.max(maxX, p[i]); maxY = Math.max(maxY, p[i + 1]); maxZ = Math.max(maxZ, p[i + 2]);
            }
            const gridPad = epsilon * Math.hypot(maxX - minX, maxY - minY, maxZ - minZ) * 2;
            const gx = Math.floor((minX - gridPad) * inverseCell), gy = Math.floor((minY - gridPad) * inverseCell), gz = Math.floor((minZ - gridPad) * inverseCell);
            const nx = Math.floor((maxX + gridPad) * inverseCell) - gx + 1, ny = Math.floor((maxY + gridPad) * inverseCell) - gy + 1, nz = Math.floor((maxZ + gridPad) * inverseCell) - gz + 1;
            const cellCount = nx * ny * nz;
            entry.grid = Number.isSafeInteger(gx) && Number.isSafeInteger(gy) && Number.isSafeInteger(gz) && Number.isSafeInteger(cellCount)
                && cellCount > 0 && cellCount <= 262144 ? {x: gx, y: gy, z: gz, nx, ny, nz} : null;
            entry.nodeCount = 0;
            if (entry.grid) {
                if (entry.heads.length < cellCount) entry.heads = new Int32Array(cellCount);
                entry.heads.fill(-1, 0, cellCount);
            }
            for (let tet = 0; tet < ids.length / 4; tet++) {
                const o = tet * 4, i = ids[o] * 3, j = ids[o + 1] * 3, k = ids[o + 2] * 3, l = ids[o + 3] * 3, d = tet * 13, b = tet * 6;
                const ax = p[j] - p[i], ay = p[j + 1] - p[i + 1], az = p[j + 2] - p[i + 2];
                const bx = p[k] - p[i], by = p[k + 1] - p[i + 1], bz = p[k + 2] - p[i + 2];
                const cx = p[l] - p[i], cy = p[l + 1] - p[i + 1], cz = p[l + 2] - p[i + 2];
                const n1x = by * cz - bz * cy, n1y = bz * cx - bx * cz, n1z = bx * cy - by * cx;
                const determinant = ax * n1x + ay * n1y + az * n1z;
                const scale = Math.sqrt(Math.max(ax * ax + ay * ay + az * az, bx * bx + by * by + bz * bz, cx * cx + cy * cy + cz * cz));
                data[d + 12] = 0;
                if (!Number.isFinite(determinant) || !(scale > 0) || Math.abs(determinant) <= epsilon * scale * scale * scale) continue;
                const inverse = 1 / determinant;
                data[d] = p[i]; data[d + 1] = p[i + 1]; data[d + 2] = p[i + 2];
                data[d + 3] = n1x * inverse; data[d + 4] = n1y * inverse; data[d + 5] = n1z * inverse;
                data[d + 6] = (cy * az - cz * ay) * inverse; data[d + 7] = (cz * ax - cx * az) * inverse; data[d + 8] = (cx * ay - cy * ax) * inverse;
                data[d + 9] = (ay * bz - az * by) * inverse; data[d + 10] = (az * bx - ax * bz) * inverse; data[d + 11] = (ax * by - ay * bx) * inverse;
                data[d + 12] = 1;
                // Expand numerical bounds by the same relative tolerance used by barycentric tests.
                const pad = epsilon * scale;
                bounds[b] = Math.min(p[i], p[j], p[k], p[l]) - pad; bounds[b + 3] = Math.max(p[i], p[j], p[k], p[l]) + pad;
                bounds[b + 1] = Math.min(p[i + 1], p[j + 1], p[k + 1], p[l + 1]) - pad; bounds[b + 4] = Math.max(p[i + 1], p[j + 1], p[k + 1], p[l + 1]) + pad;
                bounds[b + 2] = Math.min(p[i + 2], p[j + 2], p[k + 2], p[l + 2]) - pad; bounds[b + 5] = Math.max(p[i + 2], p[j + 2], p[k + 2], p[l + 2]) + pad;
                const x0 = Math.floor(bounds[b] * inverseCell), x1 = Math.floor(bounds[b + 3] * inverseCell);
                const y0 = Math.floor(bounds[b + 1] * inverseCell), y1 = Math.floor(bounds[b + 4] * inverseCell);
                const z0 = Math.floor(bounds[b + 2] * inverseCell), z1 = Math.floor(bounds[b + 5] * inverseCell);
                if (!Number.isSafeInteger(x0) || !Number.isSafeInteger(x1) || !Number.isSafeInteger(y0) || !Number.isSafeInteger(y1) || !Number.isSafeInteger(z0) || !Number.isSafeInteger(z1)
                    || (x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1) > this.maxCellsPerTet) {
                    entry.overflow.push(tet); continue;
                }
                for (let x = x0; x <= x1; x++) {
                    for (let y = y0; y <= y1; y++) {
                        for (let z = z0; z <= z1; z++) {
                            if (entry.grid) {
                                if (entry.nodeCount === entry.nodeTets.length) {
                                    const tets = new Int32Array(entry.nodeTets.length * 2), next = new Int32Array(tets.length);
                                    tets.set(entry.nodeTets); next.set(entry.nodeNext); entry.nodeTets = tets; entry.nodeNext = next;
                                }
                                const cell = (x - gx) + nx * ((y - gy) + ny * (z - gz)), index = entry.nodeCount++;
                                if (entry.heads[cell] === -1) entry.activeCells++;
                                entry.nodeTets[index] = tet; entry.nodeNext[index] = entry.heads[cell]; entry.heads[cell] = index;
                                continue;
                            }
                            const key = cellKey(x, y, z);
                            let cell = entry.hash.get(key);
                            if (!cell) { cell = {version: entry.version, ids: []}; entry.hash.set(key, cell); entry.activeCells++; }
                            else if (cell.version !== entry.version) { cell.ids.length = 0; cell.version = entry.version; entry.activeCells++; }
                            cell.ids.push(tet);
                        }
                    }
                }
            }
        }
        prepare(bodies) {
            this.stats = this._emptyStats();
            for (const body of bodies) {
                const entry = this._entries.get(body) || this._createEntry(body);
                this._refitHash(entry); this._refitBVH(entry, entry.root);
                this.stats.overflowTets += entry.overflow.length; this.stats.hashCells += entry.activeCells;
            }
            return this;
        }
        getBodyInfo(body) {
            const entry = this._entries.get(body);
            if (!entry) return null;
            return {surfaceIndices: entry.surfaceIndices, surfaceVertices: entry.surfaceVertices, tetCount: body.tetIndices.length / 4,
                surfaceTriangleCount: entry.surfaceIndices.length / 3, cellSize: entry.cellSize, hashCellCount: entry.activeCells, overflowTets: entry.overflow.length};
        }
        _inside(entry, tet, x, y, z) {
            const data = entry.tetData, d = tet * 13;
            if (!data[d + 12]) return false;
            const bounds = entry.tetBounds, b = tet * 6;
            if (x < bounds[b] || x > bounds[b + 3] || y < bounds[b + 1] || y > bounds[b + 4] || z < bounds[b + 2] || z > bounds[b + 5]) return false;
            this.stats.candidateTests++;
            x -= data[d]; y -= data[d + 1]; z -= data[d + 2];
            const u = data[d + 3] * x + data[d + 4] * y + data[d + 5] * z;
            const v = data[d + 6] * x + data[d + 7] * y + data[d + 8] * z;
            const w = data[d + 9] * x + data[d + 10] * y + data[d + 11] * z;
            const epsilon = this.relativeEpsilon;
            return u >= -epsilon && v >= -epsilon && w >= -epsilon && 1 - u - v - w >= -epsilon;
        }
        _query(entry, x, y, z, stopAtFirst) {
            if (entry.grid) {
                const grid = entry.grid, ix = Math.floor(x / entry.cellSize) - grid.x, iy = Math.floor(y / entry.cellSize) - grid.y, iz = Math.floor(z / entry.cellSize) - grid.z;
                const found = [];
                if (ix >= 0 && ix < grid.nx && iy >= 0 && iy < grid.ny && iz >= 0 && iz < grid.nz) {
                    for (let node = entry.heads[ix + grid.nx * (iy + grid.ny * iz)]; node !== -1; node = entry.nodeNext[node]) {
                        const tet = entry.nodeTets[node];
                        if (this._inside(entry, tet, x, y, z)) { found.push(tet); if (stopAtFirst) return found; }
                    }
                }
                for (const tet of entry.overflow) {
                    if (this._inside(entry, tet, x, y, z)) { found.push(tet); if (stopAtFirst) return found; }
                }
                return found;
            }
            const bucket = entry.hash.get(cellKey(Math.floor(x / entry.cellSize), Math.floor(y / entry.cellSize), Math.floor(z / entry.cellSize)));
            const cell = bucket?.version === entry.version ? bucket.ids : [];
            const found = [];
            for (const tet of cell) {
                if (this._inside(entry, tet, x, y, z)) { found.push(tet); if (stopAtFirst) return found; }
            }
            for (const tet of entry.overflow) {
                if (this._inside(entry, tet, x, y, z)) { found.push(tet); if (stopAtFirst) return found; }
            }
            return found;
        }
        _contains(entry, x, y, z) {
            if (entry.grid) {
                const grid = entry.grid, ix = Math.floor(x / entry.cellSize) - grid.x, iy = Math.floor(y / entry.cellSize) - grid.y, iz = Math.floor(z / entry.cellSize) - grid.z;
                if (ix >= 0 && ix < grid.nx && iy >= 0 && iy < grid.ny && iz >= 0 && iz < grid.nz) {
                    for (let node = entry.heads[ix + grid.nx * (iy + grid.ny * iz)]; node !== -1; node = entry.nodeNext[node]) {
                        if (this._inside(entry, entry.nodeTets[node], x, y, z)) return true;
                    }
                }
            } else {
                const bucket = entry.hash.get(cellKey(Math.floor(x / entry.cellSize), Math.floor(y / entry.cellSize), Math.floor(z / entry.cellSize)));
                if (bucket?.version === entry.version) {
                    for (const tet of bucket.ids) if (this._inside(entry, tet, x, y, z)) return true;
                }
            }
            for (const tet of entry.overflow) if (this._inside(entry, tet, x, y, z)) return true;
            return false;
        }
        queryPoint(body, point) {
            const entry = this._entries.get(body);
            if (!entry) throw new Error('Call prepare before querying contacts');
            return this._query(entry, point[0], point[1], point[2], false);
        }
        _nearestSurface(entry, x, y, z) {
            if (!entry.root) return null;
            const stack = [entry.root], p = entry.body.positions, ids = entry.surfaceIndices;
            const scratch = new Float64Array(7), best = new Float64Array(7);
            let bestDistance = Infinity, bestTriangle = -1;
            while (stack.length) {
                const node = stack.pop();
                if (boxDistanceSquared(node, x, y, z) > bestDistance) continue;
                if (node.left) {
                    const leftDistance = boxDistanceSquared(node.left, x, y, z), rightDistance = boxDistanceSquared(node.right, x, y, z);
                    if (leftDistance < rightDistance) { stack.push(node.right); stack.push(node.left); }
                    else { stack.push(node.left); stack.push(node.right); }
                } else {
                    for (let j = node.start; j < node.end; j++) {
                        const triangle = entry.order[j], o = triangle * 3, a = ids[o] * 3, b = ids[o + 1] * 3, c = ids[o + 2] * 3;
                        triangleClosest(x, y, z, p[a], p[a + 1], p[a + 2], p[b], p[b + 1], p[b + 2], p[c], p[c + 1], p[c + 2], scratch);
                        this.stats.surfaceTriangleTests++;
                        if (scratch[6] < bestDistance) { bestDistance = scratch[6]; best.set(scratch); bestTriangle = triangle; }
                    }
                }
            }
            return bestTriangle < 0 ? null : {triangle: bestTriangle, result: best};
        }
        _generateDirection(a, b, contacts) {
            const target = this._entries.get(b), source = this._entries.get(a);
            if (!target?.root) return;
            const p = a.positions;
            const vertices = source.surfaceVertices.length ? source.surfaceVertices : Array.from({length: p.length / 3}, (_, i) => i);
            for (const ai of vertices) {
                const k = ai * 3, x = p[k], y = p[k + 1], z = p[k + 2];
                if (!this._contains(target, x, y, z)) continue;
                const nearest = this._nearestSurface(target, x, y, z);
                if (!nearest) continue;
                const hit = nearest.result, depth = Math.sqrt(hit[6]);
                if (!(depth > target.cellSize * this.relativeEpsilon)) continue;
                const nx = (hit[3] - x) / depth, ny = (hit[4] - y) / depth, nz = (hit[5] - z) / depth;
                const o = nearest.triangle * 3, ids = target.surfaceIndices;
                const indices = [ids[o], ids[o + 1], ids[o + 2]], weights = [hit[0], hit[1], hit[2]];
                const av = a.predictedVelocities || a.velocities, bv = b.predictedVelocities || b.velocities;
                let vx = av[k], vy = av[k + 1], vz = av[k + 2];
                for (let j = 0; j < 3; j++) {
                    const q = indices[j] * 3, weight = weights[j];
                    vx -= weight * bv[q]; vy -= weight * bv[q + 1]; vz -= weight * bv[q + 2];
                }
                contacts.push({kind: 'soft', a, ai, b, indices, weights, nx, ny, nz, incoming: vx * nx + vy * ny + vz * nz, depth, margin: this.contactMargin});
                this.stats.generatedContacts++; this.stats.maxPenetration = Math.max(this.stats.maxPenetration, depth);
            }
        }
        generateContacts(a, b) {
            if (!this._entries.has(a) || !this._entries.has(b)) throw new Error('Call prepare before generating contacts');
            const contacts = [];
            this._generateDirection(a, b, contacts); this._generateDirection(b, a, contacts);
            return contacts;
        }
        projectContacts(contacts, contactsOut = []) {
            let solved = 0;
            for (const contact of contacts) {
                const {a, ai, b, indices, weights, nx, ny, nz} = contact;
                const p = a.positions, q = b.positions, k = ai * 3;
                let x = p[k], y = p[k + 1], z = p[k + 2], denominator = a.inverseMass[ai];
                for (let j = 0; j < 3; j++) {
                    const i = indices[j] * 3, weight = weights[j];
                    x -= weight * q[i]; y -= weight * q[i + 1]; z -= weight * q[i + 2];
                    denominator += b.inverseMass[indices[j]] * weight * weight;
                }
                const constraint = x * nx + y * ny + z * nz - (contact.margin || 0);
                if (constraint >= 0 || !(denominator > 0)) continue;
                const lambda = -constraint / denominator, wa = a.inverseMass[ai];
                p[k] += wa * lambda * nx; p[k + 1] += wa * lambda * ny; p[k + 2] += wa * lambda * nz;
                for (let j = 0; j < 3; j++) {
                    const i = indices[j] * 3, correction = b.inverseMass[indices[j]] * weights[j] * lambda;
                    q[i] -= correction * nx; q[i + 1] -= correction * ny; q[i + 2] -= correction * nz;
                }
                contactsOut.push(contact); solved++; this.stats.contacts++;
            }
            return solved;
        }
        solvePair(a, b, contactsOut = []) {
            this.prepare([a, b]);
            const contacts = this.generateContacts(a, b);
            this.projectContacts(contacts, contactsOut);
            return contacts;
        }
    }

    return {SoftContactSolver, closestPointTriangle};
});
