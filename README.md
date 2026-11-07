# Soft / Lab — C++20 PBD · XPBD

[웹 실행](https://pbd-softbody-lab-20261108.true-orbit-7047.chatgpt.site) · [GitHub](https://github.com/sh0717/pbd_softbody_simulation) · [실행용 ZIP](https://pbd-softbody-lab-20261108.true-orbit-7047.chatgpt.site/pbd-softbody-simulation.zip)

테트라 메시의 연체와 구를 Box 장애물과 함께 실험하는 브라우저 시뮬레이션입니다.
C++20이 물리 상태·제약·접촉을 계산하고, WebAssembly를 통해 JavaScript·Three.js가 입력과 화면을 담당합니다.

## 바로 실행

저장소를 다운로드하고 압축을 푼 뒤 **`index.html` 또는 `pbd_model.html`을 Chrome·Edge로 엽니다.**
`dist/`를 포함한 폴더 구조를 유지합니다. 배포 번들에 Wasm을 내장하므로 `file://`에서도 실행하며
Node.js·Python·별도 서버와 실행 중 CDN 연결은 필요하지 않습니다. WebGL 지원 브라우저가 필요합니다.

- 실행·일시정지, 초기화, PBD/XPBD 전환과 시간 간격·서브스텝·제약 반복·재질을 조절합니다.
- 토끼·구·Box 장애물은 각각 최대 10개입니다. Box를 선택해 드래그하거나 위치·크기를 편집합니다.
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
| `npm.cmd run check:quality`       | 독립 리뷰 기록과 현재 공개 소스 지문 확인                               |
| `npm.cmd run verify:portfolio`    | Wasm·웹 빌드, check, native Debug/Release, 브라우저 검사                |

VS Code 설정과 디버깅은 [개발 환경](docs/vscode-development.md), 실행 결과와 미실행 범위는
[독립 리뷰](docs/portfolio-review.md)를 확인합니다. 위 명령 목록 자체는 검사 통과 기록이 아닙니다.

## 코드 읽기

```text
src/app.js → experiment-controller → simulation/physics.js
  → native/bindings/Bindings.cpp → simulation/PhysicsWorld.cpp
  → 적분 → 거리·체적 제약 → 접촉 투영 → 속도·반발·범퍼
```

| 문서                                                                               | 내용                         |
| ---------------------------------------------------------------------------------- | ---------------------------- |
| [코드 독해](docs/code-reading.md) · [리뷰 순서](docs/code-review-guide.md)         | 실행 흐름, 수식, 관련 검사   |
| [아키텍처](docs/architecture.md) · [C++ 구조](docs/cpp-architecture.md)            | 책임, 소유권, 대여 수명      |
| [XPBD의 람다](docs/xpbd-lambda.md)                                                 | 누적 승수와 제약 보정        |
| [C++ 표준](docs/cpp-coding-standard.md) · [공통 규약](docs/project-conventions.md) | 이름, 주석, 서식과 실패 계약 |
| [프로젝트 배치](docs/project-layout.md) · [native 배치](docs/native-layout.md)     | 원본과 빌드 경로             |
| [품질 기준](docs/quality-standard.md)                                              | 독립 판정과 소스·실행 지문   |
| [공개 파일](docs/github-upload.md) · [EXE 배포](docs/desktop.md)                   | 저장소 포함 범위와 배포 절차 |

`native/src/`는 수학·상태·해법·충돌을, `native/bindings/`는 Emscripten 경계를 담당합니다.
`src/`는 앱·시뮬레이션 연결·렌더링·입력을, `tests/`와 `tools/`는 검사·빌드·개발 명령을 담습니다.

## 구현 범위와 외부 자료

PBD/XPBD 거리·체적 제약, 구·Box 정적 충돌, 구 사이 충돌, 연체의 국소 점-표면 접촉,
접촉 진입에 반응하는 범퍼를 구현합니다. 연체 접촉은 공간 셀과 BVH로 후보를 줄입니다.
연속 충돌 검출, 연체 자체 충돌, edge-edge 접촉은 구현하지 않았습니다.

알고리즘의 논문 링크와 보존 자료 해시는 [출처 목록](papers/sources.json)에 있습니다.
`src/data/bunny-data.js`의 원본 메시 대응을 유지합니다. Three.js·OrbitControls·Emscripten 등
외부 구성요소의 권리를 따르며 라이선스 고지를 배포물에 포함합니다.

실행에 필요한 `dist/`와 `wasm/`, 회귀 fixture를 저장소에 포함합니다. SDK·설치 의존성·검사 산출물,
개인 설정과 과거 과제 PDF·영상·별도 교재·작업 보고서는 공개 대상에서 제외합니다.
