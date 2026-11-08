// DOM controls read state and invoke app commands; Three.js resources belong to the scene.
export class ControlPanel {
  constructor({ world, actions, getState, obstacleLimits }) {
    this.world = world;
    this.actions = actions;
    this.getState = getState;
    this.obstacleLimits = obstacleLimits;
    this.$ = (id) => document.getElementById(id);
    this.events = new AbortController();
    this.on = (id, type, callback) =>
      this.$(id).addEventListener(type, callback, { signal: this.events.signal });

    try {
      this.on('run-button', 'click', () => this.actions.setRunning(!this.getState().running));
      this.on('restart-button', 'click', this.actions.restart);
      this.on('camera-reset', 'click', this.actions.restoreCamera);
      this.on('add-soft', 'click', () => this.actions.addSoft());
      this.on('add-ball', 'click', () => this.actions.addBall());
      this.on('mode-pbd', 'click', () => this.actions.setMode('pbd'));
      this.on('mode-xpbd', 'click', () => this.actions.setMode('xpbd'));
      this.on('softness', 'input', () => this.updateMaterial());
      this.on('Slider01', 'input', (event) => {
        const milliseconds = Number(event.target.value);

        this.actions.setTimeStep(milliseconds === 0 ? 0 : Math.max(0.001, milliseconds * 0.001));
        event.target.value = this.world.dt * 1000;
        this.$('SliderValue01').textContent = `${this.world.dt.toFixed(4)} s`;
        this.updateStatus();
      });
      this.on('Slider02', 'input', (event) => {
        this.actions.setSubsteps(Number(event.target.value));
        this.$('SliderValue02').textContent = this.world.substeps;
      });
      this.on('iterations', 'input', (event) => {
        this.actions.setIterations(Number(event.target.value));
        this.$('iterations-value').textContent = this.world.solverIterations;
      });
      this.on('gravity', 'change', (event) => this.actions.setGravity(event.target.checked));
      this.on('wireframe', 'change', (event) => this.actions.setWireframe(event.target.checked));
      this.on('add-box', 'click', () => this.actions.addObstacle('box'));
      this.on('obstacle-list', 'change', (event) =>
        this.actions.selectObstacle(
          this.world.obstacles.find(
            (obstacle) => String(obstacle.identifier) === event.target.value,
          ) || null,
        ),
      );
      this.on('obstacle-bumper', 'change', (event) => this.actions.setBumper(event.target.checked));
      this.on('bumper-strength', 'input', (event) =>
        this.actions.setBumperStrength(Math.max(0, Math.min(12, Number(event.target.value) || 0))),
      );
      this.on('remove-obstacle', 'click', this.actions.removeObstacle);

      for (const name of ['position-x', 'position-y', 'position-z', 'size-x', 'size-y', 'size-z']) {
        for (const suffix of ['', '-number'])
          this.on(name + suffix, 'input', (event) => {
            if (event.target.value.trim() === '') return;

            let value = Number(event.target.value);

            if (!Number.isFinite(value)) return;

            value = Math.max(Number(event.target.min), Math.min(Number(event.target.max), value));
            this.actions.editObstacle(name, value);
          });
      }
    } catch (error) {
      this.dispose();

      throw error;
    }
  }

  updateCounts() {
    this.$('soft-count').textContent = this.world.softBodies.length;
    this.$('ball-count').textContent = this.world.balls.length;
    this.$('obstacle-count').textContent = this.world.obstacles.length;

    this.$('add-soft').disabled = this.world.softBodies.length >= 10;
    this.$('add-ball').disabled = this.world.balls.length >= 10;
    this.$('add-box').disabled = this.world.obstacles.length >= 10;
  }

  syncObstacle() {
    const obstacle = this.getState().selectedObstacle;

    this.$('obstacle-properties').disabled = !obstacle;
    this.$('obstacle-properties').hidden = !obstacle;
    this.$('obstacle-list').value = obstacle ? String(obstacle.identifier) : '';

    if (!obstacle) return;

    this.$('obstacle-bumper').checked = obstacle.bumper;
    this.$('bumper-strength').value = obstacle.bumperStrength;
    this.$('bumper-strength').disabled = !obstacle.bumper;
    this.$('bumper-strength-value').textContent = `${obstacle.bumperStrength.toFixed(1)} m/s`;

    const values = {};
    const limits = this.obstacleLimits(obstacle);

    'xyz'.split('').forEach((axis, index) => {
      values[`position-${axis}`] = obstacle.position[index];
      values[`size-${axis}`] = obstacle.size[index];

      for (const suffix of ['', '-number']) {
        this.$(`position-${axis}${suffix}`).min = -limits[index];
        this.$(`position-${axis}${suffix}`).max = limits[index];
      }
    });

    for (const [name, value] of Object.entries(values)) {
      for (const suffix of ['', '-number']) this.$(name + suffix).value = Number(value.toFixed(3));
    }
  }

  refreshObstacles() {
    this.$('obstacle-list').replaceChildren(
      new Option(this.world.obstacles.length ? '장애물을 선택하세요' : '장애물 없음', ''),
    );

    for (const obstacle of this.world.obstacles) {
      this.$('obstacle-list').add(new Option(`Box ${obstacle.identifier}`, obstacle.identifier));
    }

    this.syncObstacle();
    this.updateCounts();
  }

  updateMode() {
    this.$('mode-pbd').setAttribute('aria-pressed', String(this.world.mode === 'pbd'));
    this.$('mode-xpbd').setAttribute('aria-pressed', String(this.world.mode === 'xpbd'));
    this.$('mode-badge').textContent = `${this.world.mode.toUpperCase()} SOLVER`;
    this.$('mode-description').textContent =
      this.world.mode === 'xpbd'
        ? 'Compliance로 재질의 유연함을 조절합니다.'
        : '보정 강성으로 조절합니다. 시간 간격과 반복 수의 영향을 받습니다.';
  }

  updateMaterial() {
    const softness = Number(this.$('softness').value) / 100;

    this.actions.setMaterial(softness);
    this.$('softness-value').textContent =
      softness < 0.3 ? '단단함' : softness < 0.65 ? '균형' : '부드러움';
  }

  updateStatus() {
    const { running, fps, physicsMs, droppedSteps, bumperHitCount } = this.getState();

    this.$('fps-value').textContent = fps ? Math.round(fps) : '—';
    this.$('physics-value').textContent = running && this.world.dt > 0 ? physicsMs.toFixed(2) : '—';
    this.$('simulation-status').textContent =
      (running
        ? this.world.dt === 0
          ? '시간 간격 0 · 물리 정지'
          : '실행 중 · 실시간 1×'
        : '일시정지 · 편집 가능') + (droppedSteps ? ` · 부하로 ${droppedSteps}회 지연 생략` : '');
    this.$('mode-badge').textContent =
      this.world.mode.toUpperCase() +
      (fps ? ` · ${Math.round(fps)} FPS` : ' SOLVER') +
      (running && this.world.dt > 0 ? ` · ${physicsMs.toFixed(2)} ms` : '');

    const stats = this.world.softContactStats;

    this.$('contact-status').textContent =
      `상호 접촉 ${stats.projections || stats.contacts || 0} · 후보 검사 ${stats.candidateTests || 0} · 범퍼 ${bumperHitCount}회`;
  }

  updateRunning() {
    const { running } = this.getState();

    this.$('run-button').textContent = running ? 'Ⅱ 일시정지' : '▶ 실행';
    this.$('run-button').setAttribute('aria-pressed', String(running));
    this.updateStatus();
  }

  isWireframe() {
    return this.$('wireframe').checked;
  }

  dispose() {
    if (this.disposed) return;

    this.disposed = true;
    this.events.abort();
  }
}
