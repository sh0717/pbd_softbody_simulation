# Epic C++ 코딩 표준의 PBD 적용 규칙

2026-11-07에 사용자 지정 [Epic 공식 한국어 C++ 코딩 표준](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine?lang=ko)을 직접 확인하여 PBD 적용 규칙을 갱신했습니다.
명명·클래스 체계·const 정확도·auto·물리적 종속성·캡슐화·주석·서식은 공식 문서를 우선합니다.
기존 2026-11-04 표준은 로컬 UE **5.8.4**, changelist **58902052** 소스를 함께 참조했습니다.
기존 class C·공백 2칸 규칙은 이 문서로 대체합니다. 과거 감사·성능·이관 문서는 당시 기록으로 보존합니다.

## 공식 규칙과 독립 프로젝트 예외

| 적용 구분 | PBD 규칙 |
| --- | --- |
| 공식 명명 | 일반 타입 F, 클래스 템플릿 T, 열거형 E, bool b, PascalCase, 의미 있는 이름과 변수별 선언 |
| 공식 구조·수명 | public 우선, 내부 상태 private, const 조회, 이동할 소유 컨테이너의 값 인자는 const 금지 |
| 공식 서식·주석 | 탭 폭 4, Allman, 의도·단위·범위·수명 설명, 공개 선언에서 호출 계약 문서화 |
| 공식 타입·include | auto 제한, 구조적 바인딩 지양, #pragma once, 직접 사용하는 헤더 include, 전역 using 금지 |
| 독립 프로젝트 예외 | std::vector/array/unique_ptr와 std 고정폭 정수를 유지합니다. Unreal 컨테이너·UObject·UHT·TEXT 의존성을 추가하지 않습니다. |
| 이름·언어 예외 | 프로젝트 타입은 전역에 선언합니다. Embind export·JS 필드·호환 API 이름을 보존하며 설명과 주석은 한글로 작성합니다. |
| 프로젝트 서식 선택 | 익명 namespace 들여쓰기 4, 선언·계산·인자의 한 줄 배치와 멤버 사이 빈 줄은 지역 규칙입니다. Epic 규칙 전체의 필수 사항으로 설명하지 않습니다. |

Epic의 UE 내부 표준은 일반 컨테이너에 엔진 타입을 사용하고 코드·주석에 미국 영어를 요구합니다.
위 예외를 포함한 독립 C++20/Wasm 적용이며, 엔진 내부 표준을 예외 없이 준수한다는 의미는 아닙니다.
Epic 저작권 고지는 Epic이 제공한 소스의 규칙이므로 자체 소스에 복사하지 않습니다.

## 1. 참조한 엔진 소스

아래 경로는 참조한 UE 5.8.4의 `Engine/` 기준입니다.
이 프로젝트를 빌드할 때 Unreal Engine 설치를 요구하지 않습니다.

| 소스 경로·기호 | 확인한 규칙 |
| --- | --- |
| `Build/Build.version` | 5.8.4와 changelist 확인 |
| `Source/Runtime/Core/Public/Math/RandomStream.h` · `FRandomStream` | F 일반 타입, PascalCase, public 인터페이스, 탭·Allman |
| `Source/Runtime/Core/Public/Misc/FeedbackContext.h` · `FFeedbackContext` | 일반 class도 F, 접근제어자 배치 |
| `Source/Runtime/Core/Public/Misc/ScopeExit.h` · `TScopeGuard` | T 클래스 템플릿, 명시적 소유·복사 제한, namespace 들여쓰기 |
| `Source/Runtime/Core/Public/Templates/UniquePtr.h` · `TDefaultDelete` | T struct 템플릿과 소유권 설명 |
| `Source/Runtime/CorePreciseFP/.clang-format` | 탭 폭 4, Allman 계열, 왼쪽 포인터 정렬, 생성자 쉼표 배치 참고 |

엔진 하위 모듈에는 서로 다른 formatter 설정과 오래된 코드가 공존합니다. 특정 파일을
완전 복제하지 않고 공식 규칙을 우선하며 아래의 프로젝트 선택을 명시합니다.
Epic 저작권 문구나 엔진 구현 코드를 프로젝트 소유 코드에 복사하지 않습니다.

## 2. 타입·함수·변수 명명

| 대상 | 규칙 | 예 |
| --- | --- | --- |
| 일반 class·struct | F 접두사 | `FSimulation`, `FContact` |
| class·struct 템플릿 | T 접두사 | `TOwnerRegistry<ObjectTypes...>` |
| enum class | E 접두사 | `EBoundaryMode` |
| 함수·멤버·인자·지역 변수·일반 상수 | PascalCase | `AdvanceTime`, `ParticleCount`, `MaxParticles` |
| bool 변수·멤버·인자 | b 접두사 | `bEnabled`, `bHasContact` |
| 출력·입출력 인자 | Out·InOut으로 역할 표시 | `OutContacts`, `InOutPosition` |
| 템플릿 타입 인자 | 의미 있는 이름과 Type/Types 접미사를 권장 | `ObjectType`, `CallableType`, `ObjectTypes` |
| 파일 | 타입 접두사를 뺀 PascalCase `.h/.cpp` | `Simulation.h`, `Simulation.cpp` |

함수는 수행 동작이나 반환 의미를 드러냅니다. bool 반환 함수는 `Is`·`Has`·`Can` 등
호출 지점에서 의미가 읽히는 이름을 우선합니다. 클래스라는 이유만으로 C를 붙이지 않습니다.
비템플릿 별칭은 대상 타입의 성격을 따르고 의존 타입 별칭은 의미 있는 `Type` 이름을 사용합니다.
짧고 의미가 명확한 템플릿의 기존 `T` 인자는 허용합니다. 엔진의 `TDefaultDelete<T>`도
같은 예이며 Type 접미사는 모든 템플릿 인자를 일괄 개명하라는 요구가 아닙니다.

## 3. 서식과 파일 구성

- UTF-8/LF, Allman 중괄호, 탭 폭 4를 사용합니다. 들여쓰기와 continuation은 탭 1개입니다.
  탭 뒤의 세부 정렬에 필요한 공백은 허용합니다. 포인터·참조는 `FBody*`, `const FBody&`로 붙입니다.
- 공개 인터페이스를 먼저 읽을 수 있게 public을 앞에 두고 구현 상세는 private에 둡니다.
  접근제어자는 class와 같은 깊이, 멤버는 탭 1개 깊이에 둡니다. 필요한 선행 타입 선언은 허용합니다.
- 프로젝트 타입은 전역에 선언하며 구현 전용 함수·상수는 익명 네임스페이스에 둡니다.
  함수 선언·정의·호출 인자와 변수 선언·대입·계산·조건식은
  한 줄에 둡니다. `boid_simulation`과 같이 `ColumnLimit: 0`으로 열 제한에 따른 분할을 방지합니다.
  함수·제어문·람다의 중괄호는 Allman을 유지하며, 함수 사이와 헤더의 각 멤버 사이에는 빈 줄 하나를 둡니다.
  이 배치는 Epic 전체의 강제 규칙이 아닌 이 프로젝트의 선택입니다.
- 함수 내부는 초기화·입력 검증·반복 계산·결과 반영·정리처럼 역할이 바뀌는 지점에서 빈 줄 하나로 구분합니다.
  같은 객체의 초기화, 한 계산의 연속 단계, 오류 보고와 반환은 의미가 이어지도록 함께 묶습니다.
  주석은 설명하는 코드에 붙이며, 줄 수나 문장 종류만으로 모든 문장을 나누지 않습니다.
- 헤더에는 `#pragma once`를 사용합니다. 구현 파일은 대응 헤더를 먼저 포함하고 실제 필요한
  헤더를 직접 포함합니다. 대응 헤더가 없는 binding·entrypoint·test는 역할에 맞게 구성합니다.
- 변수 선언 하나에 이름 하나를 쓰고 긴 식은 역할이 드러나는 지역 변수로 설명합니다.
  무의미한 getter·상속·가상함수·manager를 추가하지 않습니다.
- 삼항 연산자는 짧은 값 선택에 사용합니다. 긴 계산식이나 인자가 많은 함수 호출을 선택할 때는 `if/else`로 작성합니다.
- 주석은 책임, 단위, 범위, 소유권·대여 수명과 실패 계약을 설명합니다.
  기술 설명과 주석은 한글로 작성하며 코드 식별자와 기술 명칭은 원문을 유지합니다.
  함수 계약은 선언 앞에 `/** ... */`로 작성하고 필요한 `@param`·`@return`·`@pre`·`@note`를 사용합니다.
  짧은 계약은 `/** 설명입니다. */`, 타입의 책임과 구현상의 의도는 `//`로 설명합니다.
  멤버 변수는 선언 바로 앞의 `//`로 저장하는 값·단위·인덱스 대응·초기화 시점이나 수명을 설명합니다.
  예를 들어 `TetIndices`는 사면체당 정점 네 개, `VolumeLambdas[t]`는 같은 사면체의 누적 승수임을 명시합니다.
  주요 계산 변수는 수식에서의 역할과 후속 사용을 설명하되 자명한 축·반복 인덱스에는 같은 주석을 반복하지 않습니다.
  구현을 그대로 읽어 주는 주석이나 선언과 중복되는 설명은 추가하지 않습니다.
- 람다는 필요한 변수를 명시적으로 캡처합니다. 오래된 테스트의 짧은 검증 람다는
  기존 범위를 유지하며, 제품 코드의 새 람다에 암묵적 전체 캡처를 추가하지 않습니다.
- 전역의 `using namespace`와 외부 이름을 가져오는 `using` 선언은 금지합니다.
  프로젝트 타입을 정의하는 `using FIndices = ...` 형식의 별칭은 허용합니다.

## 4. 타입·수명·API 계약

- 명시적 타입을 기본으로 합니다. auto는 이름으로 표현할 수 없는 람다,
  가독성을 해치는 긴 iterator 타입, 의존 템플릿 식에 한정합니다.
  타입이 이미 보이는 `make_unique` 결과나 일반 값·참조도 여기서는 명시합니다.
- 읽기 전용 접근은 const 메서드와 const 참조를 사용합니다. 값 반환에 최상위 const를 붙이지 않습니다.
  이동할 값·변경될 값에 관성적으로 const를 붙이지 않습니다.
- 내부 가변 상태는 private으로 소유하고 명령과 조회를 분리합니다. 값 DTO는 공개 필드를 허용합니다.
- 단독 소유는 `std::unique_ptr`, 값 저장은 STL 컨테이너와 RAII를 사용합니다.
  raw pointer는 명시된 비소유·ABI 대여에 한정합니다. 종료·재할당·메모리 성장에 따른
  무효화 조건을 API에 남깁니다.
- `nullptr`, `override`, scoped enum과 `static_assert`를 사용합니다.
  숫자 폭이 중요한 경계는 기존 표준 고정폭 정수와 레이아웃을 유지합니다.

## 5. 독립 C++/Wasm 프로젝트의 적용 경계

이 프로젝트는 Unreal 모듈이 아닙니다. 엔진의 명명·서식·인터페이스 구성 규칙을
채택하되 C++20, STL과 `std::` 타입을 사용하며 프로젝트 타입은 전역에 선언합니다.
엔진 의존성, UHT·리플렉션 매크로, `UObject`, `TArray`, `FString`, UE 정수 별칭은 추가하지 않습니다.
매크로는 기존 프로젝트·외부 라이브러리 이름을 유지하며 UE 매크로인 것처럼 이름 붙이지 않습니다.

- Emscripten 등록 문자열, JS API·JSON 키, WASM export, enum 수치와 버퍼 레이아웃은 유지합니다.
- 기존 `Step`, `Prepare`, `Project` 등 성공 여부를 반환하는 호환 API는 호출자 계약을 보존합니다.
- 표준 라이브러리 프로토콜 `value`, `type`, `operator()` 등 요구되는 이름은 유지합니다.
- entrypoint `main.cpp`, 기존 native test 파일명, 외부 ABI 헤더명은 유지합니다.
- 생성 Wasm·번들, third-party, fixture, 과거 baseline·감사 문서는 일괄 치환하지 않습니다.
  생성물은 기존 빌드 명령으로만 갱신합니다. JS·Python의 언어별 서식도 유지합니다.

## 6. 적용·검사

`.clang-format`, `.editorconfig`의 C++ 섹션, VS Code `[cpp]` 설정을 함께 관리합니다.
`npm run format`으로 적용하고 `npm run check:format`으로 검사합니다.
`npm run check:structure`는 `tools/check-cpp-standard.mjs`를 통해 C++ 타입·bool 명명,
헤더의 `#pragma once`, 전역 using 금지, 구조적 바인딩 제한도 검사합니다.
이 검사는 어휘 기반의 빠른 검사이며 완전한 C++ 의미 분석은 아닙니다.
auto 예외의 적절성, const·소유권·책임 경계는 코드 리뷰에서 확인합니다.

C++을 변경하면 `npm run verify`로 Wasm 재생성, Node 회귀, native Debug/Release,
브라우저를 검사합니다. 명명 변경도 외부 문자열·ABI 토큰과 계산 순서를 별도로 대조합니다.
이전 품질 점수·리뷰 지문은 현재 소스 검증으로 자동 승계하지 않습니다.

## 7. Boids와 통일한 값 타입·접근 방식

2026-11-08 사용자 요청에 따라 인접 `boid_simulation`의 `.clang-format`, `Axis.h`,
`Vector3D.h`와 시뮬레이션·충돌 헤더를 확인하여 스타일을 통일했습니다.

- `FVector3D`는 배열 별칭 대신 `X/Y/Z`를 소유하는 값 타입입니다. `EAxis`와 `AllAxes`로
  축을 구분하고 `Dot`, `Cross`, `Length` 등의 연산은 벡터 메서드로 표현합니다.
- PBD의 길이는 기존 `sqrt(X*X + Y*Y + Z*Z)` 계산을 보존합니다. Boids의 `std::hypot`
  구현을 그대로 복사하여 반올림·오버플로 동작을 바꾸지 않습니다.
- 입자 저장소는 기존의 연속한 `double` XYZ 배열입니다. `ReadParticleVector`는 값 복사,
  `AddParticleVector`는 해당 입자에 대한 누적이며 저장소를 재할당하지 않습니다.
- 무게중심 좌표는 공간 축과 구분하여 `std::array<double, 3>`로 저장합니다.
- Wasm 벡터 뷰는 standard-layout, 크기와 필드 오프셋을 검증한 바이트 배치에 한정합니다.
  C++에서 `&X`를 배열처럼 순회하지 않습니다. 기존 JS 뷰·수명·메모리 성장 계약을 유지합니다.
- `FConstraintSolver`는 상태 없는 정적 커널이며 `friend`로 입자·승수 수정 권한을 모읍니다.
  객체 생성이나 엔진 의존성을 추가하지 않습니다.

현재 소스의 빌드·테스트는 `npm run verify`로 실행합니다.
