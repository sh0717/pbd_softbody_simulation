#include "simulation/PhysicsWorld.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <thread>

namespace
{

	// 선언의 제약을 검사합니다. 본문 안의 static_assert만으로는 잘못된 호출이 여기서 거부되지 않습니다.
	template <typename TaskType>
	concept CAcceptsRangeTask = requires(FParallelExecutor& Executor, const TaskType& Task) {
		Executor.ExecuteRangesAndWait(1, Task);
	};

	template <typename TaskType>
	concept CAcceptsIndexTask = requires(FParallelExecutor& Executor, const TaskType& Task) {
		Executor.Run(1, Task);
	};

	using FValidRangeTask = decltype([](std::size_t, std::size_t) noexcept
	{
	});
	using FValidIndexTask = decltype([](std::size_t) noexcept
	{
	});
	using FThrowingRangeTask = decltype([](std::size_t, std::size_t)
	{
	});
	using FThrowingIndexTask = decltype([](std::size_t)
	{
	});
	using FMutableTask = decltype([Count = 0](auto...) mutable noexcept
	{
		++Count;
	});
	using FValueReturningTask = decltype([](auto...) noexcept
	{
		return 1;
	});
	using FRvalueOnlyTask = decltype([](std::size_t&&, auto&&...) noexcept
	{
	});

	struct FVolatileTask
	{
		void operator()(...) const volatile noexcept {}
	};

	using FRawRangeTask = void(std::size_t, std::size_t) noexcept;
	using FRawIndexTask = void(std::size_t) noexcept;

	static_assert(CAcceptsRangeTask<FValidRangeTask> && CAcceptsIndexTask<FValidIndexTask>);
	static_assert(!CAcceptsRangeTask<FValidIndexTask> && !CAcceptsIndexTask<FValidRangeTask>);
	static_assert(!CAcceptsRangeTask<FThrowingRangeTask> && !CAcceptsIndexTask<FThrowingIndexTask>);
	static_assert(!CAcceptsRangeTask<FMutableTask> && !CAcceptsIndexTask<FMutableTask>);
	static_assert(!CAcceptsRangeTask<FValueReturningTask> && !CAcceptsIndexTask<FValueReturningTask>);
	static_assert(!CAcceptsRangeTask<FRvalueOnlyTask> && !CAcceptsIndexTask<FRvalueOnlyTask>);
	static_assert(!CAcceptsRangeTask<volatile FVolatileTask> && !CAcceptsIndexTask<volatile FVolatileTask>);
	static_assert(!CAcceptsRangeTask<int> && !CAcceptsIndexTask<int>);
	static_assert(!CAcceptsRangeTask<FRawRangeTask> && !CAcceptsIndexTask<FRawIndexTask>);
	static_assert(CAcceptsRangeTask<FRawRangeTask*> && CAcceptsIndexTask<FRawIndexTask*>);

	void Require(bool bCondition, const char* Message)
	{
		if (!bCondition)
		{
			std::cerr << "FAIL: " << Message << '\n';
			std::exit(EXIT_FAILURE);
		}
	}

	FSoftBody* AddTetrahedra(FPhysicsWorld& World, int TetraCount, FVector3D Offset)
	{
		std::vector<double> Positions;
		FIndices Edges;
		FIndices Tetrahedra;

		for (int Index = 0; Index < TetraCount; ++Index)
		{
			const double X = (Index % 10) * 1.5;
			const double Y = ((Index / 10) % 10) * 1.5;
			const double Z = (Index / 100) * 1.5;
			Positions.insert(Positions.end(), {X, Y, Z, X + 1, Y, Z, X, Y + 1, Z, X, Y, Z + 1});

			const std::uint32_t Base = static_cast<std::uint32_t>(Index * 4);
			Tetrahedra.insert(Tetrahedra.end(), {Base, Base + 1, Base + 2, Base + 3});
			Edges.insert(Edges.end(), {Base, Base + 1, Base, Base + 2, Base, Base + 3, Base + 1, Base + 2, Base + 1, Base + 3, Base + 2, Base + 3});
		}

		FSoftBody* Body = World.AddSoft(std::move(Positions), std::move(Edges), std::move(Tetrahedra), {}, Offset, {}, {0.2, -0.1, 0.3});
		Body->SetParticleInverseMass(0, 0);
		Body->SetParticlePosition(3, Offset + FVector3D{0.2, 0.1, 1.4});

		return Body;
	}

	void CheckExecutor()
	{
		FParallelExecutor Executor(3);
		bool bEmptyTaskRan = false;
		Executor.Run(0, [&](std::size_t) noexcept
		{
			bEmptyTaskRan = true;
		});
		Require(!bEmptyTaskRan, "empty task ran");
		Executor.Run(1, [](std::size_t) noexcept
		{
		});
		Require(Executor.GetWorkerCount() == 0, "small batch created workers");

		std::mutex GateMutex;
		std::condition_variable Gate;
		std::size_t Arrived = 0;
		std::vector<std::thread::id> ThreadIds(4);
		Executor.Run(4, [&](std::size_t Index) noexcept
		{
			ThreadIds[Index] = std::this_thread::get_id();

			std::unique_lock<std::mutex> Lock(GateMutex);
			++Arrived;
			Gate.notify_all();
			Require(Gate.wait_for(Lock, std::chrono::seconds(10), [&]
			{
				return Arrived == 4;
			}),
				"tasks did not overlap");
		});
		std::sort(ThreadIds.begin(), ThreadIds.end());
		Require(std::unique(ThreadIds.begin(), ThreadIds.end()) == ThreadIds.end(), "tasks reused a blocked thread");
		Require(Executor.GetWorkerCount() == 3, "expected three background workers");

		for (int Batch = 0; Batch < 100; ++Batch)
		{
			std::vector<int> Visits(37, 0);
			Executor.Run(Visits.size(), [&](std::size_t Index) noexcept
			{
				++Visits[Index];
			});
			Require(std::all_of(Visits.begin(), Visits.end(), [](int Value)
			{
				return Value == 1;
			}),
				"lost or duplicated work");
		}

		std::atomic<int> Completed{0};
		std::vector<FOperationResult> Results(20);
		Executor.Run(Results.size(), [&](std::size_t Index) noexcept
		{
			if (Index == 2)
			{
				Results[Index] = {EErrorCode::InvalidArgument, "task failure"};
			}

			++Completed;
		});
		Require(Completed == 20 && Results[2].Code == EErrorCode::InvalidArgument, "task status was consumed before all work finished");
		Executor.Run(7, [&](std::size_t) noexcept
		{
			++Completed;
		});
		Require(Completed == 27, "executor did not process the next batch");
	}

	void CheckRunFixedRanges()
	{
		FParallelExecutor Executor(3);
		std::vector<std::thread::id> ThreadIds(8);
		std::mutex GateMutex;
		std::condition_variable Gate;
		std::size_t Arrived = 0;
		Executor.Run(ThreadIds.size(), [&](std::size_t Index) noexcept
		{
			ThreadIds[Index] = std::this_thread::get_id();
			if (Index % 2 == 0)
			{
				// 각 구간의 첫 항목을 모두 시작할 때까지 막아 동적 인덱스 배분과 구별합니다.
				std::unique_lock<std::mutex> Lock(GateMutex);
				++Arrived;
				Gate.notify_all();
				Require(Gate.wait_for(Lock, std::chrono::seconds(10), [&]
				{
					return Arrived == 4;
				}),
					"fixed ranges did not overlap");
			}
		});

		std::vector<std::thread::id> ParticipantIds;
		for (std::size_t FirstIndex = 0; FirstIndex < ThreadIds.size(); FirstIndex += 2)
		{
			Require(ThreadIds[FirstIndex] == ThreadIds[FirstIndex + 1], "Run did not keep each fixed range on one participant");
			ParticipantIds.push_back(ThreadIds[FirstIndex]);
		}
		std::sort(ParticipantIds.begin(), ParticipantIds.end());
		Require(std::unique(ParticipantIds.begin(), ParticipantIds.end()) == ParticipantIds.end(), "fixed ranges did not use four participants");
		Require(ThreadIds[0] == std::this_thread::get_id(), "caller did not execute the first fixed range");
	}

	/** 복사가 금지된 작업도 동기 호출 동안만 빌려 실행할 수 있어야 합니다. */
	struct FNonCopyRangeTask
	{
		// 배타적인 구간이 한 번씩 수정하는 검사 저장소입니다.
		std::vector<int>& Visits;

		// 이 배치의 호출 스레드이며 함수 객체보다 오래 유지됩니다.
		std::thread::id CallerId;

		// 고정 구간을 실제 수행한 호출 스레드의 참여 여부입니다.
		std::atomic<bool>& bCallerParticipated;

		FNonCopyRangeTask(std::vector<int>& InVisits, std::atomic<bool>& InCallerParticipated)
			: Visits(InVisits)
			, CallerId(std::this_thread::get_id())
			, bCallerParticipated(InCallerParticipated)
		{
		}

		FNonCopyRangeTask(const FNonCopyRangeTask&) = delete;

		FNonCopyRangeTask& operator=(const FNonCopyRangeTask&) = delete;

		void operator()(std::size_t FirstIndex, std::size_t EndIndexExclusive) const noexcept
		{
			Require(FirstIndex < EndIndexExclusive && EndIndexExclusive <= Visits.size(), "invalid nonempty fixed range");

			if (std::this_thread::get_id() == CallerId)
			{
				bCallerParticipated.store(true);
			}

			for (std::size_t Index = FirstIndex; Index < EndIndexExclusive; ++Index)
			{
				++Visits[Index];
			}
		}
	};

	static_assert(CAcceptsRangeTask<FNonCopyRangeTask>);

	void CheckFixedRanges()
	{
		FParallelExecutor Executor(3);
		bool bEmptyTaskRan = false;
		Executor.ExecuteRangesAndWait(0, [&](std::size_t, std::size_t) noexcept
		{
			bEmptyTaskRan = true;
		});
		Require(!bEmptyTaskRan, "empty fixed batch ran");
		Require(Executor.GetWorkerCount() == 0 && Executor.GetLastExecutionWorkerCount() == 0, "empty fixed batch created workers");

		for (int Batch = 0; Batch < 100; ++Batch)
		{
			for (std::size_t Count : {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{4}, std::size_t{37}, std::size_t{0}})
			{
				std::vector<int> Visits(Count, 0);
				std::atomic<bool> bCallerParticipated{false};
				const FNonCopyRangeTask Task(Visits, bCallerParticipated);
				Executor.ExecuteRangesAndWait(Count, Task);
				Require(std::all_of(Visits.begin(), Visits.end(), [](int Value)
				{
					return Value == 1;
				}),
					"fixed range work was lost or duplicated");
				Require(bCallerParticipated.load() == (Count > 0), "fixed range caller did not participate");
				Require(Executor.GetLastExecutionWorkerCount() == (Count > 1 ? std::min<std::size_t>(3, Count - 1) : 0), "fixed range telemetry reports idle workers");
			}
		}

		std::mutex RangeMutex;
		std::vector<std::pair<std::size_t, std::size_t>> Ranges;
		Executor.ExecuteRangesAndWait(std::numeric_limits<std::size_t>::max(), [&](std::size_t FirstIndex, std::size_t EndIndexExclusive) noexcept
		{
			const std::lock_guard<std::mutex> Lock(RangeMutex);
			Ranges.emplace_back(FirstIndex, EndIndexExclusive);
		});
		std::sort(Ranges.begin(), Ranges.end());
		Require(Ranges.size() == 4 && Ranges.front().first == 0 && Ranges.back().second == std::numeric_limits<std::size_t>::max(), "large fixed range overflowed");

		for (std::size_t Index = 1; Index < Ranges.size(); ++Index)
		{
			Require(Ranges[Index - 1].second == Ranges[Index].first && Ranges[Index].first < Ranges[Index].second, "fixed ranges overlap or leave a gap");
		}

		FParallelExecutor Serial(0);
		std::size_t SerialVisits = 0;
		Serial.ExecuteRangesAndWait(37, [&](std::size_t FirstIndex, std::size_t EndIndexExclusive) noexcept
		{
			Require(FirstIndex == 0 && EndIndexExclusive == 37, "serial fixed range was split");
			++SerialVisits;
		});
		Require(SerialVisits == 1 && Serial.GetWorkerCount() == 0 && Serial.GetLastExecutionWorkerCount() == 0, "serial fixed range used workers");
	}

	void CheckDynamicIndices()
	{
		FParallelExecutor Executor(3);
		const std::thread::id CallerId = std::this_thread::get_id();
		bool bSmallTaskRan = false;
		Executor.ExecuteRangesAndWait(0, [&](std::size_t, std::size_t) noexcept
		{
			bSmallTaskRan = true;
		}, EParallelSchedule::DynamicIndices);
		Require(!bSmallTaskRan, "empty dynamic batch ran");
		Executor.ExecuteRangesAndWait(1, [&](std::size_t FirstIndex, std::size_t EndIndexExclusive) noexcept
		{
			Require(FirstIndex == 0 && EndIndexExclusive == 1 && std::this_thread::get_id() == CallerId, "single dynamic item did not run on caller");
			bSmallTaskRan = true;
		}, EParallelSchedule::DynamicIndices);
		Require(bSmallTaskRan && Executor.GetWorkerCount() == 0 && Executor.GetLastExecutionWorkerCount() == 0, "small dynamic batch created workers");

		// 네 참여자를 조건 변수로 합류시켜 실제 동시 실행과 이후 풀 재사용을 검사합니다.
		const auto CollectParticipantIds = [&](EParallelSchedule Schedule)
		{
			std::mutex GateMutex;
			std::condition_variable Gate;
			std::vector<std::thread::id> ParticipantIds;
			Executor.ExecuteRangesAndWait(4, [&](std::size_t FirstIndex, std::size_t EndIndexExclusive) noexcept
			{
				Require(EndIndexExclusive == FirstIndex + 1 && EndIndexExclusive <= 4, "four-participant batch did not assign single items");

				std::unique_lock<std::mutex> Lock(GateMutex);
				ParticipantIds.push_back(std::this_thread::get_id());
				Gate.notify_all();
				Require(Gate.wait_for(Lock, std::chrono::seconds(10), [&]
				{
					return ParticipantIds.size() == 4;
				}),
					"dynamic policy transition did not complete on four participants");
			}, Schedule);
			std::sort(ParticipantIds.begin(), ParticipantIds.end());
			Require(ParticipantIds.size() == 4 && std::unique(ParticipantIds.begin(), ParticipantIds.end()) == ParticipantIds.end(), "dynamic policy reused a blocked participant");
			Require(std::binary_search(ParticipantIds.begin(), ParticipantIds.end(), CallerId), "dynamic caller did not participate");

			return ParticipantIds;
		};
		const std::vector<std::thread::id> InitialParticipantIds = CollectParticipantIds(EParallelSchedule::DynamicIndices);

		for (int Batch = 0; Batch < 20; ++Batch)
		{
			for (EParallelSchedule Schedule : {EParallelSchedule::DynamicIndices, EParallelSchedule::FixedRanges})
			{
				for (std::size_t Count : {std::size_t{37}, std::size_t{2}, std::size_t{0}, std::size_t{1}, std::size_t{3}, std::size_t{9}})
				{
					// relaxed 원자는 검사 자체의 데이터 경쟁 없이 반환 직후 완료 결과를 관찰합니다.
					std::vector<std::atomic<int>> Visits(Count);
					std::vector<std::atomic<int>> Results(Count);
					Executor.ExecuteRangesAndWait(Count, [&](std::size_t FirstIndex, std::size_t EndIndexExclusive) noexcept
					{
						Require(FirstIndex < EndIndexExclusive && EndIndexExclusive <= Count, "dynamic policy transition produced an invalid range");

						if (Schedule == EParallelSchedule::DynamicIndices && Count > 1)
						{
							Require(EndIndexExclusive == FirstIndex + 1, "dynamic batch stopped assigning individual items");
						}

						for (std::size_t Index = FirstIndex; Index < EndIndexExclusive; ++Index)
						{
							Visits[Index].fetch_add(1, std::memory_order_relaxed);
							Results[Index].store(Batch + static_cast<int>(Index) + 1, std::memory_order_relaxed);
						}
					}, Schedule);

					for (std::size_t Index = 0; Index < Count; ++Index)
					{
						Require(Visits[Index].load(std::memory_order_relaxed) == 1, "dynamic policy transition lost or duplicated work");
						Require(Results[Index].load(std::memory_order_relaxed) == Batch + static_cast<int>(Index) + 1, "dynamic batch returned before results became visible");
					}
					Require(Executor.GetWorkerCount() == 3, "dynamic policy transition discarded the worker pool");
					Require(Executor.GetLastExecutionWorkerCount() == (Count > 1 ? std::min<std::size_t>(3, Count - 1) : 0), "dynamic policy transition reported incorrect assigned workers");
				}
			}
		}
		Require(CollectParticipantIds(EParallelSchedule::FixedRanges) == InitialParticipantIds, "dynamic and fixed policies did not reuse the same workers");

		FParallelExecutor Serial(0);
		for (std::size_t Count : {std::size_t{0}, std::size_t{1}, std::size_t{37}})
		{
			std::size_t CallCount = 0;
			std::size_t ItemVisits = 0;
			Serial.ExecuteRangesAndWait(Count, [&](std::size_t FirstIndex, std::size_t EndIndexExclusive) noexcept
			{
				Require(FirstIndex == 0 && EndIndexExclusive == Count && std::this_thread::get_id() == CallerId, "serial dynamic fallback did not execute the full range on caller");
				++CallCount;
				ItemVisits += EndIndexExclusive - FirstIndex;
			}, EParallelSchedule::DynamicIndices);
			Require(CallCount == (Count > 0 ? 1u : 0u) && ItemVisits == Count, "serial dynamic fallback lost or duplicated work");
			Require(Serial.GetWorkerCount() == 0 && Serial.GetLastExecutionWorkerCount() == 0, "serial dynamic fallback used workers");
		}
	}

	void CompareBodies(const FBody& A, const FBody& B)
	{
		Require(A.GetPositions() == B.GetPositions(), "positions differ");
		Require(A.GetVelocities() == B.GetVelocities(), "velocities differ");
		Require(A.GetPreviousPositions() == B.GetPreviousPositions(), "previous positions differ");
		Require(A.GetPreSolveVelocities() == B.GetPreSolveVelocities(), "pre-solve velocities differ");

		if (A.IsSoft())
		{
			const FSoftBody& SoftA = static_cast<const FSoftBody&>(A);
			const FSoftBody& SoftB = static_cast<const FSoftBody&>(B);
			Require(SoftA.GetEdgeLambdas() == SoftB.GetEdgeLambdas(), "edge multipliers differ");
			Require(SoftA.GetVolumeLambdas() == SoftB.GetVolumeLambdas(), "volume multipliers differ");
		}
	}

	void CheckWorlds()
	{
		for (bool bXpbd : {false, true})
		{
			FPhysicsWorld Serial(0);
			FPhysicsWorld Parallel(3);
			FSettings Settings;
			Settings.bXpbd = bXpbd;
			Settings.Gravity = {0, -9.81, 0};
			Settings.Substeps = 3;
			Settings.Iterations = 4;
			Settings.Bounds = 4;
			Serial.Configure(Settings);
			Parallel.Configure(Settings);

			std::vector<FBody*> SerialBodies;
			std::vector<FBody*> ParallelBodies;

			for (int Index = 0; Index < 5; ++Index)
			{
				const FVector3D Offset{Index * 0.7 - 2, -3.5, 0};
				SerialBodies.push_back(AddTetrahedra(Serial, 2, Offset));
				ParallelBodies.push_back(AddTetrahedra(Parallel, 2, Offset));
			}

			SerialBodies.push_back(Serial.AddBall(0.6, {0, -2.5, 0}));
			ParallelBodies.push_back(Parallel.AddBall(0.6, {0, -2.5, 0}));
			Require(!Parallel.Step(0) && Parallel.GetConstraintWorkerCount() == 0, "zero dt started workers");

			FOperationResult InvalidStatus;
			Require(!Parallel.Step(-1, &InvalidStatus) && InvalidStatus.Code == EErrorCode::OutOfRange && Parallel.GetConstraintWorkerCount() == 0, "invalid dt started workers");

			for (int Frame = 0; Frame < 120; ++Frame)
			{
				if (Frame == 60)
				{
					Serial.Reset();
					Parallel.Reset();
					SerialBodies.push_back(AddTetrahedra(Serial, 1, {2, 1, 0}));
					ParallelBodies.push_back(AddTetrahedra(Parallel, 1, {2, 1, 0}));
				}

				Serial.Step(1.0 / 60);
				Parallel.Step(1.0 / 60);

				for (std::size_t Index = 0; Index < SerialBodies.size(); ++Index)
				{
					CompareBodies(*SerialBodies[Index], *ParallelBodies[Index]);
				}

				Require(Serial.GetSimulationTime() == Parallel.GetSimulationTime(), "time differs");
			}

			Require(Serial.GetConstraintWorkerCount() == 0 && Parallel.GetConstraintWorkerCount() == 3, "world did not use requested mode");
		}

		for (int Lifetime = 0; Lifetime < 20; ++Lifetime)
		{
			FPhysicsWorld World(2);
			AddTetrahedra(World, 1, {});
			AddTetrahedra(World, 1, {2, 0, 0});
			World.Step(0.01);
		}
	}

	void Benchmark()
	{
		for (std::size_t WorkerCount : {std::size_t{0}, std::size_t{3}})
		{
			FPhysicsWorld World(WorkerCount);
			FSettings Settings;
			Settings.Bounds = 10000;
			Settings.Gravity = {0, -9.81, 0};
			World.Configure(Settings);

			for (int Index = 0; Index < 8; ++Index)
			{
				AddTetrahedra(World, 1000, {Index * 100.0, 0, 0});
			}

			std::vector<double> Milliseconds;

			for (int Frame = 0; Frame < 35; ++Frame)
			{
				const std::chrono::steady_clock::time_point Start = std::chrono::steady_clock::now();
				World.Step(1.0 / 60);

				const double Elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();

				if (Frame >= 5)
				{
					Milliseconds.push_back(Elapsed);
				}
			}

			std::sort(Milliseconds.begin(), Milliseconds.end());
			std::cout << "workers=" << World.GetConstraintWorkerCount() << " median_step_ms=" << Milliseconds[Milliseconds.size() / 2] << '\n';
		}
	}
} // namespace

int main(int ArgCount, char** Arguments)
{
	CheckExecutor();
	CheckRunFixedRanges();
	CheckFixedRanges();
	CheckDynamicIndices();
	CheckWorlds();
	std::cout << "Parallel overlap, exact PBD/XPBD equivalence, task statuses and lifetimes passed.\n";

	if (ArgCount > 1 && std::string(Arguments[1]) == "--benchmark")
	{
		Benchmark();
	}

	return 0;
}
