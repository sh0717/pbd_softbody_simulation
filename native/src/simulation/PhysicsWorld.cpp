#include "simulation/PhysicsWorld.h"
#include <cmath>
#include <cstdint>
#include <set>
#include <utility>

FOperationResult FPhysicsWorld::Configure(const FSettings& InSettings)
{
	const FOperationResult Status = InSettings.Validate(InSettings.Dt);

	if (!Status)
	{
		return Status;
	}

	Settings = InSettings;

	return {};
}

FOperationResult FPhysicsWorld::SetObstacles(std::vector<FObstacle> InObstacles)
{
	std::set<std::uint32_t> ObstacleIds;

	for (const FObstacle& Obstacle : InObstacles)
	{
		if (Obstacle.Id == 0 || !ObstacleIds.insert(Obstacle.Id).second)
		{
			return {EErrorCode::InvalidArgument, "Obstacle IDs must be unique and nonzero"};
		}

		for (EAxis Axis : AllAxes)
		{
			if (!std::isfinite(Obstacle.Position[Axis]) || !std::isfinite(Obstacle.Size[Axis]) || Obstacle.Size[Axis] <= 0)
			{
				return {EErrorCode::InvalidArgument, "Invalid obstacle geometry"};
			}
		}
	}

	Obstacles = std::move(InObstacles);

	return {};
}

FSoftBody* FPhysicsWorld::AddSoft(std::vector<double> Positions, FIndices Edges, FIndices Tetrahedra, FIndices Surface, FVector3D Offset, std::vector<double> InverseMasses, FVector3D Velocity, FOperationResult* OutResult)
{
	std::unique_ptr<FSoftBody> Body = std::make_unique<FSoftBody>(std::move(Positions), std::move(Edges), std::move(Tetrahedra), std::move(Surface), Offset, std::move(InverseMasses), Velocity);

	if (!PublishOperationResult(Body->GetInitializationResult(), OutResult))
	{
		return nullptr;
	}

	FSoftBody* Result = Body.get();
	Bodies.push_back(std::move(Body));

	return Result;
}

FBall* FPhysicsWorld::AddBall(double Radius, FVector3D Position, FVector3D Velocity, double InverseMass, FOperationResult* OutResult)
{
	std::unique_ptr<FBall> Body = std::make_unique<FBall>(Radius, Position, Velocity, InverseMass);

	if (!PublishOperationResult(Body->GetInitializationResult(), OutResult))
	{
		return nullptr;
	}

	FBall* Result = Body.get();
	Bodies.push_back(std::move(Body));

	return Result;
}

bool FPhysicsWorld::Step(double Dt, FOperationResult* OutResult)
{
	if (!PublishOperationResult(Settings.Validate(Dt), OutResult))
	{
		return false;
	}

	if (Dt == 0)
	{
		return false;
	}

	// 실제 요청 시간을 나눈 초 단위 서브스텝 길이입니다. 적분과 XPBD 유연도 보정이 같은 값을 사용합니다.
	const double SubstepSeconds = Dt / Settings.Substeps;

	if (!PublishOperationResult(Settings.ValidateSubstep(SubstepSeconds), OutResult))
	{
		return false;
	}

	Stats = {};
	Bumpers.BeginFrame();

	for (int Substep = 0; Substep < Settings.Substeps; ++Substep)
	{
		if (!PublishOperationResult(AdvanceSubstep(SubstepSeconds), OutResult))
		{
			return false;
		}
	}

	return true;
}

FOperationResult FPhysicsWorld::AdvanceSubstep(double SubstepSeconds)
{
	Collisions.BeginSubstep();

	for (std::unique_ptr<FBody>& Body : Bodies)
	{
		FTimeIntegrator::Predict(*Body, SubstepSeconds, Settings);
	}

	for (int Iteration = 0; Iteration < Settings.Iterations; ++Iteration)
	{
		const FOperationResult Status = SolveIteration(SubstepSeconds);

		if (!Status)
		{
			return Status;
		}
	}

	for (std::unique_ptr<FBody>& Body : Bodies)
	{
		FTimeIntegrator::UpdateVelocities(*Body, SubstepSeconds);
	}

	Collisions.Restitute(Settings);
	Bumpers.Apply(Collisions.GetContacts(), Obstacles, SimulationTime, SubstepSeconds, Settings.Restitution);
	SimulationTime += SubstepSeconds;

	return {};
}

FOperationResult FPhysicsWorld::SolveIteration(double SubstepSeconds)
{
	ConstraintBodies.clear();

	for (const std::unique_ptr<FBody>& Body : Bodies)
	{
		if (Body->IsSoft())
		{
			ConstraintBodies.push_back(static_cast<FSoftBody*>(Body.get()));
		}
	}

	// 몸체 인덱스를 고정 연속 구간으로 나누므로 계산 중 다음 인덱스를 얻기 위한 잠금이 없습니다.
	ConstraintExecutor.Run(ConstraintBodies.size(), [this, SubstepSeconds](std::size_t BodyIndex) noexcept
	{
		FSoftBody& SoftBody = *ConstraintBodies[BodyIndex];

		// 같은 몸체의 위치를 공유하므로 거리 → 체적 순서를 한 작업 안에서 유지합니다.
		FConstraintSolver::SolveEdges(SoftBody, SubstepSeconds, Settings);
		FConstraintSolver::SolveVolumes(SoftBody, SubstepSeconds, Settings);
	});

	// Run의 완료 대기 뒤에만 몸체 간 위치·접촉 공유 상태에 접근합니다.
	const FOperationResult Status = Collisions.SolveDynamic(Bodies, SoftSolver, Stats);

	if (!Status)
	{
		return Status;
	}

	for (std::unique_ptr<FBody>& Body : Bodies)
	{
		Collisions.SolveStatic(*Body, Obstacles, Settings);
	}

	return {};
}

void FPhysicsWorld::Settle()
{
	Collisions.Settle(Bodies, Obstacles, Settings);
}

void FPhysicsWorld::Reset()
{
	for (std::unique_ptr<FBody>& Body : Bodies)
	{
		Body->Reset();
	}

	SimulationTime = 0;
	Stats = {};
	Bumpers.Reset();
	Collisions.BeginSubstep();
}
