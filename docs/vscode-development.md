# VS Code 개발 환경

프로젝트 루트의 `pbd-lab.code-workspace`를 엽니다. 현재 폴더 이름은 **PBD Lab - 현재 소스**이며
`native/src/{core,simulation,collision}/`가 실제 C++ 작업 대상입니다.

```powershell
code --reuse-window .\pbd-lab.code-workspace
code --reuse-window --goto .\native\src\simulation\PhysicsWorld.cpp:54
```

## 현재 파일과 외부 변경 반영

이 창과 자동화 도구는 같은 디스크 파일을 편집합니다. 별도 소스 복사·가져오기 작업은 없습니다.
`native/src/`는 파일 감시 대상이며, 저장된 외부 변경은 열려 있는 깨끗한 문서에 자동으로 반영됩니다.
편집기에서 수정한 내용은 800ms 지연 자동 저장으로 같은 파일에 기록합니다.
저장 충돌은 사용자에게 알리며 디스크 내용을 무조건 덮는 설정은 사용하지 않습니다.

`artifacts/`, `build/`, `.tools/`, `node_modules/`, `dist/`, `release/`는 Explorer·검색·C++ 탐색에서
제외합니다. `artifacts/`는 추가로 읽기 전용입니다. 과거 검증 사본 안의 `CBody`, `CTimeIntegrator`,
`world/`, `solvers/` 경로를 현재 코드로 수정하지 않습니다.
기존에 열려 있던 과거 탭의 미저장 내용을 강제로 버리거나 편집 기록을 삭제하지 않습니다.
탭에는 상대 경로를 표시하고 빠른 파일 열기의 이전 파일 기록을 제외해 동명 사본의 혼동을 줄입니다.

## 도구 설정과 구성

이 환경은 Windows x64, Visual Studio 2026, CMake 4.2 이상과 C++20을 사용합니다.
VS Code의 Microsoft C/C++, CMake Tools, Prettier 확장을 권장합니다.

```powershell
pwsh -NoProfile -File tools/vscode.ps1 -Action Setup
```

공개 `.vscode/settings.json`에는 특정 PC의 도구 절대 경로를 넣지 않습니다.
`Setup`은 설치된 Visual Studio에서 MSVC·CMake·clang-format 경로를 찾아 로컬 프로젝트 설정에 기록하고,
Native/Wasm용 `.vscode/c_cpp_properties.json`을 재생성합니다. 도구 업데이트나 다른 PC로 이동한 뒤
다시 실행하면 절대 경로를 갱신할 수 있습니다. 이때 생긴 PC별 경로는 커밋하지 않습니다.
사용자 전역 설정·PowerShell 프로필·영속 환경 변수는 변경하지 않으며 확장은 자동 설치하지 않습니다.

기본 C++ 구성은 `Native MSVC (CMake)`이며 CMake Tools의 include·매크로·컴파일 옵션을 사용합니다.
구성 프리셋은 `native-msvc`, 빌드는 `native-debug` 또는 `native-release`를 선택합니다.
C++ 탐색 경로는 `native/src/`, `native/bindings/`, `tests/native/`로 한정합니다.

`native/bindings/Bindings.cpp`를 편집할 때는 `Wasm (Emscripten)` 구성을 선택할 수 있습니다.
`Setup`은 `EMSDK` 또는 `.tools/emsdk-main`의 clang·wasm32 target·sysroot·compat 헤더를 설정합니다.
편집기의 `clang-x64` 모드가 wasm32 ABI를 완전히 재현한다고 보장하지 않으며, 실제 대상 검증은
Emscripten 4.0.23 빌드로 수행합니다.

## 빌드·검사 작업

`Terminal → Run Task`에서 다음 15개 작업을 선택합니다. 기본 빌드 단축키는 `Ctrl+Shift+B`입니다.

| 작업 | 실제 동작 |
| --- | --- |
| `PBD: Setup toolchain` | 프로젝트 전용 도구 경로와 Native/Wasm 편집 설정 갱신 |
| `PBD: Configure` | 공통 `native-msvc` 프리셋으로 구성 |
| `PBD: Build Debug` | 구성 후 네이티브 Debug 빌드, 기본 빌드 작업 |
| `PBD: Build Release` | 구성 후 네이티브 Release 빌드 |
| `PBD: Test Debug` | Debug 빌드와 CTest, 기본 검사 작업 |
| `PBD: Test Release` | Release 빌드와 CTest |
| `PBD: Build Wasm` | `build:all`: C++/Wasm과 웹 산출물을 함께 갱신 |
| `PBD: Test Wasm` | 위 빌드가 끝난 뒤 Node 전체 회귀 검사 |
| `PBD: Build web` | 현재 Wasm 해시를 검사하고 웹 산출물만 갱신 |
| `PBD: Check format` | 기존 공통 서식 검사 |
| `PBD: Format C++` | 제품 C++·네이티브 테스트에 clang-format 적용 |
| `PBD: Check all` | 서식·구조·배포 해시·Node 검사 |
| `PBD: Browser tests` | 기존 브라우저 회귀 검사 |
| `PBD: Serve` | 준비 신호를 제공하는 Python HTTP 서버, 포트 8000 |
| `PBD: Rebuild and serve` | Wasm·웹 빌드를 끝낸 뒤 서버 준비까지 순서대로 대기 |

파일 하나를 독립 컴파일하지 않고 CMake의 전체 코어·테스트 의존 관계를 빌드합니다.
공통 Node 진입점을 사용하므로 VS Code 작업과 터미널의 `npm.cmd run ...`이 같은 빌드 경로를 따릅니다.
소스 저장만으로 Wasm이 재컴파일되지는 않습니다. C++ 변경을 브라우저 실행에 반영할 때는
`PBD: Build Wasm` 또는 재빌드를 포함한 브라우저 디버그 구성을 실행합니다.

## 디버깅과 포맷

| F5 구성 | 실행 대상 |
| --- | --- |
| `PBD: C++ core tests (Debug)` | Debug 빌드 후 `build/native/Debug/pbd_native_tests.exe` |
| `PBD: C++ contract tests (Debug)` | Debug 빌드 후 `build/native/Debug/pbd_refactoring_tests.exe` |
| `PBD: Browser / JavaScript (rebuild Wasm)` | Wasm·웹 재빌드와 HTTP 준비 후 Chrome에서 앱 실행 |

C++ 구성은 진입점에서 멈추며 실행 파일과 일치하는 소스를 요구합니다.
현재 `native/src/simulation/PhysicsWorld.cpp` 등의 줄에 F9로 중단점을 둔 뒤 F5/F10/F11로
실행·단계 진행을 확인합니다. `.exe`와 `.pdb`는 `build/native/Debug/`에 생성합니다.
브라우저 구성은 JavaScript·렌더링 연동용이며 최적화된 배포 Wasm의 C++ 소스 중단점 구성과 구분합니다.

C++ 명시 저장에는 프로젝트 `.clang-format`의 탭 폭 4·Allman·한 줄 인자/계산 배치를 적용합니다.
`Ctrl+S` 명시 저장의 자동 포맷과 800ms 자동 저장을 각각 검증했습니다.
자동 저장이 모든 명시 저장 포맷 동작을 수행한다고 가정하지 않습니다.

## 서버 수명

`PBD: Serve`는 `127.0.0.1:8000`에서 준비 신호를 보냅니다. 같은 포트의 서버가 현재 프로젝트의
HTML·앱·Wasm을 제공하는지 확인한 경우에만 재사용하며, 다른 서버가 점유한 포트는 오류로 처리합니다.
Windows에서는 독점 바인딩으로 중복 리스너를 막습니다.

브라우저 디버깅 종료는 서버 종료를 뜻하지 않습니다. 서버를 종료할 때는 해당 작업 터미널에서
Ctrl+C 또는 `Terminal → Terminate Task → PBD: Serve`를 사용합니다.
기존 서버를 재사용했다면 그 서버를 시작한 실행 주체가 종료 책임을 가집니다.

## 2026-11-08 검증 범위

설치된 VS Code **1.145.2**의 별도 Extension Development Host에서 공식 VS Code API로 다음을 확인했습니다.

- 실제 workspace 루트와 적용된 제외·읽기 전용·자동 저장 설정
- 현재 `native/src/simulation/Body.h`를 열어 `class FBody` 내용 확인
- `native/src/`에 배타적으로 만든 임시 파일의 외부 디스크 변경이 열린 문서에 자동 반영됨
- 편집기 수정이 명시 저장 없이 같은 파일에 자동 저장됨
- C++ 명시 저장 시 Allman·탭 포맷 적용
- `Ball.h`의 `FBody` 정의 이동 대상이 현재 `native/src/simulation/Body.h`임을 확인

실제 결과는 `artifacts/vscode-reset-20261108/editor-api-results.json`에 기록했습니다.
임시 파일은 검사 종료 시 제거했으며 제품 C++와 기존 사용자 탭의 내용을 변경하지 않았습니다.
이 결과는 에디터 API 통합 검증이며 물리적인 F5 키 입력·화면 클릭 검증과 구분합니다.
최종 C++ 소스에서는 설정된 cppvsdbg 구성을 실제로 시작하여 `PBD: Build Debug` 사전 작업,
진입점 정지, 현재 `PhysicsWorld.cpp`의 `Step` 중단점, `Dt`·`Substeps` 변수 조회,
단계 실행과 종료 코드 0을 확인했습니다. 근거는 같은 폴더의 `debug-api-results.json`입니다.
Wasm 편집용 컴파일 인자로 현재 `Bindings.cpp`의 구문 검사도 통과했습니다.

현재 공개 소스에 대한 판정과 미실행 범위는 [독립 리뷰](portfolio-review.md)를 확인합니다.

참조: [workspace 설정](https://code.visualstudio.com/docs/configure/settings),
[Extension Development Host 검사](https://code.visualstudio.com/api/working-with-extensions/testing-extension),
[CMake Presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html).
