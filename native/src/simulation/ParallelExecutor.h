#pragma once

#include "core/BuildPolicy.h"
#include <concepts>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#if defined(PARALLEL_EXECUTOR_ENABLE_THREADS) && PARALLEL_EXECUTOR_ENABLE_THREADS
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
#endif

/**
 * const 객체를 빌려 직접 호출하며 예외 없이 void를 반환하는 작업입니다.
 * @note 함수 자체는 void*로 빌릴 수 없으므로 함수 포인터 객체나 람다로 전달합니다. 복사 가능성은 요구하지 않습니다.
 * @note 인자는 실제 호출 표현식의 변환 규칙을 따릅니다. 캡처 수명과 공유 데이터 동기화는 호출자 책임입니다.
 */
template <typename TaskType, typename... IndexTypes>
concept CParallelTask = std::is_object_v<TaskType> && requires(const TaskType& Task) {
	{ static_cast<const void*>(std::addressof(Task)) } -> std::same_as<const void*>;
	{ Task(std::declval<IndexTypes>()...) } noexcept -> std::same_as<void>;
};

/** 고정 연속 구간과 비용이 다른 독립 인덱스의 배분 정책입니다. */
enum class EParallelSchedule
{
	// 참여자마다 연속 구간 하나를 맡습니다. 항목별 비용이 비슷한 Boids 갱신에 사용합니다.
	FixedRanges,
	// 먼저 끝난 참여자가 다음 인덱스를 가져갑니다. 비용 편차가 큰 작업에서 명시적으로 선택합니다.
	DynamicIndices
};

/** 한 동기 호출의 작업을 빌립니다. 함수 객체와 캡처는 완료 대기까지 호출자가 소유합니다. */
struct FParallelBatch
{
	// 호출자가 소유한 const 함수 객체 주소입니다. 복사하거나 해제하지 않습니다.
	const void* BorrowedTaskObject = nullptr;

	// 원래 함수 객체 타입으로 복원하고 [FirstIndex, EndIndexExclusive)를 실행합니다.
	void (*InvokeRangeTask)(const void* BorrowedTaskObject, std::size_t FirstIndex, std::size_t EndIndexExclusive) noexcept = nullptr;

	// 처리할 인덱스의 배타적인 끝입니다. 인덱스는 [0, ItemCount)입니다.
	std::size_t ItemCount = 0;

	// 호출 스레드를 포함한 구간 수입니다. 고정 구간은 참여자당 한 구간입니다.
	std::size_t ParticipantCount = 1;

	// 고정 구간 또는 작업을 마친 참여자가 다음 인덱스를 가져가는 정책입니다.
	EParallelSchedule Schedule = EParallelSchedule::FixedRanges;

	/**
	 * 곱셈 오버플로 없이 floor(ItemCount * ParticipantIndex / ParticipantCount) 구간을 실행합니다.
	 * @pre 내부 실행기만 호출하며 1 <= ParticipantCount <= 4, ParticipantIndex < ParticipantCount입니다.
	 */
	void ExecuteFixedRange(std::size_t ParticipantIndex) const noexcept;
};

/** 실행기가 단독 소유하는 지속 워커와 동기화 상태입니다. 계산 중 잠금을 유지하지 않습니다. */
struct FParallelExecutorState
{
#if defined(PARALLEL_EXECUTOR_ENABLE_THREADS) && PARALLEL_EXECUTOR_ENABLE_THREADS
	// 첫 병렬 호출에서 생성하고 종료 요청 후 join하는 백그라운드 스레드입니다.
	std::vector<std::thread> WorkerThreads;

	// 작업 게시·동적 인덱스 배분·세대·완료 수·종료 상태를 함께 보호합니다.
	std::mutex BatchStateMutex;

	// 새 세대 또는 종료 요청을 알립니다. predicate와 함께 사용합니다.
	std::condition_variable BatchPublishedCondition;

	// 모든 생성 워커가 해당 세대의 작업 참조를 끝냈음을 호출자에게 알립니다.
	std::condition_variable WorkersFinishedCondition;

	// 잠금 아래 게시·복사·초기화합니다. 함수 객체 자체는 소유하지 않습니다.
	FParallelBatch ActiveBatch;

	// DynamicIndices에서 아직 배분하지 않은 첫 인덱스입니다. mutex 아래에서 증가합니다.
	std::size_t NextItemIndex = 0;

	// 현재 세대를 확인하고 완료해야 하는 생성 워커 수입니다. 호출 스레드는 제외합니다.
	std::size_t RemainingWorkerCount = 0;

	// 늦은 시작과 허위 기상에서 작업을 잃거나 중복 처리하지 않게 하는 게시 순번입니다.
	std::uint64_t PublishedBatchSequence = 0;

	// 소유 스레드가 mutex 아래 게시하고 모든 워커를 깨워 종료합니다.
	bool bStopRequested = false;

	/** pthread Wasm의 모듈 예산 부족은 false입니다. 할당·스레드 생성 실패는 종료 정책을 따릅니다. */
	bool StartWorkers(std::size_t WorkerCount) noexcept;

	/** 종료 상태 게시 → 깨우기 → join 순서로 작업자와 Wasm 예약을 반환합니다. */
	~FParallelExecutorState();

	/** 실행 중인 호출이 없을 때 소유 스레드에서 호출합니다. 반복 호출할 수 있습니다. */
	void StopWorkers() noexcept;

	/** 호출 정보를 게시하고 모든 생성 워커를 깨웁니다. 소유 스레드만 호출합니다. */
	void PublishBatch(const FParallelBatch& Batch) noexcept;

	/** 모든 워커의 작업 참조가 끝난 뒤 빌린 함수와 캡처 주소를 지웁니다. */
	void WaitForWorkersAndClearBatch() noexcept;

	/** 구간 또는 배타적으로 가져온 인덱스를 잠금 밖에서 실행합니다. 0번은 호출 스레드입니다. */
	void ExecuteAssignedWork(const FParallelBatch& Batch, std::size_t ParticipantIndex) noexcept;

	/** 세대별 작업을 실행·완료하고 다음 작업 또는 종료 요청을 기다립니다. */
	void RunWorkerLoop(std::size_t ParticipantIndex) noexcept;
#endif
};

/**
 * 독립 작업을 재사용 워커와 호출 스레드에서 실행하고 모든 쓰기가 보이는 상태로 반환합니다.
 * 호출자는 작업을 게시한 뒤 0번 참여자로 계산하고, 모든 워커의 완료를 확인한 뒤 반환합니다.
 * @pre 실행·조회·파괴는 소유 스레드가 직렬화합니다. 같은 실행기의 재진입·동시 실행·실행 중 파괴는 금지합니다.
 * @note 작업은 자기 저장소만 수정하며 예외를 사용하지 않습니다. 자원 소진은 종료 정책을 따릅니다.
 * @note 같은 함수 객체를 여러 스레드가 함께 호출합니다. 공유 캡처의 변경은 호출자가 동기화해야 합니다.
 */
class FParallelExecutor
{
public:
	/** 기본 백그라운드 워커 수입니다. min(3, hardware_concurrency - 1), 순차 빌드는 0입니다. */
	static std::size_t DefaultWorkerCount() noexcept;

	/** 워커 상한을 3으로 제한합니다. 0은 순차이며 첫 병렬 호출까지 스레드를 만들지 않습니다. */
	explicit FParallelExecutor(std::size_t WorkerLimit = DefaultWorkerCount()) noexcept;

	/** 모든 워커가 종료·join한 뒤 상태를 해제합니다. 빌린 입력 저장소보다 먼저 파괴하십시오. */
	~FParallelExecutor();

	FParallelExecutor(const FParallelExecutor&) = delete;

	FParallelExecutor& operator=(const FParallelExecutor&) = delete;

	FParallelExecutor(FParallelExecutor&&) = delete;

	FParallelExecutor& operator=(FParallelExecutor&&) = delete;

	/**
	 * [0, Count)를 분할하고 모든 작업이 끝날 때까지 함수 객체를 빌립니다. 함수 객체는 복사·할당하지 않습니다.
	 * @note 풀 저장소와 스레드는 최초 병렬 호출에서 지연 생성합니다.
	 * @param RangeTask const 호출 가능하고 void를 반환하며 noexcept로 선언된 함수 객체입니다. (FirstIndex, EndIndexExclusive)를 처리합니다.
	 * @param Schedule 고정 연속 구간 또는 한 인덱스씩 동적 배분합니다. Count=0이면 호출하지 않습니다.
	 * @note 함수 객체·캡처·입력 저장소를 반환까지 유지합니다. 작업은 예외를 던지지 않아야 합니다.
	 */
	template <typename RangeTaskType>
		requires CParallelTask<RangeTaskType, std::size_t&, std::size_t&>
	void ExecuteRangesAndWait(std::size_t Count, const RangeTaskType& RangeTask, EParallelSchedule Schedule = EParallelSchedule::FixedRanges) noexcept
	{
		// 캡처 없는 람다는 함수 포인터가 됩니다. void*로 빌린 객체를 원래 타입으로 복원해 호출합니다.
		// 함수 객체를 복사하지 않으므로 복사 불가능한 callable도 같은 게시 구조를 사용할 수 있습니다.
		const auto InvokeRangeTask = [](const void* BorrowedTaskObject, std::size_t FirstIndex, std::size_t EndIndexExclusive) noexcept
		{
			const RangeTaskType& OriginalTask = *static_cast<const RangeTaskType*>(BorrowedTaskObject);
			OriginalTask(FirstIndex, EndIndexExclusive);
		};

		FParallelBatch Batch;
		Batch.BorrowedTaskObject = std::addressof(RangeTask);
		Batch.InvokeRangeTask = InvokeRangeTask;
		Batch.ItemCount = Count;
		Batch.Schedule = Schedule;
		ExecuteBatchAndWait(Batch);
	}

	/** [0, Count)의 독립 인덱스를 고정 연속 구간으로 배분합니다. 작업은 const 호출 가능하고 void 반환·noexcept여야 합니다. */
	template <typename IndexTaskType>
		requires CParallelTask<IndexTaskType, std::size_t&>
	void Run(std::size_t Count, const IndexTaskType& IndexTask) noexcept
	{
		// 인덱스 작업을 구간 작업으로 감쌉니다. 아래 호출이 동기 완료하므로 참조 캡처가 유효합니다.
		// 순차 경로에서는 전체 구간을 한 번에 받으므로 구간 안의 모든 인덱스를 순회합니다.
		const auto RangeTask = [&IndexTask](std::size_t FirstIndex, std::size_t EndIndexExclusive) noexcept
		{
			for (std::size_t Index = FirstIndex; Index < EndIndexExclusive; ++Index)
			{
				IndexTask(Index);
			}
		};

		ExecuteRangesAndWait(Count, RangeTask, EParallelSchedule::FixedRanges);
	}

	/** 생성되어 재사용 중인 백그라운드 워커 수입니다. 실행 중에는 조회하지 않습니다. */
	std::size_t GetWorkerCount() const noexcept;

	/**
	 * 최근 완료한 작업에 참여하도록 배정한 워커 수입니다. 미배정 워커와 호출 스레드는 제외합니다.
	 * @note 동적 배분에서는 배정된 워커도 인덱스를 얻지 못할 수 있으므로 실제 계산한 워커 수와 다릅니다.
	 */
	std::size_t GetLastExecutionWorkerCount() const noexcept;

private:
	/** 빈 작업·순차·병렬을 선택하고 호출 스레드 참여 뒤 완료를 기다립니다. */
	void ExecuteBatchAndWait(FParallelBatch Batch) noexcept;

	// 소유 스레드만 변경합니다. pthread Wasm 예산 예약 실패 시 0으로 전환합니다.
	std::size_t WorkerLimit;

	// 마지막 호출에서 작업을 배정한 워커 수입니다. 생성된 풀의 크기와 구분합니다.
	std::size_t LastExecutionWorkerCount = 0;

	// 지속 워커·동기화·빌린 작업을 단독 소유합니다. 순차 실행에서는 할당하지 않습니다.
	std::unique_ptr<FParallelExecutorState> State;
};
