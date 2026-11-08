#include "simulation/Ball.h"

FBall::FBall(double Radius, FVector3D Position, FVector3D Velocity, double InverseMass)
	: FBody(EBodyType::Ball, {Position.X, Position.Y, Position.Z}, {}, {InverseMass}, Velocity)
{
	if (!InitializationResult)
	{
		return;
	}

	if (!std::isfinite(Radius) || Radius <= 0)
	{
		InitializationResult = {EErrorCode::InvalidArgument, "Invalid ball radius"};

		return;
	}

	SavePreSolveVelocities();
	PublishBounds(ReadParticleVector(GetPositions(), 0), GetBoundsMin(), GetBoundsMax(), Radius);
}

void FBall::UpdateBounds()
{
	PublishBounds(ReadParticleVector(GetPositions(), 0), GetBoundsMin(), GetBoundsMax(), GetRadius());
}
