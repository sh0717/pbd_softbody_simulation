const PRESET_SUMMARIES = {
  drop: '두 연체를 높이 올려 낙하와 바닥 충돌로 인한 변형을 관찰합니다.',
  collision: '중력을 끄고 두 연체를 겹쳐 배치해 연체 접촉의 분리를 관찰합니다.',
  bumper: '활성 범퍼 위에 구체를 놓고 낙하 후 반발을 관찰합니다.',
};

// DOM controls read state and invoke app commands; Three.js resources belong to the scene.
export class ControlPanel {
  constructor({ world, actions, getState, obstacleLimits }) {
    this.world = world;
    this.actions = actions;
    this.getState = getState;
    this.obstacleLimits = obstacleLimits;
    this.impulseShortcuts = new Map();
    this.$ = (id) => document.getElementById(id);
    this.events = new AbortController();
    this.on = (id, type, callback) =>
      this.$(id).addEventListener(type, callback, { signal: this.events.signal });

    try {
      this.on('run-button', 'click', () => this.actions.setRunning(!this.getState().running));
      this.on('step-button', 'click', this.actions.advanceOneStep);
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
      this.on('scene-preset', 'change', () => this.updatePresetSummary());
      this.on('preset-apply', 'click', () => this.applyPreset());
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

      for (const button of this.$('impulseControls').querySelectorAll(
        'button[data-impulse-direction]',
      )) {
        const direction = button.dataset.impulseDirection;
        const shortcut = button.getAttribute('aria-keyshortcuts').toLowerCase();

        this.impulseShortcuts.set(shortcut, direction);
        button.addEventListener('click', () => this.actions.applyImpulse(direction), {
          signal: this.events.signal,
        });
      }

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

      this.focusButton = this.$('view-expand');
      this.focusStage = this.focusButton.closest('.stage');
      this.focusButton.hidden = !document.fullscreenEnabled || !this.focusStage?.requestFullscreen;
      this.on('view-expand', 'click', () => this.toggleFullscreen());
      document.addEventListener('fullscreenchange', () => this.updateFullscreen(), {
        signal: this.events.signal,
      });
      window.addEventListener('keydown', (event) => this.handleShortcut(event), {
        signal: this.events.signal,
      });

      this.updateFullscreen();
      this.updatePresetSummary();
    } catch (error) {
      this.dispose();

      throw error;
    }
  }

  handleShortcut(event) {
    if (
      event.defaultPrevented ||
      event.repeat ||
      event.isComposing ||
      event.ctrlKey ||
      event.metaKey ||
      event.altKey
    )
      return;

    if (
      event.target?.closest?.(
        'input, select, textarea, [contenteditable]:not([contenteditable="false"])',
      )
    )
      return;

    const key = event.key.toLowerCase();
    const physicalKey = event.code?.startsWith('Key') ? event.code.slice(3).toLowerCase() : '';
    // 영문 배열에서는 입력 문자를 따르고, 한글 입력 등에서는 물리 키 위치를 사용합니다.
    const direction = this.impulseShortcuts.get(/^[a-z]$/.test(key) ? key : physicalKey);

    if (direction) {
      event.preventDefault();
      this.actions.applyImpulse(direction);
      return;
    }

    if (key === ' ' && event.target?.closest?.('button, a, summary, [role="button"]')) return;

    switch (key) {
      case ' ':
        this.actions.setRunning(!this.getState().running);
        break;
      case 'n':
        this.actions.advanceOneStep();
        break;
      case 'r':
        this.actions.restart();
        break;
      case 'c':
        this.actions.restoreCamera();
        break;
      case 'f':
        if (this.focusButton.hidden) return;

        this.toggleFullscreen();
        break;
      default:
        return;
    }

    event.preventDefault();
  }

  async toggleFullscreen() {
    if (this.focusButton.hidden) return;

    try {
      if (document.fullscreenElement === this.focusStage) await document.exitFullscreen();
      else await this.focusStage.requestFullscreen();
    } catch {
      this.focusButton.title = '이 브라우저에서 전체 화면을 열 수 없습니다.';
    }
  }

  updateFullscreen() {
    const expanded = document.fullscreenElement === this.focusStage;

    const label = expanded ? '전체 화면 종료' : '전체 화면';

    this.focusButton.setAttribute('aria-label', label);
    this.focusButton.title = `${label} (F)`;
    this.focusButton.setAttribute('aria-pressed', String(expanded));
  }

  updatePresetSummary(applied) {
    const preset = this.$('scene-preset').value;
    const available = Object.hasOwn(PRESET_SUMMARIES, preset);
    this.$('preset-apply').disabled = !available;

    let summary = available
      ? PRESET_SUMMARIES[preset]
      : '박스 배치와 솔버 설정을 유지하고 물체의 배치와 시간을 초기화합니다.';

    if (applied) summary = '배치 완료. 실행 또는 한 스텝으로 관찰하세요.';
    else if (applied === false)
      summary =
        preset === 'bumper'
          ? '낙하 공간이 있는 활성 범퍼를 추가한 뒤 다시 적용해 주세요.'
          : '실험에 필요한 연체와 구체를 추가해 주세요.';

    this.$('preset-summary').textContent = summary;
  }

  applyPreset() {
    const preset = this.$('scene-preset').value;

    if (Object.hasOwn(PRESET_SUMMARIES, preset))
      this.updatePresetSummary(this.actions.loadPreset(preset));
  }

  updateGravity() {
    this.$('gravity').checked = this.world.gravity[1] !== 0;
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
    this.$('mode-badge').textContent = this.world.mode.toUpperCase();
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
    const { running, fps, physicsMs, completedSteps, droppedSteps, bumperHitCount } =
      this.getState();
    const state = running ? (this.world.dt === 0 ? 'held' : 'running') : 'paused';
    const status = this.$('simulation-status');
    const labels = { paused: '일시정지', held: '시간 정지', running: '실행 중' };

    if (status.dataset.state !== state) {
      status.dataset.state = state;
      status.textContent = labels[state];
    }

    this.$('step-button').disabled = running || this.world.dt === 0;
    this.$('fps-value').textContent = fps ? Math.round(fps) : '—';
    this.$('physics-value').textContent = completedSteps ? physicsMs.toFixed(2) : '—';
    this.$('telemetry-time').textContent = this.world.simulationTime.toFixed(3);
    this.$('telemetry-frame').textContent = completedSteps.toLocaleString('ko-KR');
    this.$('telemetry-workers').textContent = this.world.constraintWorkerCount;

    const stats = this.world.softContactStats;
    const contacts = stats.projections ?? stats.contacts ?? 0;

    this.$('telemetry-contacts').textContent = contacts.toLocaleString('ko-KR');
    this.$('contact-status').textContent =
      `후보 검사 ${(stats.candidateTests ?? 0).toLocaleString('ko-KR')} · 범퍼 ${bumperHitCount.toLocaleString('ko-KR')}회` +
      (droppedSteps ? ` · 스텝 ${droppedSteps.toLocaleString('ko-KR')}회 생략` : '');
  }

  updateRunning() {
    const { running } = this.getState();

    this.$('run-label').textContent = running ? '일시정지' : '실행';
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
