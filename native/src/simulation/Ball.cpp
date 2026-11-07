#include "simulation/Ball.h"
#include <stdexcept>

FBall::FBall(double Radius, FVector3D Position, FVector3D Velocity, double InverseMass)
	: FBody(EBodyType::Ball, {Position.X, Position.Y, Position.Z}, {}, {InverseMass}, Velocity)
{
	if (!std::isfinite(Radius) || Radius <= 0)
	{
		throw std::invalid_argument("Invalid ball radius");
	}
	SavePreSolveVelocities();
	PublishBounds(ReadParticleVector(GetPositions(), 0), GetBoundsMin(), GetBoundsMax(), Radius);
}

void FBall::UpdateBounds()
{
	PublishBounds(ReadParticleVector(GetPositions(), 0), GetBoundsMin(), GetBoundsMax(), GetRadius());
}
