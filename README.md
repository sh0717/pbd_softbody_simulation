# Soft / Lab — C++20 PBD · XPBD

[웹 실행](https://pbd-softbody-lab-20261108.true-orbit-7047.chatgpt.site) · [GitHub](https://github.com/sh0717/pbd_softbody_simulation) · [실행용 ZIP](https://pbd-softbody-lab-20261108.true-orbit-7047.chatgpt.site/pbd-softbody-simulation.zip)

테트라 메시의 연체와 구를 Box 장애물과 함께 실험하는 브라우저 시뮬레이션입니다.
C++20이 물리 상태·제약·접촉을 계산하고, WebAssembly를 통해 JavaScript·Three.js가 입력과 화면을 담당합니다.

## 바로 실행

저장소를 다운로드하고 압축을 푼 뒤 **`index.html` 또는 `pbd_model.html`을 Chrome·Edge로 엽니다.**
`dist/`를 포함한 폴더 구조를 유지합니다. 배포 번들에 Wasm을 내장하므로 `file://`에서도 실행하며
Node.js·Python·별도 서버와 실행 중 CDN 연결은 필요하지 않습니다. WebGL 지원 브라우저가 필요합니다.

- 실행·일시정지, 초기화, PBD/XPBD 전환과 시간 간격·서브스텝·제약 반복·재질을 조절합니다.
- 토끼·공·Box 장애물은 각각 최대 10개입니다. Box를 선택해 드래그하거나 위치·크기를 편집합니다.
- 범퍼를 켜면 접촉 진입 시 추가 충격을 줍니다. 초기화는 물체를 복원하고 편집한 장애물 배치를 유지합니다.

## 개발

Node.js 22.20.0 기준입니다. 웹 빌드는 저장소에 포함된 Wasm을 사용합니다.

```powershell
npm.cmd ci --ignore-scripts
npm.cmd run build
npm.cmd start
```

<http://127.0.0.1:4173/>을 엽니다. 서버는 `Ctrl+C`로 종료합니다.
`build`는 Three.js·OrbitControls 0.139.2를 로컬 배포 폴더로 복사하고 실행 HTML과 번들을 생성합니다.
C++을 수정했다면 Emscripten 4.0.23과 Python을 준비해 `npm.cmd run build:all`로 Wasm도 재빌드합니다.
SDK는 `EMSDK` 또는 `.tools/emsdk-main`에서 찾습니다.

| 명령                              | 범위                                                                    |
| --------------------------------- | ----------------------------------------------------------------------- |
| `npm.cmd run check`               | C++·JS 서식, 구조, 소스/Wasm 해시와 Node 회귀. clang-format 22.1.3 필요 |
| `npm.cmd run test:native`         | CMake·MSVC Debug 빌드와 CTest                                           |
| `npm.cmd run test:native:release` | 네이티브 Release 빌드와 CTest                                           |
| `npm.cmd run test:browser`        | Python·설치된 Chrome을 사용하는 UI·수명 검사                            |
| `npm.cmd run test:distribution`   | 배포 HTML의 HTTP·직접 파일 실행 검사                                    |
| `npm.cmd run check:execution`     | 전체 검사 결과와 현재 소스·배포물 해시 확인                             |
| `npm.cmd run verify`              | Wasm·웹 빌드, check, native Debug/Release, 브라우저 검사                |

검사 결과와 오류 로그는 GitHub Actions와 로컬 `artifacts/`에서 확인합니다.
`check:execution`은 `verify`가 생성한 검사 결과의 소스·배포물 해시와 종료 코드를 확인합니다.
CI 브라우저 검사는 `PBD_TEST_SOFTWARE_WEBGL=1`로 CPU 렌더러를 사용합니다.

## 핵심 알고리즘

입자의 위치를 예측하고 **거리·체적·접촉 제약으로 위치를 보정한 뒤 속도를 복원**합니다.
아래 PNG는 현재 소스의 처리 순서를 나타내는 개념도입니다.

![PBD·XPBD 전체 처리 순서](docs/images/algorithms/01-pipeline.png)

### PBD·XPBD 제약 보정

제약 오차를 $`C`$, 입자 역질량을 $`w_i`$라 두면 XPBD의 위치 보정은 다음과 같습니다.
고정 입자는 $`w_i=0`$입니다. PBD는 유연도를 0으로 두고 보정량에 강성을 곱하며,
XPBD는 유연도 $`\alpha`$와 이번 서브스텝의 누적 승수 $`\lambda`$를 사용합니다.

```math
\begin{aligned}
h&=\Delta t/\mathrm{Substeps},\\[2pt]
\widetilde\alpha&=\alpha/h^2,\\[2pt]
D&=\sum_i w_i\lVert\nabla_i C\rVert^2,\\[2pt]
\Delta\lambda&=\frac{-C-\widetilde\alpha\lambda}{D+\widetilde\alpha},\\[2pt]
\Delta\mathbf x_i&=w_i\nabla_i C\,\Delta\lambda,\\[2pt]
\lambda&\leftarrow\lambda+\Delta\lambda.
\end{aligned}
```

승수는 서브스텝 시작에 초기화하고 제약 반복 동안 누적합니다.
위치는 누적값 전체가 아닌 이번 $`\Delta\lambda`$로 보정합니다.

### 거리·체적 제약

![같은 거리 오차를 PBD와 XPBD로 보정하는 예](docs/images/algorithms/03-pbd-xpbd.png)

거리 제약은 변의 기준 길이 $`L_0`$를, 체적 제약은 사면체의 기준 체적 $`V_0`$를 복원합니다.
체적의 부호를 보존하여 뒤집힌 형상을 구분합니다.

```math
\begin{aligned}
C_{edge}&=\lVert\mathbf x_B-\mathbf x_A\rVert-L_0,\\[2pt]
\mathbf e_k&=\mathbf x_k-\mathbf x_0\quad(k=1,2,3),\\[2pt]
V&=\mathbf e_1\cdot(\mathbf e_2\times\mathbf e_3)/6,\\[2pt]
C_{volume}&=6(V-V_0).
\end{aligned}
```

체적 제약을 6배로 계산하므로 XPBD 체적 유연도도 36배로 환산합니다.
[ConstraintSolver.cpp](native/src/simulation/ConstraintSolver.cpp)의 거리 보정 코드입니다.
아래 발췌는 읽기 쉽도록 줄바꿈했습니다.

```cpp
const double DeltaLambda =
    (-(EdgeLength - RestLength)
        - ScaledCompliance * InOutBody.GetEdgeLambdas()[EdgeIndex])
    / (InverseMassSum + ScaledCompliance) * ProjectionStiffness;

InOutBody.EdgeLambdas[EdgeIndex] += DeltaLambda;
AddParticleVector(InOutBody.Positions, VertexA, EdgeNormal * (-InverseMassA * DeltaLambda));
AddParticleVector(InOutBody.Positions, VertexB, EdgeNormal * (InverseMassB * DeltaLambda));
```

### 충돌 처리

![공간 셀·BVH 접촉 탐색과 양쪽 입자 보정](docs/images/algorithms/05-soft-contact.png)

- **연체-연체:** 경계구·AABB로 쌍을 줄이고, 공간 셀의 사면체 포함 검사로 침투 입자를 찾습니다.
  표면 BVH에서 최근접점을 구해 양쪽 몸체를 역질량과 삼각형 가중치에 따라 분리합니다.
- **구·Box·월드 경계:** 구는 중심 거리, Box는 최근접점·가장 가까운 면으로 겹침을 해소합니다.
- **속도:** 보정 위치와 이전 위치의 차이를 $`h`$로 나눈 뒤 반발과 범퍼를 적용합니다.

실행 순서는 [PhysicsWorld.cpp](native/src/simulation/PhysicsWorld.cpp), 충돌 구현은
[CollisionSolver.cpp](native/src/collision/CollisionSolver.cpp)와
[SoftContactSolver.cpp](native/src/collision/SoftContactSolver.cpp)를 확인합니다.
유도·수치 예제·병렬 처리·메모리 수명은 아래 상세 문서에 있습니다.

## 코드 읽기

```text
src/app.js → experiment-controller → simulation/physics.js
  → native/bindings/Bindings.cpp → simulation/PhysicsWorld.cpp
  → 적분 → 거리·체적 제약 → 접촉 투영 → 속도·반발·범퍼
```

| 문서                                                                               | 내용                         |
| ---------------------------------------------------------------------------------- | ---------------------------- |
| [코드 독해](docs/code-reading.md)                                                  | 실행 흐름, 수식, 관련 검사   |
| [아키텍처](docs/architecture.md) · [C++ 구조](docs/cpp-architecture.md)            | 책임, 소유권, 대여 수명      |
| [XPBD의 람다](docs/xpbd-lambda.md)                                                 | 누적 승수와 제약 보정        |
| [C++ 표준](docs/cpp-coding-standard.md) · [공통 규약](docs/project-conventions.md) | 이름, 주석, 서식과 실패 계약 |
| [프로젝트 배치](docs/project-layout.md) · [native 배치](docs/native-layout.md)     | 원본과 빌드 경로             |
| [EXE 배포](docs/desktop.md)                                                        | 저장소 포함 범위와 배포 절차 |

`native/src/`는 수학·상태·해법·충돌을, `native/bindings/`는 Emscripten 경계를 담당합니다.
`src/`는 앱·시뮬레이션 연결·렌더링·입력을, `tests/`와 `tools/`는 검사·빌드·개발 명령을 담습니다.

## 구현 범위와 외부 자료

PBD/XPBD 거리·체적 제약, Box 정적 장애물 충돌, 공 사이 충돌, 연체의 국소 점-표면 접촉,
접촉 진입에 반응하는 범퍼를 구현합니다. 연체 접촉은 공간 셀과 BVH로 후보를 줄입니다.
장애물은 C++·Wasm·JS 전체에서 축 정렬 Box만 지원하며 구형 장애물과 장애물 `radius` 입력은 거부합니다.
공 객체의 반지름·충돌은 별도 기능으로 유지합니다.
연속 충돌 검출, 연체 자체 충돌, edge-edge 접촉은 구현하지 않았습니다.

알고리즘의 논문 링크와 보존 자료 해시는 [출처 목록](papers/sources.json)에 있습니다.
`src/data/bunny-data.js`의 원본 메시 대응을 유지합니다. Three.js·OrbitControls·Emscripten 등
외부 구성요소의 권리를 따르며 라이선스 고지를 배포물에 포함합니다.

실행에 필요한 `dist/`와 `wasm/`, 회귀 fixture를 저장소에 포함합니다. SDK·설치 의존성·검사 산출물,
개인 설정과 과거 과제 PDF·영상·별도 교재·작업 보고서는 공개 대상에서 제외합니다.
