# Windows EXE 배포 절차

이 프로젝트의 package-lock.json과 node_modules를 사용해 Windows x64 포터블 EXE를 만듭니다.
웹 직접 실행은 [README](../README.md)의 안내를 따릅니다.

## 개발자 명령

```powershell
npm.cmd ci --ignore-scripts
node node_modules/electron/install.js
npm.cmd run release:exe
```

C++ 원본을 수정했다면 Emscripten 4.0.23으로 `npm.cmd run build:core`를 먼저 실행합니다.
원본/Wasm 해시가 다르면 패키징은 실패합니다. Electron 44.9.3과 electron-builder 26.15.3을
고정합니다. 최초 설치·패키징에는 npm 및 Electron/NSIS 다운로드 연결이 필요합니다.

| 명령 | 작업 |
| --- | --- |
| `npm.cmd run build:web` | EXE용 웹 자원 생성; PBD는 기존 0.139.2 UMD 자원을 로컬 포함 |
| `npm.cmd run build:exe` | 원본/Wasm 검사 → 웹 생성 → 포터블 EXE → SHA-256 |
| `npm.cmd run test:exe` | 실제 EXE만 한글·공백 폴더에 복사 → 숨김 창 기능 검사 |
| `npm.cmd run release:exe` | build → check → test:distribution → build:exe → test:exe → 검증된 배포 manifest |

모든 명령은 이 저장소 루트에서 실행합니다. 이웃 프로젝트나 상위 과제 모음에 의존하지 않습니다.

## 배포물과 실행

`release/<제품명>-<version>-win-x64.exe` 하나를 복사하여 더블클릭합니다.
사용자 PC에 Node.js·Python·브라우저·SDK·인터넷·별도 서버가 필요하지 않습니다.
포터블 실행 시 내부 런타임은 임시 폴더에 풀리고 사용자 프로필에 캐시가 남을 수 있습니다.
코드 서명은 적용하지 않으며 관리자 권한·자동 업데이트를 사용하지 않습니다.
Windows x64와 각 렌더러가 요구하는 그래픽 드라이버/WebGL 환경이 필요합니다.

`.exe.sha256`은 무결성 확인용입니다. `release-manifest.json`은 실제 검사한 EXE의
해시·크기·버전·기능 검사·오류·외부 요청 기록을 담습니다. 실행에 필수인 부속 파일은 아닙니다.
배포 검사가 실패하면 이전 성공 manifest를 제거하고 새 성공 기록을 만들지 않습니다.
`win-unpacked/`와 builder 설정 로그는 내부 중간 결과입니다.

```powershell
Get-FileHash .\release\<실제-파일명>.exe -Algorithm SHA256
```

## 구현과 검증 범위

`desktop/config.cjs`는 제품 ID·자원 허용 목록·CSP를 정의합니다.
`desktop/main.cjs`, `tools/package-desktop.mjs`, `tools/release-desktop.mjs`,
`tests/desktop/desktop.mjs`는 패키징·실행 검사를 담당합니다.
파일 선별은 새 임시 staging에서 수행하며 개발 node_modules·소스 자료·PDF·영상을 포함하지 않습니다.
라이선스 고지는 앱 자원과 Electron 런타임에 보존합니다.

렌더러는 Node 통합을 끄고 context isolation·sandbox를 켭니다. 자체 프로토콜의 허용된 자원만
제공하고 외부 HTTP/WebSocket 및 file 요청을 차단합니다.

검사를 실행하면 `artifacts/desktop-results.json`에 실제 결과를,
`artifacts/desktop-exe.png`에 패키지 렌더러의 화면을 기록합니다.
현재 UI 회귀와 반복 해제를 검사하며 실제 통과 수와 오류는 해당 실행 기록으로 확인합니다.
숨김 창 검사는 FPS·GPU 성능 측정이나 다른 PC 호환성 검증을 의미하지 않습니다.
이 명령은 로컬 배포물을 생성하며 GitHub 업로드는 수행하지 않습니다.


웹·폴더 경로는 [공통 폴더·빌드 구조](project-layout.md)를 따릅니다.
기존 Embind 로더에는 동적 함수 생성이 있어 해당 CSP에 unsafe-eval을 허용합니다.
렌더러의 Node API 비활성화·sandbox·외부 네트워크 차단은 함께 적용합니다.
