# GitHub 공개 파일

[실행 안내](../README.md) · [프로젝트 배치](project-layout.md) · [.gitignore](../.gitignore)

이 프로젝트 폴더를 독립된 저장소 루트로 공개합니다. 상위 과제 모음, SDK와 로컬 작업 자료는 포함하지 않습니다.
제외한 원본은 로컬에 보존하며 공개 파일만 별도 트리로 검증합니다.

## 포함 범위

| 대상                                                                                            | 목적                                         |
| ----------------------------------------------------------------------------------------------- | -------------------------------------------- |
| `native/`, `src/`                                                                               | C++20 물리, Wasm 연결, 화면·입력과 원본 메시 |
| `tests/unit/`, `tests/native/`, `tests/browser/`, `tests/desktop/`, `tests/fixtures/`의 JS·HTML | 현재 자동 검사와 회귀 기준선                 |
| `tools/`, `desktop/`                                                                            | 빌드·검증·개발·EXE 패키징                    |
| `index.html`, `pbd_model.html`, `styles.css`, `dist/`                                           | 설치 없이 실행하는 웹 배포물과 라이선스 고지 |
| `wasm/pbd_core.js`, `wasm/pbd_core.wasm`, `wasm/build-manifest.json`                            | SDK 없는 웹 빌드와 소스/산출물 해시 확인     |
| `README.md`, 허용목록의 `docs/`, `papers/sources.json`                                          | 현재 사용·설계·검증 안내와 논문 출처         |
| package·lockfile, CMake·CI·서식 설정, workspace·공용 `.vscode/`                                 | 재현 가능한 의존성과 개발 명령               |

문서는 `.gitignore`의 허용목록으로 관리합니다. 현재 설계, 코드 독해, 코딩 표준, 개발 환경,
품질·검증, 배포와 XPBD 설명만 공개합니다. 과거 작업 분담·변경 보고서와 감사 문서는 포함하지 않습니다.
`dist/`와 `wasm/`은 필요한 배포물입니다. 생성 파일은 빌드 도구로 갱신하고 직접 편집하지 않습니다.

## 제외 범위

- SDK·의존성: `.tools/`, `node_modules/`, `.venv/`
- 로컬 산출물: `build/`, `artifacts/`, `release/`, `tmp/`, 캐시·로그·스크린샷·검사 리포트
- 개인 설정: `CMakeUserPresets.json`, `.vscode/c_cpp_properties.json`, `*.local.code-workspace`, `.vs/`, 환경 파일
- 작업 자료: `AGENTS.md`, `.codex/`, `.agents/`, 과거 문서, `tests/legacy/`
- 원본 자료: 과제 PDF·영상·메모, `study/`, `build_report.py`, 다운로드한 논문 PDF

`papers/sources.json`의 `file`은 로컬 보존 파일명입니다. 논문 PDF가 저장소에 있다는 뜻은 아닙니다.
`*.wasm`, `*.py`나 `tests/fixtures/` 전체를 무시하면 필요한 배포·검사 파일을 잃으므로 개별 정책을 적용합니다.
EXE와 체크섬은 [배포 절차](desktop.md)로 검증한 뒤 Release Assets로 제공합니다.

## 실행과 검사

다운로드한 폴더의 `index.html` 또는 `pbd_model.html`을 Chrome·Edge에서 직접 엽니다.
`dist/` 상대 경로를 유지하면 `file://`로 실행합니다. WebGL이 필요하며 실행 중 CDN 연결은 필요하지 않습니다.

개발과 HTTP 실행은 아래 명령을 사용합니다.

```powershell
npm.cmd ci --ignore-scripts
npm.cmd run build
npm.cmd start
```

서버 주소는 <http://127.0.0.1:4173/>입니다. `npm.cmd run test:distribution`은 배포 HTML의
HTTP·직접 파일 실행을 검사합니다. `test:browser`는 Python 서버와 설치된 Chrome을 사용합니다.
브라우저 검사에 필요한 서버는 자동 검사 환경이며 직접 HTML을 여는 사용자의 실행 조건과 다릅니다.

공개 트리에서 다음을 확인합니다. `check`에는 clang-format 22.1.3이 필요합니다.

```powershell
npm.cmd run check
npm.cmd run test:distribution
npm.cmd run check:quality
git status --short --untracked-files=all
git ls-files
git status --short --ignored
```

이미 추적한 파일은 `.gitignore`만 바꿔도 제거되지 않습니다. 웹 Upload files 역시 무시 규칙을 적용하지 않으므로
실제 공개 목록을 확인합니다. C++을 바꿨다면 Emscripten 4.0.23으로 Wasm을 다시 빌드하고
native Debug/Release와 Node·브라우저 결과를 각각 기록합니다.

## 독립 리뷰와 지문

`check:quality`는 [품질 기준](quality-standard.md)에 따른 독립 판정과 공개 소스 지문을 확인합니다.
문서·설정 선별도 입력 파일 집합을 바꾸므로 최종 공개 트리 자체를 다시 검토해야 합니다.
기존 점수의 지문만 바꾸거나 검사를 제거하지 않습니다. 소스 지문 검사는 `.gitignore`와 별개이므로
공개에서 제외한 자료를 공개 트리에 남겨 두지 않습니다.

검사 기록은 [독립 리뷰](portfolio-review.md)와 [점수 근거](portfolio-score.json)에 남깁니다.
원격 Actions 성공은 실제 원격 실행 뒤 확인하며 로컬 검사·브라우저·GPU·성능 측정을 구분합니다.
