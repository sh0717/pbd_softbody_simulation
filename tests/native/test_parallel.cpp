#include "simulation/PhysicsWorld.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace
{
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
		Executor.Run(0, [](std::size_t)
		{
			Require(false, "empty task ran");
		});
		Executor.Run(1, [](std::size_t)
		{
		});
		Require(Executor.GetWorkerCount() == 0, "small batch created workers");

		std::mutex GateMutex;
		std::condition_variable Gate;
		std::size_t Arrived = 0;
		std::vector<std::thread::id> ThreadIds(4);
		Executor.Run(4, [&](std::size_t Index)
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
			Executor.Run(Visits.size(), [&](std::size_t Index)
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
		Executor.Run(Results.size(), [&](std::size_t Index)
		{
			if (Index == 2)
			{
				Results[Index] = {EErrorCode::InvalidArgument, "task failure"};
			}

			++Completed;
		});
		Require(Completed == 20 && Results[2].Code == EErrorCode::InvalidArgument, "task status was consumed before all work finished");
		Executor.Run(7, [&](std::size_t)
		{
			++Completed;
		});
		Require(Completed == 27, "executor did not process the next batch");
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
	CheckWorlds();
	std::cout << "Parallel overlap, exact PBD/XPBD equivalence, task statuses and lifetimes passed.\n";

	if (ArgCount > 1 && std::string(Arguments[1]) == "--benchmark")
	{
		Benchmark();
	}

	return 0;
}
