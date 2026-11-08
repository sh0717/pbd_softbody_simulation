#include "simulation/ParallelExecutor.h"
#include <algorithm>

#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__EMSCRIPTEN_PTHREADS__)
#include <atomic>

namespace
{
	// 빌드의 PTHREAD_POOL_SIZE=3과 대응하는 모듈 전체 작업자 예산입니다.
	constexpr std::size_t WasmWorkerCapacity = 3;

	// 여러 월드가 같은 모듈의 준비된 Worker를 초과 예약하지 않게 합니다.
	std::atomic<std::size_t> ReservedWasmWorkers{0};

	bool ReserveWasmWorkers(std::size_t WorkerCount) noexcept
	{
		std::size_t Reserved = ReservedWasmWorkers.load();

		for (;;)
		{
			if (WorkerCount > WasmWorkerCapacity - Reserved)
			{
				return false;
			}

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
	if (!ReserveWasmWorkers(WorkerCount))
	{
		return false;
	}
#endif
	Workers.reserve(WorkerCount);

	for (std::size_t Index = 0; Index < WorkerCount; ++Index)
	{
		Workers.emplace_back([this]() noexcept
		{
			WorkerLoop();
		});
	}

	return true;
}

FParallelExecutorState::~FParallelExecutorState()
{
	Stop();
}

void FParallelExecutorState::Stop() noexcept
{
	{
		const std::lock_guard<std::mutex> Lock(Mutex);
		bStopping = true;
	}

	WorkReady.notify_all();

	for (std::thread& Worker : Workers)
	{
		Worker.join();
	}
#if defined(__EMSCRIPTEN_PTHREADS__)
	ReservedWasmWorkers.fetch_sub(Workers.size());
#endif
	Workers.clear();
}

void FParallelExecutorState::Consume() noexcept
{
	for (;;)
	{
		std::size_t Index;
		{
			const std::lock_guard<std::mutex> Lock(Mutex);

			if (NextIndex == Count)
			{
				return;
			}

			Index = NextIndex++;
		}

		(*Task)(Index);
	}
}

void FParallelExecutorState::WorkerLoop() noexcept
{
	std::uint64_t ObservedGeneration = 0;
	std::unique_lock<std::mutex> Lock(Mutex);

	for (;;)
	{
		WorkReady.wait(Lock, [this, &ObservedGeneration]
		{
			return bStopping || Generation != ObservedGeneration;
		});

		if (bStopping)
		{
			return;
		}

		ObservedGeneration = Generation;
		Lock.unlock();
		Consume();
		Lock.lock();

		if (--PendingWorkers == 0)
		{
			WorkFinished.notify_one();
		}
	}
}

void FParallelExecutorState::Run(std::size_t InCount, const std::function<void(std::size_t)>& InTask) noexcept
{
	{
		const std::lock_guard<std::mutex> Lock(Mutex);
		Task = &InTask;
		Count = InCount;
		NextIndex = 0;
		PendingWorkers = Workers.size();
		++Generation;
	}

	WorkReady.notify_all();
	Consume();

	std::unique_lock<std::mutex> Lock(Mutex);
	WorkFinished.wait(Lock, [this]
	{
		return PendingWorkers == 0;
	});
	Task = nullptr;
}

std::size_t FParallelExecutor::DefaultWorkerCount() noexcept
{
	const unsigned int HardwareThreads = std::thread::hardware_concurrency();
	return HardwareThreads > 1 ? std::min<std::size_t>(3, HardwareThreads - 1) : 0;
}
#else
std::size_t FParallelExecutor::DefaultWorkerCount() noexcept
{
	return 0;
}
#endif

FParallelExecutor::FParallelExecutor(std::size_t InWorkerLimit)
	: WorkerLimit(std::min<std::size_t>(3, InWorkerLimit))
{
}

FParallelExecutor::~FParallelExecutor() = default;

void FParallelExecutor::Run(std::size_t Count, const std::function<void(std::size_t)>& Task) noexcept
{
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
	if (Count > 1 && WorkerLimit > 0)
	{
		if (!State)
		{
			State = std::make_unique<FParallelExecutorState>();

			if (!State->StartWorkers(WorkerLimit))
			{
				State.reset();
				WorkerLimit = 0;
			}
		}

		if (State)
		{
			State->Run(Count, Task);

			return;
		}
	}
#endif
	for (std::size_t Index = 0; Index < Count; ++Index)
	{
		Task(Index);
	}
}

std::size_t FParallelExecutor::GetWorkerCount() const noexcept
{
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
	return State ? State->Workers.size() : 0;
#else
	return 0;
#endif
}
