# 공통 폴더·빌드 구조

세 프로젝트는 다음 경로와 책임을 공유합니다. 도메인 내부 파일명과 전용 메시·디버거·보존 기준선은 해당 프로젝트에 필요한 구성을 유지합니다.

```text
src/
  index.html             웹 화면 원본
  app.js                 초기화·실행·정리
  styles/app.css         화면 스타일 원본
  simulation/            Wasm 어댑터와 시뮬레이션 조율
  rendering/             상태 조회·Three.js 자원
  ui/                    입력과 표시
  data/                  정적 데이터가 있는 프로젝트
native/
  src/{core,simulation,collision}/  C++20 도메인
  bindings/              ABI/Emscripten 경계
  sources.txt            공통 C++ 구현 목록
wasm/                    배포용 Wasm·빌드 메타데이터
  build-manifest.json
dist/                   웹 빌드 산출물
  index.html
desktop/                Electron 설정·창·실행 검사
tests/{unit,native,browser,desktop,fixtures}/
tools/                  공통 명령 진입점과 전용 빌더
docs/                   설명·검증 범위
build/native/           CMake Debug·Release
release/                포터블 EXE·SHA-256·배포 manifest
artifacts/              현재 검사 로그·화면
CMakeLists.txt
CMakePresets.json
package.json
package-lock.json
```

루트 `index.html`과 기존 `yoosh.html`·`pbd_model.html`·`SPH-Fluid-Lab.html`은 호환 진입점입니다.
Boids/PBD는 `src/index.html`로부터 배포 HTML을 생성합니다. SPH의 `src/index.html`은 개발 서버용이며,
동일한 `src/app.js`를 사용하는 `src/standalone-entry.js`로 단독 배포 HTML을 생성합니다.
루트 `styles.css`가 있는 프로젝트는 `src/styles/app.css`를 읽는 호환 CSS입니다.
HTML·CSS 수정은 `src/`에서 수행하고 `npm run build`로 생성물을 갱신합니다.

Boids의 `src/simulation/generated/`는 기존 ABI와 고정 메모리를 사용하는 내장 Base64 어댑터입니다.
공개 모듈 경로를 유지하며 `wasm/boids-core.wasm`은 같은 바이트를 웹 빌드에서 추출합니다.
PBD/SPH의 Embind 로더·메모리 성장·Worker 소유권은 각각 유지합니다. 세 코어의 파일 형식이나 ABI를 강제로 합치지 않습니다.

| 명령 | 세 프로젝트의 공통 동작 |
| --- | --- |
| `npm ci --ignore-scripts` | 프로젝트 자신의 lockfile로 설치 |
| `npm start` | Node 서버, 기본 `http://127.0.0.1:4173/`, `PORT`로 변경 |
| `npm run build` / `build:web` | 원본/Wasm 대조 후 `dist/` 생성; SDK 불필요 |
| `npm run build:core` | Emscripten 4.0.23으로 C++20/Wasm 재빌드 |
| `npm run build:all` | build:core → build |
| `npm run build:native` | 공통 CMake preset으로 Debug 빌드 |
| `npm run build:native:release` | 같은 build/native에서 Release 빌드 |
| `npm run test:native` / `test:native:release` | 해당 구성 빌드 후 CTest |
| `npm run check:layout` | 경로·산출물·빌드 명령 계약 확인 |
| `npm run check` | 서식·구조/경로·Wasm 해시·Node 회귀 |
| `npm run test:distribution` | 실제 Chrome으로 dist와 호환 HTML 초기화·CSP·오프라인 확인 |
| `npm run build:exe` / `test:exe` | 포터블 EXE 생성 / 실제 단일 EXE 기능 검사 |
| `npm run release:exe` | build → check → test:distribution → build:exe → test:exe |

Windows 네이티브 프리셋은 Visual Studio 18 2026 x64와 CMake 4.2 이상을 사용합니다.
CMake가 PATH에 없으면 VS Installer로 설치 위치를 조회합니다. `CMAKE`로 실행 파일을 지정할 수 있습니다.
Python은 PBD/SPH의 C++/Wasm 재컴파일에 사용하며 일반 웹 빌드·실행에서는 필요하지 않습니다.
VS Code의 `Lab: Build web`, `Lab: Build native Debug/Release`, `Lab: Test native Debug/Release`,
`Lab: Release EXE` 작업을 공통으로 제공합니다. 기존 디버깅 작업도 보존합니다.

`tools/check-layout.mjs`는 현재 구조의 경로와 명령 계약을 검사합니다.
상위 과제 모음의 배포 명령은 공유 빌드·서버·검사·Electron 코드와 CMakePresets의 동일성도 검사합니다.
각 프로젝트는 이웃 프로젝트 없이 독립 설치·빌드할 수 있습니다.
과거 build/artifacts 및 원본 과제 자료는 보존합니다. 기존 작업 사본을 새 구조로 재분류하거나 삭제하지 않습니다.


C++ 파일의 공통 배치와 기존 경로의 이동 기준은 [native 내부 구조](native-layout.md)를 따릅니다.
