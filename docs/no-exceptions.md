# C++ 예외 미사용 계약

C++ 코어·바인딩·네이티브 검사는 `throw`, `try`, `catch`를 사용하지 않습니다.
MSVC는 `/EHs-c-`와 `_HAS_EXCEPTIONS=0`, Emscripten은 `-fno-exceptions`와
`DISABLE_EXCEPTION_CATCHING=1`로 빌드합니다. JavaScript 오류 처리는 유지합니다.

인접 Boids의 `core/BuildPolicy.h`, CMake의 PUBLIC 컴파일 옵션과
`tools/check-cpp-standard.mjs`를 참조하여 같은 정책을 적용합니다.
공통 헤더는 예외 활성화 매크로 또는 잘못된 MSVC STL 설정을 발견하면 컴파일을
중단하고, 구조 검사는 C++ 코드의 예외 키워드와 SEH 구문을 거부합니다.

## 입력 검증과 상태 반환

`FOperationResult`는 `EErrorCode`와 오류 메시지를 소유합니다. `None`이면 성공입니다.
설정·입자 명령·접촉 준비처럼 외부 입력을 검증하는 경계에서만 결과를 확인합니다.
단순 유효성 판정은 bool, 검증된 내부 계산은 void를 사용합니다.

```cpp
const FOperationResult Status = SoftContactSolver.Prepare(SoftContactParticipants);
if (!Status)
{
    return Status;
}
```

`Step`은 기존 bool 결과와 선택적 `FOperationResult* OutResult`를 함께 사용합니다.
`dt=0`은 false와 성공 상태, 잘못된 시간 간격은 false와 실패 상태입니다.
검증 실패는 위치 예측 전에 반환하며 입자·승수·시간·진단을 보존합니다.
실행 중 잘못된 토폴로지가 발견된 경우 앞선 위치 변경의 전체 롤백은 제공하지 않습니다.

생성자는 `GetInitializationResult()`로 검증 결과를 제공합니다. 실패한 객체는
상태 조회와 파괴만 허용합니다. 월드와 owner 등록 경계는 실패한 객체를 등록하지 않으며,
`AddSoft`·`AddBall`은 nullptr와 실패 상태를 반환합니다.
`Prepare`의 널 입력 검사는 캐시 변경 전에 전체 목록을 검사합니다.

`FConstraintSolver::SolveEdges/SolveVolumes`, `FSoftContactSolver::Generate`와
`FCollisionSolver::SolveSoftPairs`는 void입니다. 월드 Step과 직접 투영 바인딩에서
시간·유연도를 검증하고, 접촉 세션은 두 몸체의 준비 여부를 검사한 뒤 내부 계산을 호출합니다.
제약 작업별 결과 배열은 두지 않습니다. 새 공간 캐시의 토폴로지 검증과 후보 등록은
공개 Prepare 안에서 완료하므로 내부 준비 함수 사이의 결과 전달도 제거했습니다.

`AdvanceSubstep`, `SolveIteration`, `SolveDynamic`은 접촉 준비에서 발견되는 잘못된
토폴로지를 호출자에게 전달해야 하므로 작업 결과를 유지합니다. 이 오류는 실제로
발생할 수 있는 입력 오류이며 내부 계약 위반으로 바꾸지 않습니다.

## Wasm과 JavaScript

바인딩은 실패 상태를 저장한 뒤 정상 반환하여 C++ 지역 객체를 RAII로 정리합니다.
`takeError()`는 가장 최근 호출의 오류 객체 또는 null을 반환하고 오류를 소비합니다.
각 바인딩 호출 시작 시 이전 상태를 초기화합니다. 바인딩 호출은 직렬화합니다.

`physics.js`는 호출 반환 후 상태를 확인하여 `InvalidArgument`는 `TypeError`,
`OutOfRange`는 `RangeError`, 나머지는 `Error`로 변환합니다.
raw Wasm export의 직접 호출자도 각 호출 후 상태를 확인해야 합니다.
소유권 토큰·몸체 관찰 주소·정상 반환값·입자 배열의 배치는 유지합니다.

## 자원 소진과 내부 전제조건

STL 컨테이너 할당, `std::thread` 생성, 동기화 자원 소진은 복구 가능한 입력 오류로
취급하지 않습니다. 예외 비활성화 빌드에서 자원이 소진되면 프로세스 또는 Wasm 실행을
종료합니다. Wasm의 준비된 Worker 예산 부족은 작업 발행 전에 확인하여 순차 실행합니다.
제약 작업은 검증한 입력으로 계산하며 정상 실행 결과만 몸체 배열에 기록합니다.

유효한 생성 결과·몸체 수명·등록된 토폴로지는 호출 전제조건입니다. 내부의 검증된
접촉 범위 조회는 assert와 범위가 확인된 인덱싱을 사용합니다.
제약 커널의 시간·유연도 계약 위반과 준비되지 않은 몸체의 내부 Generate 호출은
입자·접촉 출력 수정 전에 진단을 남기고 std::abort로 종료합니다. 이 검사는 NDEBUG와
관계없이 유지합니다. JS를 통한 같은 입력 오류는 경계 검증으로 먼저 거부합니다.
`tests/native/test_no_exceptions.cpp`는 예외 활성화 매크로가 있으면 컴파일을 거부하고,
실패 상태·기존 상태 보존·널 입력·소유권 토큰 소진과 자식 프로세스의 계약 위반 종료를 검사합니다.
