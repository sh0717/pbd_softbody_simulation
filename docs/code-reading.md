# 코드 독해 경로

현재 리뷰 대상은 프로젝트 루트의 `native/src/`와 `native/bindings/`입니다.
`artifacts/` 안의 소스는 당시 검증을 보존한 사본이며 현재 빌드에 사용하지 않습니다.

현재 구현의 진입·수치·수명 계약을 코드와 연결합니다. 알고리즘 기반과 외부 자료는
[README의 구현 범위](../README.md#구현-범위와-외부-자료)를 확인합니다.

## 렌더링 프레임과 물리 스텝이 진행되는 경로

1. [app.js](../src/app.js)의 `start()`가 Wasm을 초기화하고 world → scene → UI 순으로 생성합니다.
   [ExperimentController](../src/experiment-controller.mjs)는 선택·실행 상태와 사용자 명령을 소유합니다.
   UI에는 `actions`, picking에는 `selection`을 전달하며 전체 가변 앱 상태를 공유하지 않습니다.
   `frame(nowMs)`는 `advancePhysics()` → 화면 렌더 → 통계 갱신만 조율합니다.
2. `advancePhysics()`는 requestAnimationFrame의 ms를 초로 변환합니다. 경과 시간은 0.1초,
   catch-up은 프레임당 6 step으로 제한하고 남은 완전한 step 수를 지연 생략 횟수로 기록합니다.
   `stepPhysics()`의 CPU 계측에는 JS/Wasm 호출과 범퍼 표시 명령이 포함되며 GPU 완료 시간은 아닙니다.
3. [physics.js](../src/simulation/physics.js)의 `PhysicsWorld.step()`은 설정·장애물을 동기화하고
   `native.step()`을 호출합니다. 성공한 step 뒤 진단과 native body/obstacle 대응을 JS 결과로 변환합니다.
   마지막 성공 입력과 값·장애물 identity·순서를 비교하여 실제 변경 때만 native에 전달합니다.
4. [Bindings.cpp](../native/bindings/Bindings.cpp)의 `GetSettings()`·`ReadVector()`·`ReadPositiveInteger()`가
   외부 데이터의 유한성·차원·범위를 검증합니다. Emscripten 의존성은 이 파일에만 있습니다.
5. [PhysicsWorld.cpp](../native/src/simulation/PhysicsWorld.cpp)의 `Step()`은 dt와 h²의 유한성을 검증한 뒤
   전체 물체가 공유하는 `AdvanceSubstep()`을 반복합니다. `dt=0`은 진행 상태를 보존합니다.

```text
AdvanceSubstep(h)
  모든 body Predict / BeginSubstep(λ 초기화)
  반복 횟수만큼 SolveIteration(h)
    연체 edge → signed volume
    동적 접촉: 구체 관련 접촉 → 연체 쌍 전체 수집 → 연체 접촉 전체 투영
    body별 정적 접촉
  모든 body UpdateVelocities
  Restitute → 능동 bumper → simulationTime += h
```

객체 하나의 모든 substep을 마친 뒤 다른 객체를 계산하는 순서가 아닙니다.
물체 사이의 접촉이 같은 시간 단계를 보도록 `FPhysicsWorld`가 순서를 소유합니다.

## 위치와 속도 배열의 역할

`Positions`는 예측 위치와 제약으로 보정된 위치를 같은 배열에서 갱신합니다.
`PreviousPositions`는 서브스텝 시작 위치를 보존하며 속도 복원의 기준입니다.

| 배열 | 예측 직후 | 제약·접촉 처리 뒤 |
| --- | --- | --- |
| `Velocities` | 중력·감쇠 또는 드래그를 반영한 속도 | 위치 차이로 복원한 뒤 반발·범퍼로 갱신한 속도 |
| `PreSolveVelocities` | 위 적분 속도를 복사한 스냅샷 | 그대로 보존하여 접촉의 입사 법선 속도 계산에 사용 |

`FTimeIntegrator::Predict()`의 마지막에 `SavePreSolveVelocities()`로 저장합니다.
`UpdateVelocities()`와 반발 처리는 `Velocities`만 갱신합니다. Reset은 두 배열을 모두 0으로 초기화하고,
편집 후 Settle은 실제 이동한 입자의 두 속도만 0으로 만듭니다.
JavaScript의 `predictedVelocities` 이름은 기존 소비자 호환을 위해 유지하며 같은 스냅샷을 대여합니다.

## 먼저 읽을 수치 커널

[ConstraintSolver.cpp](../native/src/simulation/ConstraintSolver.cpp)의 `SolveVolumes()`는
`C = 6*(signedVolume - restVolume)`과 `/6`이 없는 외적 기울기를 사용합니다.
`restVolume`은 계속 물리 체적입니다. 누적 승수의 유도와 예제는 [XPBD의 누적 람다](xpbd-lambda.md)에 있습니다.

```text
alpha = compliance / h²                    // XPBD 거리 제약; PBD에서는 0
alpha = 36*(volumeCompliance / h²)         // C=6(V-V0)의 체적 제약
deltaLambda = (-C - alpha * lambda) / (Σ inverseMass * |gradient|² + alpha)
deltaPosition = gradient * inverseMass * deltaLambda
```

λ는 substep 시작에 초기화하고 그 substep의 반복 동안 누적합니다. 위치에는 이번 deltaLambda만 적용합니다.
체적 λ는 물리 체적 V-V0 기준 승수의 1/6입니다. 제약·기울기·유연도를 함께 환산해 기존 재질 의미를 유지합니다.
`MaximumWeightedGradientSquared`와 `GetTetLimits()`는 심하게 붕괴한 테트라의 한 번 보정을 rest diameter 이내로 제한합니다.
기울기가 0인 완전 붕괴는 건너뜁니다. 이 제한은 정확한 비선형 해를 보장하는 조건이 아닙니다.
해석해·부호·퇴화·고정 입자는 [physics.test.mjs](../tests/unit/physics.test.mjs)가 검사합니다.

## 국소 접촉을 읽는 경로

[SoftContactSolver.cpp](../native/src/collision/SoftContactSolver.cpp)는 접촉 생성을 조율하고,
[사면체 공간 인덱스](../native/src/collision/TetrahedronSpatialIndex.h)와
[표면 BVH](../native/src/collision/SurfaceBvh.h)가 각 탐색 자료구조를 소유합니다.
독립 위상·기하 모듈과 함께
다음 순서를 확인합니다.

| 단계 | 함수 | 유지할 계약 |
| --- | --- | --- |
| 토폴로지 | [SurfaceTopology.cpp](../native/src/collision/SurfaceTopology.cpp)의 `BuildSurfaceTopology` | 테트라의 한 번만 공유되는 면이 실제 외곽. supplied surface와 일치 검증 후 독립 배열 반환 |
| 현재 위치 인덱스 | `Prepare`, `PrepareBody`, `RefitBodySpatialCache` | 위치 변경 뒤 재준비. dense/sparse/overflow 경로 모두 후보 누락 방지 |
| 포함 검사 | `FTetrahedronSpatialIndex::QueryContainingTetrahedra`, 내부 `ContainsPoint` | AABB 후보 뒤 barycentric 포함 검사. `bFirst`는 최소 번호가 아닌 첫 순회 일치 |
| 외곽 최근접점 | `FSurfaceBvh::FindNearestPoint`, [TriangleGeometry.cpp](../native/src/collision/TriangleGeometry.cpp)의 `FindClosestPointOnTriangle` | AABB 거리 하한으로 가지치기. 동일 거리에서 기존 방문·엄격 비교 순서 유지 |
| 수집 | `CollectDirectionalContacts`, `Generate` | A→B·B→A를 모두 수집하고 위치를 바꾸지 않음 |
| 투영 | `Project` | 한 점과 상대 표면 삼각형의 세 정점에 inverse-mass·barycentric 가중 보정 |

`PrepareBody()`는 처음 보는 topology를 로컬 candidate로 구축하고 셀 인덱스·BVH 갱신까지
마친 뒤 캐시에 등록합니다. 기존 topology는 같은 `RefitBodySpatialCache()`로 위치만 갱신합니다.
`Prepare()`는 Null 입력을 먼저 검사하고 요청 body 전체 준비 뒤 batch 통계를 발행합니다.
같은 entry의 할당 실패에서 전체 rollback은 보장하지 않습니다.

셀 인덱스는 안전한 범위에서 조밀 그리드를, 그 밖에서는 희소 `std::map`을 사용합니다.
실제 해시 테이블로 바꾸는 알고리즘 변경은 이번 클래스 분리에 포함하지 않습니다.
BVH는 표면 인덱스·노드·순회 스택을 소유하고 위치 배열은 질의·갱신 호출에서 빌립니다.

`FindClosestPointOnTriangle()`은 퇴화 여부 판단·가중치 선택·결과 재구성을 연결합니다.
`TriangleRegionWeights()`는 A/B/C 정점·AB/AC/BC 엣지·면의 7개 Voronoi 영역을 평탄한 분기로 판정합니다.
`DegenerateTriangleWeights()`는 AB → BC → CA 선분을 검사하며 같은 거리에서는 첫 선분을 유지합니다.
`DistanceSquared`는 장면 길이의 제곱, 가중치는 무차원입니다.
퇴화 기준의 `|cross(edge, edge)|²`와 `max(|edge|²)²`는 둘 다 길이의 4제곱입니다.

[test_refactoring.cpp](../tests/native/test_refactoring.cpp)의 세 기하 검사는 7개 영역·경계,
일직선·중복 정점·완전 붕괴의 tie 순서, 크기·정점 winding 불변식을 직접 확인합니다.
[soft-contacts.test.mjs](../tests/unit/soft-contacts.test.mjs)는 별도의 oriented-face oracle로
공간 후보의 포함 결과와 실제 토끼 데이터를 검사합니다.

## 정적 충돌과 반발의 책임

[CollisionSolver.cpp](../native/src/collision/CollisionSolver.cpp)의 `SolveStatic()`은
이동 가능한 정점을 순회하고 `SolveBoundaryContact()` 이후 입력 장애물을 순서대로 처리합니다.
구 투영은 `ProjectSphereObstacle()`, 상자의 외부 최근점·내부 면 투영은
`ProjectBoxObstacle()`이 담당합니다. 두 private 함수는 위치를 투영하고 접촉 법선을 반환하며,
접촉 배열 기록은 호출자가 결정합니다. `Settle()`의 기록 없는 투영도 같은 경로를 사용합니다.
경계 X/Y/Z 순서, 구 중심 퇴화의 +X 법선, 상자 내부에서 동률이면 X→Y→Z를 고르는 규칙을 유지합니다.

`Restitute()`는 저장된 접촉 순서를 유지하며 `RestituteSoftContact()` 또는
`RestituteParticleContact()`로 전달합니다. 연체의 barycentric 질량 분모와 구/정적 장애물의
역질량 합 계산은 각 함수 안에 두고, 목표 법선 속도는 호출자가 기존 식으로 한 번 계산합니다.
함수 분리는 알고리즘 복잡도나 물리식을 변경하지 않습니다.

## 저장소와 대여 수명

- [Body.h](../native/src/simulation/Body.h): `std::vector`를 단독 소유합니다. 외부 조회는 const 참조이며
  입자 편집은 인덱스·유한성·역질량 검증 후 지정 원소만 수정합니다. 생성 후 버퍼 크기를 바꾸지 않습니다.
- [PhysicsWorld.h](../native/src/simulation/PhysicsWorld.h): `std::unique_ptr<FBody>`가 body를 소유합니다.
  getter의 body 관찰 포인터는 world보다 오래 사용할 수 없습니다.
  설정 조회는 const 참조이며 값 복사본을 수정한 뒤 `Configure()`로 검증·적용합니다.
- [OwnerRegistry.h](../native/bindings/OwnerRegistry.h): world/solver를 공통 토큰 공간에서
  소유합니다. 주소와 독립된 토큰을 재사용하지 않고 조회·삭제 때 owner 종류도 확인합니다.
- [ContactSession.h](../native/bindings/ContactSession.h): 독립 solver의 접촉 배열·세대를
  비공개로 소유합니다. 성공한 `Prepare`만 배치를 무효화하고 `Project`가 세대·인덱스를
  검사합니다. `Generate`가 반환한 범위의 const 접촉 조회는 다음 Generate/Prepare까지
  빌리는 값이며, 몸체 owner 유지·교체는 기존 JS 어댑터 책임입니다.
- Wasm 메모리 증가 뒤 JS TypedArray는 무효화될 수 있으므로 매 호출 후 getter에서 재취득합니다.
  네이티브 vector 주소 안정성과 JS ArrayBuffer의 수명은 서로 다른 조건입니다.
  호환 TypedArray는 writable이며 C++ const만으로 읽기 전용이 되지 않습니다. topology/rest 배열은
  생성 후 불변 계약을 유지하고 직접 위치 편집 시 bounds와 공간 캐시를 다시 갱신합니다.
- `SoftContactSolver.prepare()`는 현재 body owner 집합만 유지합니다. body 객체 identity가 바뀌면
  새 native solver를 준비한 뒤 성공 시 교체하여 주소 재사용에 의한 이전 토폴로지 캐시 승계를 막습니다.
  같은 owner 집합은 기존 캐시를 refit합니다.
- contact에는 JS 준비 generation과 native batch generation이 있습니다. 성공한 재준비 뒤 이전 contact를
  투영하면 거부합니다. 새 owner 준비 실패는 이전 batch를 보존하며, 같은 owner refit의 내부 할당 실패는
  native `Prepare`의 basic exception guarantee를 따릅니다.
- raw C++ `FSoftContactSolver`는 body 수명을 추적하는 owner가 아닙니다. 준비한 몸체를 파괴한 뒤
  그 캐시·접촉을 재사용하지 않습니다. JS adapter의 owner 검증을 우회한 raw Wasm 호출도 같은 전제가 있습니다.

이 계약은 [wasm-adapter.test.mjs](../tests/unit/wasm-adapter.test.mjs)의 만료·owner 교체·실패 원자성·
stale contact·서로 다른 토폴로지의 연속 world 검사와 연결됩니다.

## 실패와 화면 자원 정리

app은 초기화·프레임·WebGL context loss에 같은 `fail()` 경로를 사용합니다.
RAF 취소 → 이벤트 중단 → picking/UI → scene GPU 자원 → world 순으로 정리합니다.
`PhysicsScene`·`ControlPanel`·`PickingController`는 부분 생성 실패와 반복 `dispose()`를 처리합니다.
app과 scene은 개별 해제가 실패해도 나머지 정리를 계속 시도하고 최초 실패 원인을 보존합니다.
`ExperimentController`는 명령 callback을 명명된 메서드에 묶습니다. 선택 전환은 이전 drag를
해제하고, 설정/장애물 편집은 world 검증이 성공한 다음 표시를 갱신합니다. dt 변경 실패는
시간 누적기를 지우지 않으며 성공·run 전환·restart만 명시된 callback으로 초기화합니다.
[experiment-controller.test.mjs](../tests/unit/experiment-controller.test.mjs)는 실제 Wasm world와
화면 대역으로 선택·편집 실패·시간 변경·restart·종료 뒤 보존 callback을 검사합니다.
[browser.mjs](../tests/browser/browser.mjs)는 실제 UI 조작, 반복 해제, 물리 step 오류 주입 뒤
canvas·native owner·리스너 정리를 검사합니다.

검사를 통과한 입력과 장면의 동작을 증명하며 모든 GPU 드라이버·내부 할당 실패·고속 충돌 경로까지
증명하지는 않습니다. 실행한 명령과 범위는 [독립 리뷰](portfolio-review.md)를 확인합니다.

검토 기준과 소스·실행 지문의 관계는 [품질 기준](quality-standard.md)을 확인합니다.
