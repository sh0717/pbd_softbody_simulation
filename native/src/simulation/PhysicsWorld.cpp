#include "simulation/PhysicsWorld.h"
#include <cmath>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <utility>

void FPhysicsWorld::Configure(const FSettings& InSettings)
{
	InSettings.Validate(InSettings.Dt);
	Settings = InSettings;
}

void FPhysicsWorld::SetObstacles(std::vector<FObstacle> InObstacles)
{
	std::set<std::uint32_t> ObstacleIds;
	for (const FObstacle& Obstacle : InObstacles)
	{
		if (Obstacle.Id == 0 || !ObstacleIds.insert(Obstacle.Id).second)
		{
			throw std::invalid_argument("Obstacle IDs must be unique and nonzero");
		}
		if (!std::isfinite(Obstacle.Radius) || Obstacle.Radius <= 0)
		{
			throw std::invalid_argument("Invalid obstacle radius");
		}
		for (EAxis Axis : AllAxes)
		{
			if (!std::isfinite(Obstacle.Position[Axis]) || !std::isfinite(Obstacle.Size[Axis]) || Obstacle.Size[Axis] <= 0)
			{
				throw std::invalid_argument("Invalid obstacle geometry");
			}
		}
	}
	Obstacles = std::move(InObstacles);
}

FSoftBody* FPhysicsWorld::AddSoft(std::vector<double> Positions, FIndices Edges, FIndices Tetrahedra, FIndices Surface, FVector3D Offset, std::vector<double> InverseMasses, FVector3D Velocity)
{
	std::unique_ptr<FSoftBody> Body = std::make_unique<FSoftBody>(std::move(Positions), std::move(Edges), std::move(Tetrahedra), std::move(Surface), Offset, std::move(InverseMasses), Velocity);
	FSoftBody* Result = Body.get();
	Bodies.push_back(std::move(Body));
	return Result;
}

FBall* FPhysicsWorld::AddBall(double Radius, FVector3D Position, FVector3D Velocity, double InverseMass)
{
	std::unique_ptr<FBall> Body = std::make_unique<FBall>(Radius, Position, Velocity, InverseMass);
	FBall* Result = Body.get();
	Bodies.push_back(std::move(Body));
	return Result;
}

bool FPhysicsWorld::Step(double Dt)
{
	Settings.Validate(Dt);
	if (Dt == 0)
	{
		return false;
	}
	// 실제 요청 시간을 나눈 초 단위 서브스텝 길이입니다. 적분과 XPBD 유연도 보정이 같은 값을 사용합니다.
	const double SubstepSeconds = Dt / Settings.Substeps;
	Settings.ValidateSubstep(SubstepSeconds);
	Stats = {};
	Bumpers.BeginFrame();
	for (int Substep = 0; Substep < Settings.Substeps; ++Substep)
	{
		AdvanceSubstep(SubstepSeconds);
	}
	return true;
}

void FPhysicsWorld::AdvanceSubstep(double SubstepSeconds)
{
	Collisions.BeginSubstep();

	for (std::unique_ptr<FBody>& Body : Bodies)
	{
		FTimeIntegrator::Predict(*Body, SubstepSeconds, Settings);
	}

	for (int Iteration = 0; Iteration < Settings.Iterations; ++Iteration)
	{
		SolveIteration(SubstepSeconds);
	}

	for (std::unique_ptr<FBody>& Body : Bodies)
	{
		FTimeIntegrator::UpdateVelocities(*Body, SubstepSeconds);
	}

	Collisions.Restitute(Settings);
	Bumpers.Apply(Collisions.GetContacts(), Obstacles, SimulationTime, SubstepSeconds, Settings.Restitution);
	SimulationTime += SubstepSeconds;
}

void FPhysicsWorld::SolveIteration(double SubstepSeconds)
{
	for (std::unique_ptr<FBody>& Body : Bodies)
	{
		if (Body->IsSoft())
		{
			FSoftBody& SoftBody = static_cast<FSoftBody&>(*Body);
			FConstraintSolver::SolveEdges(SoftBody, SubstepSeconds, Settings);
			FConstraintSolver::SolveVolumes(SoftBody, SubstepSeconds, Settings);
		}
	}

	Collisions.SolveDynamic(Bodies, SoftSolver, Stats);

	for (std::unique_ptr<FBody>& Body : Bodies)
	{
		Collisions.SolveStatic(*Body, Obstacles, Settings);
	}
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
