# 공통 C++ 파일 구조

Boids·PBD·SPH는 `native/` 내부에서 같은 폴더 분류를 사용합니다.
`.h`와 `.cpp`는 해당 모듈 폴더에 함께 둡니다.

```text
native/
  CMakeLists.txt
  sources.txt
  bindings/
    Bindings.cpp             C ABI 또는 Embind 진입점
    <호스트 어댑터 .h/.cpp>   프로젝트별 변환·수명 조율
    include/boids/boids.h     Boids 공개 C API만 해당
  src/
    core/                    수학·설정·상수·수치 커널
    simulation/              상태·객체·시간 진행·해법·이웃 탐색
    collision/               충돌 형상·기하·접촉·충돌 응답
```

| 분류 | Boids | PBD | SPH |
| --- | --- | --- | --- |
| `core/` | Vector3D, SimulationConfig, WorldBounds | MathUtils, Settings | Vector2, Config, Kernels |
| `simulation/` | BirdFlock, Goal, Predator, SimulationState, NeighborSearch, SpatialGrid, KdTree | Body, Ball, SoftBody, PhysicsWorld, ConstraintSolver, TimeIntegrator | Simulation, Neighbors, Fields, Integration, Boundary |
| `collision/` | Obstacle, SphereObstacle, BoxObstacle, ObstacleSet | CollisionSolver, SoftContactSolver, TetrahedronPointQuery, TetrahedronGeometry, TetrahedronCellGrid, SurfaceBvh, SurfaceTopology, TriangleGeometry, BumperSystem | Collision |
| `bindings/Bindings.cpp` | 공개 C ABI 함수 | Embind 등록 | Embind 등록 |

기존 `bodies/`, `world/`, `solvers/`, `spatial/`의 계산 코드는 `simulation/`으로 이동했습니다.
함수·클래스·수치식·업데이트 순서·ABI는 유지하며, include 경로와 빌드 등록을 갱신합니다.
`simulation/`에는 물리·군집 실행에 필요한 상태와 계산을 두고, 공용 수학과 충돌 계산은 별도 폴더로 구분합니다.

Boids의 공개 헤더는 `bindings/include/boids/boids.h`에 둡니다.
CMake가 이 공개 include root를 전달하므로 기존 `#include <boids/boids.h>` 문은 유지됩니다.
PBD/SPH의 Embind 경계에는 별도 C ABI 헤더가 필요하지 않으므로 형식만 맞추기 위한 헤더를 만들지 않습니다.
Boids의 독립 실행용 main은 `tests/native/DebugRunner.cpp`로 옮겼고,
`boids_debug` 실행 파일과 디버거 출력 경로는 유지했습니다.

`native/sources.txt`는 CMake와 Emscripten이 함께 사용하는 구현 목록입니다.
파일 이동 후에도 기존 컴파일 단위 순서를 유지합니다. 도메인 코드는 비공개 host 어댑터를 참조하지 않으며,
기존 Boids 공개 ABI의 공용 상수 의존은 명시적으로 유지합니다.

`npm run check`가 실행하는 `tools/check-native-layout.mjs`는 다음을 확인합니다.

- `native/` 하위 디렉터리는 `bindings/`, `src/`입니다.
- `native/src/`의 모듈은 `core/`, `simulation/`, `collision/`입니다.
- 각 모듈에 `.h/.cpp`를 배치하고, API 진입점은 `bindings/Bindings.cpp`를 사용합니다.
- 모든 C++ 구현 파일이 소스 목록에 중복 없이 등록돼 있습니다.

Wasm 해시·빌드 기록은 실제 재컴파일로 갱신합니다. 기존 독립 리뷰 승인이나 점수를 다시 봉인하지 않습니다.
