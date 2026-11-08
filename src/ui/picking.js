// Pointer controller borrows views and invokes app commands without owning selection.
export class PickingController {
  constructor({ THREE, view, world, selection, onObstacleMoved, onBodyMoved, isRunning }) {
    this.THREE = THREE;
    this.world = world;
    this.selection = selection;
    this.onObstacleMoved = onObstacleMoved;
    this.onBodyMoved = onBodyMoved;
    this.isRunning = isRunning;
    this.renderer = view.renderer;
    this.camera = view.camera;
    this.controls = view.controls;
    this.bodyViews = view.bodyViews;
    this.obstacleViews = view.obstacleViews;
    this.pointer = new this.THREE.Vector2();
    this.raycaster = new this.THREE.Raycaster();
    this.dragPlane = new this.THREE.Plane();
    this.dragPoint = new this.THREE.Vector3();
    this.dragOffset = new this.THREE.Vector3();
    this.events = new AbortController();
    this.dragging = false;

    try {
      this.renderer.domElement.addEventListener(
        'pointerdown',
        (event) => {
          if (event.button !== 0) return;

          this.pointerCoordinates(event);

          const hit = this.raycaster.intersectObjects([
            ...this.bodyViews.values(),
            ...this.obstacleViews.values(),
          ])[0];

          if (!hit) {
            this.selection.selectObstacle(null);

            return;
          }

          if (hit.object.userData.obstacle)
            this.selection.selectObstacle(hit.object.userData.obstacle);
          else this.selection.selectBody(hit.object.userData.body);

          this.dragPlane.setFromNormalAndCoplanarPoint(
            this.camera.getWorldDirection(new this.THREE.Vector3()),
            hit.point,
          );

          const center = this.selection.obstacle
            ? this.selection.obstacle.position
            : this.selection.body.center;

          this.dragOffset.fromArray(center).sub(hit.point);
          this.dragging = true;
          this.controls.enabled = false;
          this.renderer.domElement.setPointerCapture(event.pointerId);
          event.stopImmediatePropagation();
        },
        { capture: true, signal: this.events.signal },
      );
      this.renderer.domElement.addEventListener(
        'pointermove',
        (event) => {
          if (!this.dragging) return;

          this.pointerCoordinates(event);

          if (!this.raycaster.ray.intersectPlane(this.dragPlane, this.dragPoint)) return;

          this.dragPoint.add(this.dragOffset);

          if (this.selection.obstacle)
            this.onObstacleMoved(this.selection.obstacle, this.dragPoint.toArray());
          else if (this.selection.body) {
            this.selection.setDragTarget(this.dragPoint.toArray());

            if (!this.isRunning() || this.world.dt === 0)
              this.onBodyMoved(this.selection.body, this.dragPoint.toArray());
          }
        },
        { signal: this.events.signal },
      );

      for (const event of ['pointerup', 'pointercancel', 'lostpointercapture']) {
        this.renderer.domElement.addEventListener(
          event,
          (eventObject) => this.releasePointer(eventObject),
          { signal: this.events.signal },
        );
      }

      window.addEventListener('blur', () => this.endDrag(), { signal: this.events.signal });
    } catch (error) {
      this.events.abort();

      throw error;
    }
  }

  pointerCoordinates(event) {
    const rect = this.renderer.domElement.getBoundingClientRect();

    this.pointer.set(
      ((event.clientX - rect.left) / rect.width) * 2 - 1,
      (-(event.clientY - rect.top) / rect.height) * 2 + 1,
    );
    this.raycaster.setFromCamera(this.pointer, this.camera);
  }

  endDrag() {
    this.dragging = false;
    this.controls.enabled = true;
    this.selection.setDragTarget(null);
  }

  releasePointer(event) {
    this.endDrag();

    if (this.renderer.domElement.hasPointerCapture(event.pointerId))
      this.renderer.domElement.releasePointerCapture(event.pointerId);
  }

  dispose() {
    if (this.disposed) return;

    this.disposed = true;
    this.endDrag();
    this.events.abort();
  }
}
