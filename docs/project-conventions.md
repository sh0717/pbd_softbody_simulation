# 시뮬레이션 프로젝트 공통 설계 규약

Boids·PBD·SPH에 같은 내용을 적용합니다. 각 프로젝트는 이웃 프로젝트의 파일이나
`node_modules` 없이 독립적으로 설치·빌드·검증할 수 있어야 합니다.

## 1. 책임과 의존 방향

```text
HTML → app → simulation → Wasm adapter → native bindings → native domain
         ├→ rendering: 상태 조회 → Three.js 자원 갱신
         └→ UI: 사용자 입력 → app 명령 → simulation 검증
```

| 모듈 | 소유하는 것 | 공개 계약 |
| --- | --- | --- |
| `src/app.js` | 실행 루프, 모듈 연결, 선택·실행 상태 | 초기화, 명령 조율, 종료 |
| `src/simulation/` | 코어 인스턴스, 입력 검증, 시뮬레이션 서비스 | 명령, 조회, step/reset/dispose |
| `src/rendering/` | renderer, geometry, material, 시각화 캐시 | 생성, 상태 동기화, resize/dispose |
| `src/ui/` | DOM 이벤트와 표시 상태, 포인터 입력 | 생성, 표시 갱신, 명령 callbacks, dispose |
| `src/data/` | 정적 메시 등 읽기 전용 입력 자료 | 데이터 제공 |
| `native/src/` | 도메인 상태, 계산, C++ 자원 | 명시적 명령·const 조회 |
| `native/bindings/` | ABI 변환·외부 런타임 접점·host 코디네이터 | 검증 책임, 버퍼 대여·수명 계약 |

시뮬레이션은 DOM·Three.js·UI를 참조하지 않습니다. 렌더러는 UI를 참조하지 않습니다.
물리 step은 app의 실행 루프 또는 명시적 단일 스텝 명령이 호출합니다.
UI와 렌더러에서 물리 루프를 시작하지 않습니다. 화면 모듈은 필요한 조회와 명령만 전달받고,
전체 가변 앱 상태를 공유하는 객체를 전달하지 않습니다.

상태와 수명이 있는 화면 서비스는 `constructor`와 명명된 갱신 메서드, `dispose()`를 가진
클래스로 표현합니다. 상태 없는 계산과 데이터 변환은 독립 함수로 둡니다.
객체 생성만 감추는 불필요한 factory·manager·상속 계층을 추가하지 않습니다.

## 2. 명령·조회·실패 형식

- 외부 입력은 시뮬레이션/ABI 경계에서 유한성·범위·소유자를 검증합니다.
  실패한 입력은 상태 일부만 변경하는 결과를 남기지 않도록 처리합니다.
- 변경은 `updateSettings`, `moveBody` 같은 동작 이름의 명령으로, 조회는 getter 또는
  명시적 조회 메서드로 제공합니다. 화면의 표시 상태와 물리 상태를 구분합니다.
- 잘못된 입력은 `TypeError`/`RangeError` 등 기존 공개 API 계약에 따른 실패로 전달합니다.
  초기화 실패는 app이 화면에 표시하고 루프와 이미 생성한 자원을 정리합니다.
- C++은 STL·값 타입·RAII를 우선합니다. 단독 소유는 `std::unique_ptr`, 읽기는
  `const std::vector<T>&` 등 const 참조를 사용합니다. raw pointer는 ABI 대여나
  수명이 설명된 비소유 접근으로 한정합니다.

## 3. 생성·갱신·해제

생성 순서는 코어 초기화 → 시뮬레이션 → 렌더러 → UI → 실행 루프입니다.
종료는 루프 중단 → 이벤트/observer 해제 → 화면 자원 해제 → 시뮬레이션 해제 순서입니다.
`dispose()`는 반복 호출할 수 있어야 하며 종료된 시뮬레이션의 명령은 오류로 거부합니다.
해제 후 남은 외부 참조가 자원을 계속 소유할 수 있는지 문서에 명시합니다.

렌더링은 코어 상태의 읽기 소비자입니다. C++ 저장소 재할당과 Wasm 메모리 증가에 따른
JS TypedArray 무효화는 다른 조건이므로 별도로 기록합니다. 버퍼 대여의 수명과
다음 호출이 내용을 바꾸는 조건을 API 주석에 남깁니다.

## 4. 폴더와 파일 형식

```text
native/
  src/core/            # 수학·설정·상수
  src/simulation/      # 상태·객체·해법·탐색
  src/collision/       # 형상·접촉·충돌 응답
  bindings/           # C ABI 또는 Emscripten 접점
  bindings/include/   # 공개 C ABI 헤더가 있는 프로젝트만 사용
  sources.txt         # CMake와 Wasm이 공유하는 구현 목록
  CMakeLists.txt
src/
  app.js
  simulation/
  rendering/
  ui/
  data/               # 정적 데이터가 있을 때 사용
tools/                # 빌드·서버·개발·검증 CLI
tests/
  unit/
  native/
  browser/
  fixtures/           # 보존 원본, 런타임에서 import 금지
docs/
artifacts/            # 재생성 가능한 현재 검증 결과
build/                # CMake·중간 산출물·보존 스냅샷
```

도메인에 필요하지 않은 빈 폴더는 만들지 않습니다. 제품 코드와 보존 자료·생성 코드를
혼합하지 않습니다. 기존 과제 PDF·영상과 과거 검증 기록은 보존합니다.

C++ 파일은 PascalCase `.h/.cpp`, JS 복합 파일명은 kebab-case, Node 도구는 `.mjs`를
사용합니다. 단위 검사는 `<feature>.test.js` 또는 `.test.mjs`로 이름을 붙입니다.
브라우저 앱은 ES module 형식을 사용합니다. PBD의 기존 UMD/CommonJS 어댑터·데이터·
생성 Wasm loader는 호환 경계로 유지하고 전역 package type을 임의로 바꾸지 않습니다.

## 5. 코드 서식

- 모든 언어의 소스·개발 도구·검사 코드는 초기화·입력 검증·반복 계산·결과 반영·정리의 경계를
  빈 줄 하나로 구분합니다. 같은 객체의 초기화와 같은 계산·오류 처리는 함께 묶으며 주석은 설명 대상에 붙입니다.
- C++은 [Unreal Engine 소스 기반 코딩 표준](cpp-coding-standard.md)을 따릅니다.
  C++20, Allman, 탭 폭·기본·continuation 들여쓰기 4를 적용합니다.
  선언·정의·호출 인자와 계산·조건식은 한 줄에 유지하며 헤더의 각 멤버 사이는 빈 줄 하나로 구분합니다.
  일반 class/struct는 F, 클래스 템플릿은 T, enum class는 E 접두사를 사용합니다.
  함수·멤버·인자·지역 변수·상수는 PascalCase, bool은 b, 출력은 Out,
  누적 수정은 InOut을 사용합니다. 공개 ABI 이름·값·레이아웃은 유지합니다.
- 읽기 인터페이스를 public에 먼저 두고 내부 상태는 private으로 캡슐화합니다.
  const 조회·명시적 타입을 우선하며 auto는 람다·복잡한 iterator·의존 타입에 한정합니다.
  STL·RAII·표준 고정폭 정수를 사용하는 독립 C++ 프로젝트 규칙입니다.
  프로젝트 타입은 전역에, 구현 전용 함수·상수는 익명 네임스페이스에 선언합니다.
- JS는 들여쓰기 2칸, 작은따옴표, 세미콜론, camelCase 함수·변수, PascalCase 클래스,
  UPPER_SNAKE_CASE 상수입니다. Prettier 3.6.2와 .prettierrc.json을 적용합니다.
- .editorconfig의 C++ 전용 섹션과 VS Code [cpp] 설정을 .clang-format에 맞춥니다.
  npm run check:format으로 서식을 검사하고 실행 로그에 formatter 버전을 기록합니다.
- 생성 코드·보존 fixture·정적 메시 배열·과거 검증 기록은 자동 재포맷에서 제외합니다.
  서식·이름 변경과 의미 변경은 검토에서 구분합니다.

## 6. 공통 개발 명령

프로젝트 루트에서 실행합니다. PowerShell에서는 `npm.cmd`를 사용할 수 있습니다.

| 명령 | 계약 |
| --- | --- |
| `npm ci --ignore-scripts` | 각 프로젝트의 lockfile로 개발 의존성 설치 |
| `npm start` | 해당 프로젝트의 로컬 서버 |
| `npm run build:core` | 고정 버전 Emscripten으로 C++20/Wasm 재빌드 |
| `npm run build` | 해당 프로젝트의 웹 실행 산출물 구성 |
| `npm test` | 실제 Wasm을 사용하는 Node 단위·수치 회귀 |
| `npm run test:native` | C++ 빌드와 CTest |
| `npm run test:browser` | Playwright/Chrome으로 실제 화면·입력·오류 검사 |
| `npm run check:format` / `npm run format` | JS/config·C++ 서식 검사 / 적용 |
| `npm run check:structure` | 경로·manifest·의존 방향 검사 |
| `npm run check:runtime` | C++ 원본과 배포 Wasm의 해시 연결 검사 |
| `npm run check` | 서식 → 구조 → Wasm 원본 연결 → Node 회귀 검사 |
| `npm run verify` | 재빌드 → check → native Debug/Release → 브라우저 검사 |
| `npm run check:execution` | 전체 검사 결과의 소스·배포물 해시·종료 코드 확인 |

루트 `CMakeLists.txt`를 `cmake -S .`의 진입점으로 사용합니다.
SDK 버전은 Emscripten 4.0.23을 유지하고 컴파일 옵션·ABI·메모리 모델은 프로젝트별
필요에 따라 보존합니다. Boids의 고정 메모리와 PBD의 성장 가능 메모리를 동일하게
만들지 않습니다. 배포 Wasm과 번들은 생성 도구로 갱신합니다.

## 7. 문서와 검증 형식

`docs/architecture.md`는 책임표 → 의존 흐름 → 소유권·수명 → 명령·조회·실패 →
빌드 산출물 순서로 작성하고 실제 소스 경로를 연결합니다.
리뷰 기록은 역할, 문제 파일·조건·심각도, 수정, 재검증 결과를 남깁니다.
결과는 실행 명령·환경·통과 수·미실행 범위로 기록합니다.
소스 검토, 빌드, Node 수치 검사, 브라우저 동작, CPU 시간, GPU 측정은 구분합니다.
구조 변경만으로 정확도나 성능 개선을 주장하지 않습니다.

## 8. 적용 범위와 재현

`native/sources.txt`는 `native/src/`와 `native/bindings/`의 전체 구현 목록입니다.
PBD 네이티브 CMake만 Emscripten 전용 bindings를 제외하고 Wasm 빌드는 전체 목록을 사용합니다.
순수 도메인 헤더에서 host bindings 구현 헤더를 참조하지 않습니다. Boids 공개 C ABI 헤더의
공유 용량·슬롯 상수 의존은 기존 호환 계약으로 명시합니다.

`check:structure`는 파일 존재·소스 등록·문자열로 명시한 import/include·HTML 자원 경로와
기본 계층 의존을 확인하는 빠른 검사입니다. JS/C++ 전체 의미 분석이나 동적 경로의 증명은 아닙니다.
새 개발 환경에서는 `npm run check`로 배포 Wasm과 회귀 검사를 실행합니다.
`npm run verify`는 Wasm 재빌드·네이티브 Debug/Release·브라우저 검사까지 실행합니다.
실행 결과는 `artifacts/validation.json`에 기록하며 `npm run check:execution`으로 해시를 대조합니다.

## 9. SPH 적용

SPH도 `native/src/`, `native/bindings/`, `src/simulation/`, `src/rendering/`, `src/ui/`,
`src/data/`, `tools/`, `tests/{unit,native,browser,fixtures}/`로 구성합니다.
Worker가 C++ 코어를 소유하고 JS에는 독립 스냅샷을 전달합니다. 단독 HTML 배포는 같은
Wasm과 Worker를 내장하며, Python/Pyodide는 비교용 fixture에서만 보존합니다.
SPH의 `verify:migration`은 공개된 Python 기준선과 실제 native/Wasm을 비교합니다.
모델별 메모리·시간 정책과 공개 API가 다른 점은 각 `architecture.md`에 설명합니다.


## 10. 공통 Windows 배포

`build:web`, `build:exe`, `test:exe`, `release:exe` 명령과 Windows x64 포터블 EXE·SHA-256·검증 manifest 형식을 통일합니다.
각 프로젝트의 독립 빌드를 유지하며 [배포 계약](desktop.md)을 따릅니다.


현재 웹 원본 `src/index.html`, `src/styles/app.css`와 `dist/`, `wasm/`, `CMakePresets.json`을 저장소에 포함합니다.
공통 경로·빌드 명령은 [project-layout.md](project-layout.md)를 따릅니다. 생성 산출물은 빌드 도구로 갱신합니다.


C++ 파일의 공통 배치와 기존 경로의 이동 기준은 [native 내부 구조](native-layout.md)를 따릅니다.
