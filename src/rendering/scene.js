import { runCleanup } from '../lifecycle.mjs';

// Owns Three.js objects and their lifetime. Simulation state is only read here.
export class PhysicsScene {
  constructor({ THREE, viewport, bounds, bunnyData }) {
    try {
      this.THREE = THREE;
      this.viewport = viewport;
      this.bunnyData = bunnyData;
      this.scene = new this.THREE.Scene();
      this.scene.background = new this.THREE.Color(0xe7ece7);
      this.scene.fog = new this.THREE.Fog(0xe7ece7, 30, 68);
      this.renderer = new this.THREE.WebGLRenderer({ antialias: true, alpha: false });
      this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
      this.renderer.outputEncoding = this.THREE.sRGBEncoding;
      this.renderer.toneMapping = this.THREE.ACESFilmicToneMapping;
      this.renderer.toneMappingExposure = 1.15;
      this.renderer.shadowMap.enabled = true;
      this.renderer.shadowMap.type = this.THREE.PCFSoftShadowMap;
      this.renderer.domElement.setAttribute('aria-label', '토끼, 구와 장애물의 3D 시뮬레이션');
      this.viewport.appendChild(this.renderer.domElement);
      this.camera = new this.THREE.PerspectiveCamera(40, 1, 0.1, 150);
      this.controls = new this.THREE.OrbitControls(this.camera, this.renderer.domElement);
      this.controls.enableDamping = true;
      this.controls.dampingFactor = 0.08;
      this.controls.minDistance = 4;
      this.controls.maxDistance = 45;
      this.controls.maxPolarAngle = Math.PI * 0.88;
      this.bodyViews = new Map();
      this.obstacleViews = new Map();

      this.restoreCamera();
      this.scene.add(new this.THREE.HemisphereLight(0xffffff, 0xa2b4a0, 0.8));
      const key = new this.THREE.DirectionalLight(0xfff2de, 1.65);
      key.position.set(7, 15, 8);
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
      const fill = new this.THREE.DirectionalLight(0xc6e0e9, 0.65);
      fill.position.set(-10, 5, -8);
      this.scene.add(fill);
      const floor = new this.THREE.Mesh(
        new this.THREE.PlaneGeometry(100, 100),
        new this.THREE.MeshStandardMaterial({ color: 0xc8d2c5, roughness: 0.95 }),
      );
      floor.material.color.convertSRGBToLinear();
      floor.rotation.x = -Math.PI / 2;
      floor.position.y = -bounds - 0.02;
      floor.receiveShadow = true;
      this.scene.add(floor);
      const grid = new this.THREE.GridHelper(20, 20, 0x98ad99, 0xb5c3b1);
      grid.position.y = -bounds + 0.002;
      grid.material.transparent = true;
      grid.material.opacity = 0.5;
      this.scene.add(grid);
      const boundsGeometry = new this.THREE.BoxGeometry(20, 20, 20);
      const boundary = new this.THREE.LineSegments(
        new this.THREE.EdgesGeometry(boundsGeometry),
        new this.THREE.LineBasicMaterial({ color: 0x9cae9e, transparent: true, opacity: 0.2 }),
      );
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
    this.camera.position.set(10.5, 6.7, 14.5);
    this.controls.target.set(0, -2, 0);
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
        roughness: body.type === 'soft' ? 0.48 : 0.24,
        metalness: body.type === 'soft' ? 0.03 : 0.18,
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

  makeObstacleGeometry(shape) {
    return shape === 'sphere'
      ? new this.THREE.SphereGeometry(1, 40, 28)
      : new this.THREE.BoxGeometry(1, 1, 1);
  }

  addObstacle(obstacle) {
    let geometry;
    let material;
    let mesh;
    try {
      geometry = this.makeObstacleGeometry(obstacle.shape);
      material = new this.THREE.MeshStandardMaterial({
        color: 0x91a184,
        roughness: 0.65,
        metalness: 0.03,
      });
      mesh = new this.THREE.Mesh(geometry, material);
      material.color.convertSRGBToLinear();
      mesh.castShadow = mesh.receiveShadow = true;
      mesh.userData.obstacle = obstacle;
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
        ],
        error,
      );
    }
  }

  syncObstacle(obstacle) {
    const mesh = this.obstacleViews.get(obstacle);
    mesh.position.fromArray(obstacle.position);
    if (obstacle.shape === 'sphere') mesh.scale.setScalar(obstacle.radius);
    else mesh.scale.fromArray(obstacle.size);
    mesh.updateMatrixWorld(true);
  }

  replaceObstacleShape(obstacle) {
    const mesh = this.obstacleViews.get(obstacle);
    const geometry = this.makeObstacleGeometry(obstacle.shape);
    const previous = mesh.geometry;
    mesh.geometry = geometry;
    previous.dispose();
  }

  removeObstacle(obstacle) {
    const mesh = this.obstacleViews.get(obstacle);
    runCleanup([
      () => this.scene.remove(mesh),
      () => mesh.geometry.dispose(),
      () => mesh.material.dispose(),
      () => this.obstacleViews.delete(obstacle),
    ]);
  }

  selectObstacle(obstacle) {
    for (const [item, mesh] of this.obstacleViews) {
      mesh.material.color.setHex(item === obstacle ? 0x73968b : 0x91a184).convertSRGBToLinear();
      mesh.material.emissive.setHex(item === obstacle ? 0x142a21 : 0);
    }
  }

  selectBody(body, selected) {
    this.bodyViews.get(body).material.emissive.setHex(selected ? 0x352819 : 0);
  }

  setWireframe(enabled) {
    for (const [body, mesh] of this.bodyViews)
      if (body.type === 'soft') mesh.material.wireframe = enabled;
  }

  flashObstacle(obstacle, now) {
    const mesh = this.obstacleViews.get(obstacle);
    if (mesh) mesh.userData.flashUntil = now + 180;
  }

  resetFlashes() {
    for (const mesh of this.obstacleViews.values()) mesh.userData.flashUntil = 0;
  }

  render(now, selectedObstacle) {
    this.controls.update();
    for (const [obstacle, mesh] of this.obstacleViews) {
      const pulse = Math.max(0, ((mesh.userData.flashUntil || 0) - now) / 180);
      mesh.material.emissive.setHex(
        pulse > 0 ? 0xb65d24 : obstacle === selectedObstacle ? 0x142a21 : 0,
      );
      mesh.material.emissiveIntensity = pulse > 0 ? pulse : 1;
    }
    this.renderer.render(this.scene, this.camera);
  }

  resize() {
    const width = Math.max(1, this.viewport.clientWidth);
    const height = Math.max(1, this.viewport.clientHeight);
    this.renderer.setSize(width, height);
    this.camera.aspect = width / height;
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
