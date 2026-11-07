# 2026-11-08 독립 리뷰: 배포·Box UI·속도 명명·공간 컴포넌트

reviewer: independent-verification  
프로필: **github-public**  
판정: **승인, 99/100, 미해결 차단 결함 0건**

제품 구현에 참여하지 않은 리뷰어가 최종 변경과 현재 실행 근거를 검토했습니다.
이번 판정은 Wasm 내장 배포, Box 전용 앱, PreSolve 명명과 두 공간 클래스 추출 및
공개 파일 선별을 포함합니다. 이전 승인·점수의 지문만 갱신하지 않았습니다.
20개 사유와 소스 줄은 [portfolio-score.json](portfolio-score.json)에 있습니다.

## 최종 변경 판정

| 경계 | 확인한 결과 |
| --- | --- |
| 배포 | 현재 Wasm 바이트를 app 번들에 내장하고 Emscripten의 wasmBinary 입력으로 전달합니다. 개발 원본은 HTTP fallback을 유지합니다. |
| 앱 정책 | 기본 장애물·생성/편집 UI는 Box 전용이며 Ball은 유지합니다. Sphere와 options.shape 우회를 변경 전에 거부하고 shape 전환 명령을 제거했습니다. |
| 호환성 | raw World/Wasm의 sphere 기능과 JS predictedVelocities selector는 유지합니다. PreSolveVelocities는 같은 제약 전 스냅샷의 C++ 이름입니다. |
| 공간 인덱스 | FTetrahedronSpatialIndex가 기존 dense grid·정렬 map·overflow·역기저를 소유합니다. 실제 해시테이블로 바꾼 구현이 아닙니다. |
| BVH | FSurfaceBvh가 topology·nodes·순서·스택을 소유하고 위치는 호출 중 빌립니다. 결과와 진단 topology는 독립 값입니다. |
| 접촉 조율 | Solver가 두 컴포넌트를 소유하고 준비·접촉 생성·투영을 조율합니다. 신규 캐시 완성 후 등록·통계 마지막 commit·기존 basic guarantee를 유지합니다. |

기울기·수식·후보 순서와 stable_sort·오른쪽우선 동률·strict 최소값 갱신을 대조했습니다.
첫 Rebuild/Refit, 동일 토폴로지·위치·유효 설정·직렬 질의 전제를 명확히 했습니다.
기존 공용 검색 스택이 몸체별 BVH에 속하므로 보존 용량이 분산되는 메모리 특성은 달라집니다.
성능 개선이나 실제 병렬 구현으로 평가하지 않습니다.

## 실제 검증

| 범위 | 결과 |
| --- | --- |
| root와 공개본 전체 게이트 | 각각 build → check → native Debug → native Release → browser, 5개 exit code 모두0 |
| Node·Native | 각각 Node136/136, Debug·Release CTest2/2; native 계약 실행 파일에 직접 컴포넌트 검사2개를 포함한29개 |
| UI·수명 | Chrome UI26개·수명3개, 예외 주입 회귀 외 오류·실패 요청/응답 없음 |
| 배포 | HTTP3·file3 엔트리의 초기화·물리 호출·유한 상태 통과, 외부/실패/Wasm 요청0 |
| 기준선 | source ZIP·47파일·이전 Wasm provenance와 비교 입력 해시 검증, 10장면×60프레임 정확 비교 최대 차이0 |
| 공개 선별 | 152파일, 제품116파일 원본 일치, 상대 링크103개·끊김0, 필수 dist/Wasm/새 native 클래스 포함 |

리뷰어가 최종 native29개, controller5개, 배포6개를 직접 재실행했습니다.
두 프로필의 실제 source/runtime과 실행 기록, 비교기가 읽은 after 해시 및 번들 내
Wasm 바이트 일치도 직접 assert했습니다. HTTP 엔트리는 desktop CSP, file 엔트리는
브라우저 file origin에서 검사했습니다. 특수 파일 접근 허용 플래그를 추가하지 않았습니다.

최종 빈 줄 2개 편집은 독립 리뷰어가 비의미적 변경으로 확인했습니다. 두 프로필의 전체 게이트를 다시 실행했고, 최종 Wasm 바이트는 위 수치 비교 런타임과 같습니다. 소스 및 빌드 manifest 지문은 재빌드 결과로 갱신했습니다.

현재 문서·설정이 다른 root와 공개본의 지문을 억지로 같게 만들지 않았습니다.
각 프로필을 독립 검토했으며 runtime은 동일합니다. 이 프로필의 source 입력은 135개입니다.

~~~text
source  ccfd8857c63c4914f19a3cbaa88e7cafa9d465b9e5cfdd792a2adbc96fc764d9
runtime b6c26844412b6ce77543a8867db68a568bb6bb55a2c0774dd15cdd69aed45dcb
~~~

실행 근거는 artifacts/portfolio-validation.json, artifacts/spatial-components-20261108/,
artifacts/boids-style-deployment-20261108/ 및 artifacts/github-public-20261108/의 로컬 기록입니다.
공개 저장소에는 검사 로그·원본 과제 자료·SDK를 포함하지 않습니다.

## 온라인 게시와 한계

[온라인 앱](https://pbd-softbody-lab-20261108.true-orbit-7047.chatgpt.site/)과
[실행 ZIP](https://pbd-softbody-lab-20261108.true-orbit-7047.chatgpt.site/pbd-softbody-simulation.zip)의
익명 GET을 확인했습니다. 리뷰어의 현재 app.js 해시는 로컬 번들과 같고, ZIP은 기록한
SHA-256과 일치합니다. Box 전용·장애물2/공1 같은 원격 UI 주장은 조율 담당자의
별도 live QA 범위이며, 리뷰어가 원격 화면 조작을 재실행한 결과로 계산하지 않습니다.
GitHub push와 원격 Actions 성공은 이 판정 시점의 실행 근거에 포함하지 않습니다.

S2의 동등성은 이번 보존 기준선에 대한 결과입니다. C6 이전의 과거 장기 궤적 차이를
해결했다고 주장하지 않으며 기본 장면의 Sphere→Box 변경은 의도한 입력 변화입니다.
새 npm 설치의 moderate 경고6건은 영향·해결 여부가 미평가이므로 P2를4점으로 남깁니다.
공개 Setup이 만드는 PC별 도구 경로는 커밋 대상과 구분합니다.

점수는 R30/30·A25/25·S20/20·B15/15·P9/10으로99/100입니다.
별도 PC·OS·새 SDK 설치, GPU·sanitizer·추가 정적 분석·실제 병렬화·성능 개선 검증·
EXE 재패키징·원격 CI는 실행하지 않았습니다. 앞선 VS Code editor6/debug5 기록은 이번
변경 후 새 검사로 계산하지 않습니다. 최종 봉인은 위 실행과 독립 판단을 대체하지 않습니다.
