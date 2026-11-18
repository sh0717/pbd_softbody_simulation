import { runCleanup } from '../lifecycle.mjs';

const CAMERA_FOV = 32;
const MIN_CAMERA_ASPECT = 1.12;
const OBSTACLE_COLOR = 0x30365f;
const OBSTACLE_SELECTED_COLOR = 0x4d5983;
const OBSTACLE_OUTLINE_COLOR = 0x838dc4;
const BUMPER_OUTLINE_COLOR = 0xf9ca7b;
const SELECTED_OUTLINE_COLOR = 0x76f1ea;
const BUMPER_FLASH_COLOR = 0xc26828;
const BUMPER_FLASH_MS = 180;

// Owns Three.js objects and their lifetime. Simulation state is only read here.
export class PhysicsScene {
  constructor({ THREE, viewport, bounds, bunnyData }) {
    try {
      this.THREE = THREE;
      this.viewport = viewport;
      this.bunnyData = bunnyData;

      this.scene = new this.THREE.Scene();
      this.scene.fog = new this.THREE.Fog(0x11142d, 21, 52);

      this.renderer = new this.THREE.WebGLRenderer({ antialias: true, alpha: true });
      this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
      this.renderer.outputEncoding = this.THREE.sRGBEncoding;
      this.renderer.toneMapping = this.THREE.ACESFilmicToneMapping;
      this.renderer.toneMappingExposure = 0.9;
      this.renderer.shadowMap.enabled = true;
      this.renderer.shadowMap.type = this.THREE.PCFSoftShadowMap;
      this.renderer.domElement.setAttribute('aria-label', '연체 물리 3D 시뮬레이션');

      this.viewport.appendChild(this.renderer.domElement);

      this.camera = new this.THREE.PerspectiveCamera(CAMERA_FOV, 1, 0.1, 150);
      this.controls = new this.THREE.OrbitControls(this.camera, this.renderer.domElement);
      this.controls.enableDamping = true;
      this.controls.dampingFactor = 0.08;
      this.controls.minDistance = 4;
      this.controls.maxDistance = 45;
      this.controls.maxPolarAngle = Math.PI * 0.88;

      this.bodyViews = new Map();
      this.obstacleViews = new Map();

      this.restoreCamera();
      this.scene.add(new this.THREE.HemisphereLight(0xe6f0ff, 0x52446d, 0.95));

      const key = new this.THREE.DirectionalLight(0xf2ecff, 1.56);
      key.position.set(-4, 11, 8);
      key.castShadow = true;
      key.shadow.mapSize.set(2048, 2048);
      Object.assign(key.shadow.camera, {
        left: -14,
        right: 14,
        top: 14,
        bottom: -14,
        near: 0.5,
        far: 50,
      });
      key.shadow.bias = -0.0003;
      key.shadow.normalBias = 0.03;

      this.scene.add(key);

      const rim = new this.THREE.DirectionalLight(0xb6adff, 1.05);
      rim.position.set(5, 6, -10);
      this.scene.add(rim);

      const floor = new this.THREE.Mesh(
        new this.THREE.PlaneGeometry(100, 100),
        new this.THREE.MeshStandardMaterial({ color: 0x1b1e36, roughness: 0.92 }),
      );
      floor.material.color.convertSRGBToLinear();
      floor.rotation.x = -Math.PI / 2;
      floor.position.y = -bounds - 0.02;
      floor.receiveShadow = true;

      this.scene.add(floor);

      const grid = new this.THREE.GridHelper(
        bounds * 2,
        20,
        new this.THREE.Color(0x807cbd).convertSRGBToLinear(),
        new this.THREE.Color(0x505a91).convertSRGBToLinear(),
      );
      grid.position.y = -bounds + 0.002;
      grid.material.transparent = true;
      grid.material.opacity = 0.75;

      this.scene.add(grid);

      const boundsGeometry = new this.THREE.BoxGeometry(bounds * 2, bounds * 2, bounds * 2);
      const boundary = new this.THREE.LineSegments(
        new this.THREE.EdgesGeometry(boundsGeometry),
        new this.THREE.LineBasicMaterial({
          color: 0x7774af,
          transparent: true,
          opacity: 0.3,
          toneMapped: false,
        }),
      );
      boundary.material.color.convertSRGBToLinear();

      boundsGeometry.dispose();
      this.scene.add(boundary);

      this.resizeObserver = new ResizeObserver(() => this.resize());
      this.resizeObserver.observe(this.viewport);
      this.resize();
      this.disposed = false;
    } catch (error) {
      runCleanup([() => this.dispose()], error);
    }
  }

  restoreCamera() {
    const damping = this.controls.enableDamping;

    this.controls.enableDamping = false;
    this.controls.update();

    this.camera.position.set(10.5, 3.5, 14);
    this.controls.target.set(0, -1, 0);
    this.controls.update();

    this.controls.enableDamping = damping;
    this.camera.updateMatrixWorld(true);
  }

  syncBody(body) {
    const mesh = this.bodyViews.get(body);

    if (body.type === 'ball') mesh.position.fromArray(body.positions);
    else {
      // Reacquire Wasm positions before copying; memory growth expires previous views.
      mesh.geometry.attributes.position.array.set(body.positions);
      mesh.geometry.attributes.position.needsUpdate = true;
      mesh.geometry.computeVertexNormals();
      mesh.geometry.computeBoundingSphere();
    }
  }

  addBody(body, color, wireframe) {
    let geometry;
    let material;
    let mesh;

    try {
      if (body.type === 'soft') {
        geometry = new this.THREE.BufferGeometry();

        const attribute = new this.THREE.BufferAttribute(
          new Float32Array(body.positions.length),
          3,
        );
        attribute.setUsage(this.THREE.DynamicDrawUsage);
        geometry.setAttribute('position', attribute);
        geometry.setIndex(
          new this.THREE.BufferAttribute(new Uint16Array(this.bunnyData.tetSurfaceTriIds), 1),
        );
      } else geometry = new this.THREE.SphereGeometry(body.radius, 32, 24);

      material = new this.THREE.MeshStandardMaterial({
        color,
        roughness: body.type === 'soft' ? 0.41 : 0.28,
        metalness: body.type === 'soft' ? 0.04 : 0.16,
      });
      material.color.convertSRGBToLinear();
      material.wireframe = body.type === 'soft' && wireframe;

      mesh = new this.THREE.Mesh(geometry, material);
      mesh.castShadow = true;
      mesh.receiveShadow = true;
      mesh.userData.body = body;
      mesh.userData.baseColor = color;

      this.bodyViews.set(body, mesh);
      this.scene.add(mesh);
      this.syncBody(body);
    } catch (error) {
      runCleanup(
        [
          () => {
            if (mesh) this.scene.remove(mesh);
          },
          () => {
            if (mesh && this.bodyViews?.get(body) === mesh) this.bodyViews.delete(body);
          },
          () => geometry?.dispose(),
          () => material?.dispose(),
        ],
        error,
      );
    }
  }

  addObstacle(obstacle) {
    let geometry;
    let material;
    let outlineGeometry;
    let outlineMaterial;
    let mesh;

    try {
      geometry = new this.THREE.BoxGeometry(1, 1, 1);
      material = new this.THREE.MeshStandardMaterial({
        color: OBSTACLE_COLOR,
        roughness: 0.62,
        metalness: 0.12,
      });
      mesh = new this.THREE.Mesh(geometry, material);
      material.color.convertSRGBToLinear();
      mesh.castShadow = mesh.receiveShadow = true;
      mesh.userData.obstacle = obstacle;

      outlineGeometry = new this.THREE.EdgesGeometry(geometry);
      outlineMaterial = new this.THREE.LineBasicMaterial({ toneMapped: false });

      const outline = new this.THREE.LineSegments(outlineGeometry, outlineMaterial);
      // Picking must reach the obstacle mesh rather than its visual outline.
      outline.raycast = () => {};
      outline.scale.setScalar(1.002);
      mesh.add(outline);
      mesh.userData.outline = outline;

      this.obstacleViews.set(obstacle, mesh);
      this.scene.add(mesh);
    } catch (error) {
      runCleanup(
        [
          () => {
            if (mesh) this.scene.remove(mesh);
          },
          () => {
            if (mesh && this.obstacleViews?.get(obstacle) === mesh)
              this.obstacleViews.delete(obstacle);
          },
          () => geometry?.dispose(),
          () => material?.dispose(),
          () => outlineGeometry?.dispose(),
          () => outlineMaterial?.dispose(),
        ],
        error,
      );
    }
  }

  syncObstacle(obstacle) {
    const mesh = this.obstacleViews.get(obstacle);
    mesh.position.fromArray(obstacle.position);
    mesh.scale.fromArray(obstacle.size);
    mesh.updateMatrixWorld(true);
  }

  removeObstacle(obstacle) {
    const mesh = this.obstacleViews.get(obstacle);
    const outline = mesh.userData?.outline;

    runCleanup([
      () => this.scene.remove(mesh),
      () => mesh.geometry.dispose(),
      () => mesh.material.dispose(),
      () => outline?.geometry.dispose(),
      () => outline?.material.dispose(),
      () => this.obstacleViews.delete(obstacle),
    ]);
  }

  selectObstacle(obstacle) {
    for (const [item, mesh] of this.obstacleViews) {
      mesh.material.color
        .setHex(item === obstacle ? OBSTACLE_SELECTED_COLOR : OBSTACLE_COLOR)
        .convertSRGBToLinear();
    }
  }

  selectBody(body, selected) {
    this.bodyViews.get(body).material.emissive.setHex(selected ? 0x271c0f : 0);
  }

  setWireframe(enabled) {
    for (const [body, mesh] of this.bodyViews)
      if (body.type === 'soft') mesh.material.wireframe = enabled;
  }

  flashObstacle(obstacle, now) {
    const mesh = this.obstacleViews.get(obstacle);

    if (mesh) mesh.userData.flashUntil = now + BUMPER_FLASH_MS;
  }

  resetFlashes() {
    for (const mesh of this.obstacleViews.values()) mesh.userData.flashUntil = 0;
  }

  render(now, selectedObstacle) {
    this.controls.update();

    for (const [obstacle, mesh] of this.obstacleViews) {
      const pulse = Math.max(0, ((mesh.userData.flashUntil || 0) - now) / BUMPER_FLASH_MS);
      const selected = obstacle === selectedObstacle;
      let outlineColor = OBSTACLE_OUTLINE_COLOR;

      if (selected) outlineColor = SELECTED_OUTLINE_COLOR;
      else if (obstacle.bumper) outlineColor = BUMPER_OUTLINE_COLOR;

      mesh.userData.outline.material.color.setHex(outlineColor).convertSRGBToLinear();
      mesh.material.emissive.setHex(pulse > 0 ? BUMPER_FLASH_COLOR : 0);
      mesh.material.emissiveIntensity = pulse > 0 ? pulse : 1;
    }

    this.renderer.render(this.scene, this.camera);
  }

  resize() {
    const width = Math.max(1, this.viewport.clientWidth);
    const height = Math.max(1, this.viewport.clientHeight);

    this.renderer.setSize(width, height);
    this.camera.aspect = width / height;

    // Keep enough horizontal coverage for the initial layout on narrow screens.
    const { degToRad, radToDeg } = this.THREE.MathUtils;
    const halfFovTangent = Math.tan(degToRad(CAMERA_FOV / 2));
    const aspectScale = Math.max(1, MIN_CAMERA_ASPECT / this.camera.aspect);

    this.camera.fov = radToDeg(2 * Math.atan(halfFovTangent * aspectScale));
    this.camera.updateProjectionMatrix();
  }

  dispose() {
    if (this.disposed) return;

    this.disposed = true;
    runCleanup([
      () => this.resizeObserver?.disconnect(),
      () => this.controls?.dispose(),
      () => {
        // Collect before releasing: a throwing dispose listener must not stop
        // traversal, and a shared resource is released only once.
        const resources = new Set();

        this.scene?.traverse((object) => {
          if (object.geometry) resources.add(object.geometry);

          if (Array.isArray(object.material))
            for (const material of object.material) resources.add(material);
          else if (object.material) resources.add(object.material);

          if (object.shadow?.map) resources.add(object.shadow.map);
        });
        runCleanup(Array.from(resources, (resource) => () => resource.dispose()));
      },
      () => this.bodyViews?.clear(),
      () => this.obstacleViews?.clear(),
      () => this.renderer?.dispose(),
      () => this.renderer?.domElement.remove(),
    ]);
  }
}
