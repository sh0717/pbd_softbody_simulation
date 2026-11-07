# PBD 코드 리뷰 가이드

약 25~30분 동안 실행 순서, 수치 보존, 소유권과 실패 계약을 확인하는 순서입니다.
설치·전체 검사 실행 시간은 별도입니다. 상세 독해는 [code-reading.md](code-reading.md),
모듈 경계와 JS/Wasm 수명은 [architecture.md](architecture.md)를 함께 확인합니다.
파일의 줄 번호보다 아래의 함수명으로 탐색합니다.

명명·주석·서식은 [C++ 적용 규칙](cpp-coding-standard.md)을 따릅니다.
`SolveEdges/SolveVolumes`의 순회 뒤 `ProjectEdge/ProjectVolume`의 단일 투영을 읽고,
`Predict`에서 `PredictParticle`로 이어 가면 실행 순서와 원소 변경 범위를 구분할 수 있습니다.

## 리뷰 순서

| 시간 | 먼저 읽을 소스·함수 | 확인할 내용 |
| --- | --- | --- |
| 0~3분 | [PhysicsWorld.h](../native/src/simulation/PhysicsWorld.h), [PhysicsWorld.cpp](../native/src/simulation/PhysicsWorld.cpp)의 `Step` | 검증과 dt=0 처리가 통계·hit·시간 변경보다 앞에 있는가? `AdvanceSubstep`을 모든 몸체가 공유하는가? |
| 3~8분 | [TimeIntegrator.cpp](../native/src/simulation/TimeIntegrator.cpp)의 `Predict`·`UpdateVelocities`, [SoftBody.cpp](../native/src/simulation/SoftBody.cpp)의 `BeginSubstep`, [ConstraintSolver.cpp](../native/src/simulation/ConstraintSolver.cpp)의 `SolveEdges`·`SolveVolumes` | 승수는 서브스텝 시작에 초기화하고 제약 반복 동안 누적하는가? 부피 C=6(V-V0)·외적 기울기·36배 유연도와 고정 입자·붕괴 보정 제한이 일치하는가? |
| 8~13분 | [CollisionSolver.cpp](../native/src/collision/CollisionSolver.cpp)의 `SolveDynamic`·`SolveSoftPairs`·`SolveStatic` | 구 접촉과 연체 접촉의 처리 시점을 구분하는가? 연체 pair 전체의 접촉 수집을 끝낸 뒤 투영하는가? 정점·장애물 순서를 유지하는가? |
| 13~18분 | [SoftContactSolver.cpp](../native/src/collision/SoftContactSolver.cpp)의 `QueryContainingTetrahedra`·`FindNearestSurfacePoint`·`Project`, [TriangleGeometry.cpp](../native/src/collision/TriangleGeometry.cpp)의 `ClosestTriangle` | 포함 검사와 외곽 최근접점을 구분하는가? 첫 순회 일치와 최소 인덱스를 혼동하지 않는가? 동률 선택·barycentric 역질량 보정의 식과 누적 순서를 유지하는가? |
| 18~23분 | [SoftContactSolver.h](../native/src/collision/SoftContactSolver.h)의 `Prepare`, [OwnerRegistry.h](../native/bindings/OwnerRegistry.h), [ContactSession.h](../native/bindings/ContactSession.h) | 누가 몸체·캐시·접촉을 소유하는가? 신규 등록과 기존 entry refit의 실패 보장을 구분하는가? owner 토큰과 접촉 세대를 검사한 뒤 투영하는가? |
| 23~27분 | `Restitute`와 [BumperSystem.cpp](../native/src/collision/BumperSystem.cpp)의 `Apply`·`ApplyGroup`, 아래 회귀 검사 | 저장된 접촉 순서, 목표 법선 속도, 최초 접촉 그룹·정점 중복 제거, cooldown 갱신 시점을 유지하는가? 변경 범위를 검사 근거와 연결하는가? |

## 한 step의 실제 호출 흐름

[PhysicsWorld.cpp](../native/src/simulation/PhysicsWorld.cpp)에서 다음 순서를 확인합니다.

```text
FPhysicsWorld::Step(Dt)
  설정/Dt 검증 → 유효한 dt=0이면 변경 없이 false
  h = Dt / Substeps 검증 → 통계와 frame hit 초기화
  각 서브스텝 AdvanceSubstep(h)
    Collisions.BeginSubstep: 이전 접촉 목록 비우기
    모든 body Predict: 이전 위치 저장, BeginSubstep, 예측 위치·속도 갱신
    설정된 횟수만큼 SolveIteration(h)
      각 soft body: SolveEdges → SolveVolumes
      SolveDynamic
        CollectBodies → SolveBallContacts → CollectSoftPairs → SolveSoftPairs
        soft pair가 있으면 Prepare(참여 body)
          모든 pair Generate: A→B와 B→A 접촉 수집
          수집한 SoftContacts 순서대로 Project
          진단 누적과 참여 body bounds 갱신
      각 body SolveStatic: 이동 가능한 정점마다 경계 X/Y/Z → 입력 장애물 순서
    모든 body UpdateVelocities
    Collisions.Restitute → Bumpers.Apply → SimulationTime += h
```

`SolveBallContacts`의 입자 쌍은 검사 중 바로 투영·기록합니다. 반면 연체끼리의
`SolveSoftPairs`는 같은 준비 상태에서 모든 pair의 접촉을 수집한 다음 전체를 투영합니다.
두 경로를 모두 “동적 접촉 전체를 모은 뒤 투영한다”로 단순화하지 않습니다.
몸체 하나의 모든 서브스텝을 끝낸 다음 다음 몸체를 처리하는 순서도 아닙니다.

`Predict`에서 `FSoftBody::BeginSubstep`이 λ를 초기화합니다. `SolveEdges`·`SolveVolumes`는
현재 서브스텝의 λ를 누적합니다. `UpdateVelocities`와 `Restitute` 뒤에 범퍼가 실행되며,
범퍼는 해당 서브스텝 시작의 `SimulationTime`을 받은 후 월드 시간이 증가합니다.

## 소유권·실패·동률 확인 질문

- **몸체 포인터는 누가 살려 두는가?** `FPhysicsWorld`는 `unique_ptr<FBody>`를 소유합니다.
  솔버 캐시와 `FContact`는 몸체를 관찰하며 소유하지 않습니다. 현재 배치뿐 아니라 이전에
  캐시에 등록한 몸체의 수명도 유지해야 합니다. `FSoftContactSolver::Configure`는 전체 캐시 등록을 지웁니다.
  raw C++ 사용과 JS 어댑터의 owner 보존 정책을 같은 것으로 가정하지 않습니다.
- **모든 실패가 이전 배치를 그대로 보존하는가?** `Prepare`는 Null 몸체를 먼저 거부하고,
  신규 body의 topology/hash/BVH를 완성한 뒤 등록합니다. batch 통계는 전체 준비 성공 후
  교체합니다. 기존 entry의 refit 중 할당 실패는 basic guarantee이며 전체 rollback이 아닙니다.
  다시 질의하기 전에 필요한 몸체 전체를 재준비해야 합니다. `Step` 실행 중 할당 실패도
  초기 검증 실패와 다른 계약입니다.
- **접촉 범위와 접촉 본체를 언제까지 사용할 수 있는가?** `FContactSession`은 배열과 세대를
  소유합니다. 성공한 `Prepare`가 이전 배치를 무효화하며 `Project`는 세대·인덱스를 검사합니다.
  `GetContact`의 const 참조는 다음 `Generate`·`Prepare` 또는 파괴 시 무효화될 수 있습니다.
  후속 접촉 투영 오류가 앞서 끝난 투영까지 되돌리지는 않습니다.
- **`Query(..., bFirst=true)`는 무엇을 고르는가?** 최소 사면체 번호가 아니라 dense 셀 체인,
  sparse bucket, overflow 후보를 실제로 순회하다 처음 포함된 사면체입니다.
  `QueryContainingTetrahedra`의 조기 반환을 정렬된 최솟값 선택으로 바꾸지 않았는지 확인합니다.
- **같은 거리·같은 면이면 누가 먼저인가?** BVH 자식 거리 비교가 같으면 현재 코드는 오른쪽을
  먼저 꺼내며, 최근접 결과는 엄격한 `<`에서만 바꿉니다. 퇴화 삼각형은 AB→BC→CA 중
  처음 같은 거리를 유지합니다. 상자 내부의 면 선택은 `ClosestBoxFaceAxis`의 X→Y→Z입니다.
  성능을 이유로 순회를 바꿀 때도 이 결과를 따로 대조합니다.
- **대여 배열이 계속 유효한가?** C++ vector의 주소 안정성과 Wasm 메모리 성장 뒤 JS
  TypedArray의 유효성은 다른 조건입니다. owner 해제·배치 교체·메모리 성장 경로는
  [architecture.md](architecture.md)의 실제 어댑터 계약까지 확인합니다.

## 관련 검사와 실행 명령

| 소스·검사 | 연결할 리뷰 근거 |
| --- | --- |
| [test_refactoring.cpp](../tests/native/test_refactoring.cpp)의 `ZeroStepPreservesProgressedState`, `StaticContactsPreserveBoundaryObstacleOrderAndPinnedParticles`, `SettlePreservesUnmovedParticles` | dt=0 보존, 경계/장애물·고정 입자 순서, 이동한 입자의 속도만 초기화 |
| 같은 파일의 `PreparePublishesCompleteCacheAndBatchDiagnostics`, `ContactSessionOwnsBatchLifetimeAndPreservesFailedPreparation`, `OwnerRegistryRejectsStaleAndWrongTypeTokens` | 캐시 등록·진단 발행, 배치·세대·owner 종류 검증. 모든 내부 allocation failure를 직접 주입한 증거는 아님 |
| 같은 파일의 `ClosestTrianglePreservesDegenerateTieOrder`, `BumperScratchPreservesContactOrderAndReuse` | 퇴화 선분 동률, 최초 범퍼 접촉 순서·중복 정점 처리. BVH 방문 순서는 해당 구현도 별도로 대조 |
| [test_physics.cpp](../tests/native/test_physics.cpp), [physics.test.mjs](../tests/unit/physics.test.mjs), [soft-contacts.test.mjs](../tests/unit/soft-contacts.test.mjs) | 실제 수치 커널·공간 포함·접촉과 별도 기하 기대값 |
| [wasm-adapter.test.mjs](../tests/unit/wasm-adapter.test.mjs), [browser.mjs](../tests/browser/browser.mjs) | 만료 owner·배치, 실패 원자성과 실제 UI/부분 실패·반복 해제 |

프로젝트 루트에서 실행합니다. C++ 주석 변경도 원본 해시를 바꾸므로 Emscripten 4.0.23을
준비한 뒤 `build:all`로 Wasm과 빌드 기록을 갱신합니다. 생성물이나 승인 해시를 직접 고치지 않습니다.

```powershell
npm.cmd run build:all
npm.cmd run check
npm.cmd run test:native
npm.cmd run test:native:release
npm.cmd run test:browser
```

위 명령은 검증 절차이며 이 문서 자체가 새 실행 결과를 나타내지는 않습니다.
리뷰 기록에는 실제 변경 파일·보존할 계약·실행 명령과 결과·미실행 범위를 남깁니다.
과거 관찰점 수나 테스트 수를 현재 소스의 실행 결과로 승계하지 않습니다.
