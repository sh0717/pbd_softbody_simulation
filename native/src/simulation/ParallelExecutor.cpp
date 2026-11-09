#include "simulation/ParallelExecutor.h"
#include <algorithm>

// 한 호출의 흐름: 작업 게시 → 호출 스레드도 계산 → 모든 워커 완료 대기 → 빌린 주소 제거입니다.
// 워커는 호출마다 생성하지 않고 다음 게시를 기다립니다. 실행기 파괴 시 종료를 알리고 join합니다.
// 스레드 비활성 빌드는 같은 API로 호출 스레드에서만 실행하며 동기화 상태를 만들지 않습니다.

void FParallelBatch::ExecuteFixedRange(std::size_t ParticipantIndex) const noexcept
{
	// 몫과 나머지로 분리하여 ItemCount가 size_t 최댓값이어도 구간 곱셈이 넘치지 않습니다.
	// 예: 항목 10개, 참여자 3명이면 [0, 3), [3, 6), [6, 10)으로 빈틈·중복 없이 나눕니다.
	const std::size_t ItemsPerParticipant = ItemCount / ParticipantCount;
	const std::size_t Remainder = ItemCount % ParticipantCount;
	const std::size_t FirstIndex = ItemsPerParticipant * ParticipantIndex + Remainder * ParticipantIndex / ParticipantCount;
	const std::size_t EndIndexExclusive = ItemsPerParticipant * (ParticipantIndex + 1) + Remainder * (ParticipantIndex + 1) / ParticipantCount;
	InvokeRangeTask(BorrowedTaskObject, FirstIndex, EndIndexExclusive);
}

#if defined(PARALLEL_EXECUTOR_ENABLE_THREADS) && PARALLEL_EXECUTOR_ENABLE_THREADS
#if defined(__EMSCRIPTEN_PTHREADS__)
#include <atomic>

namespace
{
	// PTHREAD_POOL_SIZE=3에 대응합니다. 같은 모듈의 여러 실행기가 준비된 Worker를 나누어 씁니다.
	constexpr std::size_t WasmWorkerCapacity = 3;

	// 실행기별 개수가 아닌 모듈 전체 예약 수입니다. 워커가 join된 뒤에만 예약을 반환합니다.
	std::atomic<std::size_t> ReservedWasmWorkers{0};

	/** 준비된 pthread 풀의 남은 자리를 한 번에 예약합니다. 부족하면 생성 없이 false를 반환합니다. */
	bool ReserveWasmWorkers(std::size_t WorkerCount) noexcept
	{
		std::size_t Reserved = ReservedWasmWorkers.load();

		for (;;)
		{
			if (WorkerCount > WasmWorkerCapacity - Reserved)
			{
				return false;
			}

			// 다른 실행기가 먼저 예약하거나 허위 실패하면 Reserved가 현재 값으로 갱신되어 재검사합니다.
			if (ReservedWasmWorkers.compare_exchange_weak(Reserved, Reserved + WorkerCount))
			{
				return true;
			}
		}
	}
} // namespace
#endif

bool FParallelExecutorState::StartWorkers(std::size_t WorkerCount) noexcept
{
#if defined(__EMSCRIPTEN_PTHREADS__)
	// 브라우저에 미리 준비한 워커를 초과하여 생성·대기하지 않도록 모듈 예산부터 확보합니다.
	if (!ReserveWasmWorkers(WorkerCount))
	{
		return false;
	}
#endif
	WorkerThreads.reserve(WorkerCount);

	// 0번은 호출 스레드에 남겨 두고, 워커에는 수명 동안 유지할 참여자 번호를 부여합니다.
	for (std::size_t ParticipantIndex = 1; ParticipantIndex <= WorkerCount; ++ParticipantIndex)
	{
		WorkerThreads.emplace_back(&FParallelExecutorState::RunWorkerLoop, this, ParticipantIndex);
	}

	return true;
}

FParallelExecutorState::~FParallelExecutorState()
{
	StopWorkers();
}

void FParallelExecutorState::StopWorkers() noexcept
{
	// 종료도 작업 게시와 같은 mutex로 보호합니다. 잠든 워커는 깨워야 종료 조건을 확인합니다.
	{
		const std::lock_guard<std::mutex> Lock(BatchStateMutex);
		bStopRequested = true;
	}

	BatchPublishedCondition.notify_all();

	// join 중에는 mutex를 잡지 않습니다. 워커가 mutex를 다시 얻어 종료할 수 있어야 합니다.
	for (std::thread& Worker : WorkerThreads)
	{
		Worker.join();
	}
#if defined(__EMSCRIPTEN_PTHREADS__)
	// 실제 스레드가 모두 끝난 뒤 예산을 돌려주어 다른 실행기가 해당 자리를 재사용하게 합니다.
	ReservedWasmWorkers.fetch_sub(WorkerThreads.size());
#endif
	WorkerThreads.clear();
}

void FParallelExecutorState::PublishBatch(const FParallelBatch& Batch) noexcept
{
	{
		const std::lock_guard<std::mutex> Lock(BatchStateMutex);
		ActiveBatch = Batch;
		NextItemIndex = 0;
		// 계산을 배정하지 않은 워커도 이번 세대를 확인해야 이전 호출의 주소를 나중에 읽지 않습니다.
		RemainingWorkerCount = WorkerThreads.size();
		++PublishedBatchSequence;
	}

	// 상태를 먼저 게시하고 잠금을 푼 뒤 깨웁니다. 늦게 대기한 워커도 세대 차이로 작업을 찾습니다.
	BatchPublishedCondition.notify_all();
}

void FParallelExecutorState::WaitForWorkersAndClearBatch() noexcept
{
	std::unique_lock<std::mutex> Lock(BatchStateMutex);
	// wait는 대기 중 잠금을 놓고, predicate 재검사 시 다시 얻습니다. 허위 기상도 여기서 걸러집니다.
	// 각 워커의 계산 후 mutex 해제와 이 잠금 획득을 통해 작업 결과가 호출자에게 보입니다.
	WorkersFinishedCondition.wait(Lock, [this]
	{
		return RemainingWorkerCount == 0;
	});

	// 미배정 워커도 세대를 확인한 뒤 완료하므로 반환 이후 이전 callable을 얻을 수 없습니다.
	ActiveBatch = {};
}

void FParallelExecutorState::ExecuteAssignedWork(const FParallelBatch& Batch, std::size_t ParticipantIndex) noexcept
{
	// 풀을 크게 만든 뒤 작은 작업을 실행하면 남는 워커도 완료 보고만 하고 다음 게시를 기다립니다.
	if (ParticipantIndex >= Batch.ParticipantCount)
	{
		return;
	}

	if (Batch.Schedule == EParallelSchedule::FixedRanges)
	{
		Batch.ExecuteFixedRange(ParticipantIndex);

		return;
	}

	for (;;)
	{
		std::size_t ItemIndex;
		{
			// 인덱스 예약만 직렬화하여 중복 처리를 막습니다. 느린 몸체의 계산으로 잠금을 점유하지 않습니다.
			const std::lock_guard<std::mutex> Lock(BatchStateMutex);

			if (NextItemIndex == Batch.ItemCount)
			{
				return;
			}

			ItemIndex = NextItemIndex++;
		}

		// 계산은 잠금 밖에서 실행합니다. 참여자마다 서로 다른 인덱스의 출력만 수정해야 합니다.
		Batch.InvokeRangeTask(Batch.BorrowedTaskObject, ItemIndex, ItemIndex + 1);
	}
}

void FParallelExecutorState::RunWorkerLoop(std::size_t ParticipantIndex) noexcept
{
	// 시작 전에 게시가 끝났어도 첫 순번과 차이가 생깁니다. notify 수신 여부 대신 상태로 판단합니다.
	std::uint64_t LastObservedBatchSequence = 0;

	for (;;)
	{
		FParallelBatch AssignedBatch;
		{
			std::unique_lock<std::mutex> Lock(BatchStateMutex);
			BatchPublishedCondition.wait(Lock, [this, &LastObservedBatchSequence]
			{
				return bStopRequested || PublishedBatchSequence != LastObservedBatchSequence;
			});

			if (bStopRequested)
			{
				return;
			}

			// 주소와 실행 정보만 지역 복사합니다. 함수 객체·캡처의 소유권은 호출자에게 남아 있습니다.
			LastObservedBatchSequence = PublishedBatchSequence;
			AssignedBatch = ActiveBatch;
		}

		ExecuteAssignedWork(AssignedBatch, ParticipantIndex);
		// 완료 카운터를 내리기 전에 워커의 비소유 주소도 제거합니다.
		AssignedBatch = {};

		{
			const std::lock_guard<std::mutex> Lock(BatchStateMutex);
			// 계산과 주소 사용이 모두 끝난 뒤 완료를 기록합니다. 마지막 워커가 호출자의 대기를 해제합니다.
			--RemainingWorkerCount;

			if (RemainingWorkerCount == 0)
			{
				WorkersFinishedCondition.notify_one();
			}
		}
	}
}

std::size_t FParallelExecutor::DefaultWorkerCount() noexcept
{
	// 호출 스레드도 계산하므로 논리 스레드 하나를 남깁니다. 하드웨어 개수를 모르면 순차로 실행합니다.
	const unsigned int HardwareThreadCount = std::thread::hardware_concurrency();
	return HardwareThreadCount > 1 ? std::min<std::size_t>(3, HardwareThreadCount - 1) : 0;
}
#else
std::size_t FParallelExecutor::DefaultWorkerCount() noexcept
{
	return 0;
}
#endif

FParallelExecutor::FParallelExecutor(std::size_t InWorkerLimit) noexcept
	: WorkerLimit(std::min<std::size_t>(3, InWorkerLimit))
{
}

FParallelExecutor::~FParallelExecutor() = default;

void FParallelExecutor::ExecuteBatchAndWait(FParallelBatch Batch) noexcept
{
	LastExecutionWorkerCount = 0;

	if (Batch.ItemCount == 0)
	{
		// 빈 호출은 callable을 실행하거나 워커 풀을 생성하지 않습니다.
		return;
	}

#if defined(PARALLEL_EXECUTOR_ENABLE_THREADS) && PARALLEL_EXECUTOR_ENABLE_THREADS
	if (Batch.ItemCount > 1 && WorkerLimit > 0)
	{
		if (!State)
		{
			// 풀은 첫 병렬 호출에서만 만들고 다음 호출에도 재사용합니다.
			State = std::make_unique<FParallelExecutorState>();

			if (!State->StartWorkers(WorkerLimit))
			{
				// Wasm 예산이 부족한 실행기는 이후 호출도 순차로 유지합니다. 새 실행기는 다시 예약할 수 있습니다.
				State.reset();
				WorkerLimit = 0;
			}
		}

		if (State)
		{
			// 항목 수보다 참여자를 많이 배정하지 않습니다. 풀의 미배정 워커도 게시 확인·완료는 수행합니다.
			Batch.ParticipantCount = std::min(Batch.ItemCount, State->WorkerThreads.size() + 1);
			State->PublishBatch(Batch);
			// 호출 스레드도 0번 참여자로 계산한 뒤 합류합니다. 반환 시에는 빌린 작업을 쓰는 워커가 없습니다.
			State->ExecuteAssignedWork(Batch, 0);
			State->WaitForWorkersAndClearBatch();
			LastExecutionWorkerCount = Batch.ParticipantCount - 1;

			return;
		}
	}
#endif
	// 단일 항목·워커 0개·스레드 비활성 빌드·Wasm 예약 실패는 전체 구간을 호출 스레드에서 처리합니다.
	Batch.ExecuteFixedRange(0);
}

std::size_t FParallelExecutor::GetWorkerCount() const noexcept
{
#if defined(PARALLEL_EXECUTOR_ENABLE_THREADS) && PARALLEL_EXECUTOR_ENABLE_THREADS
	return State ? State->WorkerThreads.size() : 0;
#else
	return 0;
#endif
}

std::size_t FParallelExecutor::GetLastExecutionWorkerCount() const noexcept
{
	return LastExecutionWorkerCount;
}
