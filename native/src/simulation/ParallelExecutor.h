#pragma once
#include "core/BuildPolicy.h"
#include <cstddef>
#include <functional>
#include <memory>
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
#endif

/** 실행기가 단독 소유하는 작업자와 동기화 상태입니다. 실행 중 외부에서 수정하지 않습니다. */
struct FParallelExecutorState
{
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
	// 생성부터 Stop의 join까지 살아 있는 백그라운드 스레드입니다.
	std::vector<std::thread> Workers;

	// 작업 발행, 인덱스 배분과 완료를 보호합니다.
	std::mutex Mutex;

	// 새 작업 세대 또는 종료 요청을 작업자에게 알립니다.
	std::condition_variable WorkReady;

	// 마지막 작업자의 완료를 호출 스레드에 알립니다.
	std::condition_variable WorkFinished;

	// Run의 호출 스택에 있는 함수 객체를 완료 대기 동안만 빌립니다.
	const std::function<void(std::size_t)>* Task = nullptr;

	// 현재 작업의 배타적인 끝 인덱스입니다.
	std::size_t Count = 0;

	// mutex 아래에서 증가시키는 아직 배분하지 않은 첫 인덱스입니다.
	std::size_t NextIndex = 0;

	// 현재 세대를 아직 완료하지 않은 백그라운드 작업자 수입니다.
	std::size_t PendingWorkers = 0;

	// 깨우기 전에 증가시켜 늦게 시작한 작업자도 현재 작업을 인식하게 합니다.
	std::uint64_t Generation = 0;

	// 파괴 또는 생성 실패 시 Mutex 아래에서 발행하는 종료 요청입니다.
	bool bStopping = false;

	/** Wasm Worker 예산이 부족하면 false입니다. std::thread 생성·할당 실패는 종료 정책을 따릅니다. */
	bool StartWorkers(std::size_t WorkerCount) noexcept;

	~FParallelExecutorState();

	/** 아직 작업을 발행하지 않았거나 Run 완료 후 호출하며 모든 작업자를 join합니다. */
	void Stop() noexcept;

	/** 각 인덱스를 배타적으로 배분하고 물리 계산은 잠금 밖에서 수행합니다. */
	void Consume() noexcept;

	/** 새 세대의 작업을 처리한 뒤 완료 수를 갱신하고 다음 세대를 기다립니다. */
	void WorkerLoop() noexcept;

	/** 작업을 발행하고 호출자도 참여하며 마지막 작업자 완료 뒤 빌린 함수를 해제합니다. */
	void Run(std::size_t InCount, const std::function<void(std::size_t)>& InTask) noexcept;
#endif
};

/**
 * 독립 인덱스 작업을 std::thread 풀과 호출 스레드에 분배합니다.
 * @note Run과 파괴는 같은 소유 스레드에서 직렬 호출합니다. 작업에서 재진입하지 않습니다.
 * 각 작업의 쓰기 저장소는 분리하고, 빌린 데이터는 Run이 반환할 때까지 유지합니다.
 */
class FParallelExecutor
{
public:
	/** 하드웨어 스레드 수를 고려한 백그라운드 작업자 수입니다. 최대 3, 순차 Wasm은 0입니다. */
	static std::size_t DefaultWorkerCount() noexcept;

	/** 0이면 순차 실행하며, 양수는 최대 3으로 제한합니다. 스레드는 첫 병렬 작업에서 생성합니다. */
	explicit FParallelExecutor(std::size_t WorkerLimit = DefaultWorkerCount());

	/** 모든 작업자를 깨워 종료하고 join한 뒤 동기화 저장소를 파괴합니다. */
	~FParallelExecutor();

	FParallelExecutor(const FParallelExecutor&) = delete;

	FParallelExecutor& operator=(const FParallelExecutor&) = delete;

	/**
	 * [0, Count)를 한 번씩 처리하고 모든 쓰기가 호출자에게 보이는 상태로 반환합니다.
	 * @note 작업은 예외를 사용하지 않으며 필요한 실패 상태를 호출자가 제공한 독립 저장소에 기록합니다.
	 * Wasm Worker 예산 부족은 순차 실행합니다. std::thread 생성·동적 할당 자원 소진은 종료 정책을 따릅니다.
	 */
	void Run(std::size_t Count, const std::function<void(std::size_t)>& Task) noexcept;

	/** 현재 생성된 백그라운드 작업자 수입니다. Run 실행 중에는 조회하지 않습니다. */
	std::size_t GetWorkerCount() const noexcept;

private:
	// 소유 스레드만 변경하는 작업자 수 상한입니다. 0은 순차 실행을 뜻합니다.
	std::size_t WorkerLimit;

	// 스레드·동기화·현재 작업을 소유합니다. 순차 실행에서는 할당하지 않습니다.
	std::unique_ptr<FParallelExecutorState> State;
};
