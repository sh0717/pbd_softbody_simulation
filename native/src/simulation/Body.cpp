#include "simulation/Body.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace
{
	void ValidateInverseMasses(const std::vector<double>& InverseMasses)
	{
		for (double InverseMassValue : InverseMasses)
		{
			if (!std::isfinite(InverseMassValue) || InverseMassValue < 0)
			{
				throw std::invalid_argument("Invalid inverseMass");
			}
		}
	}
} // namespace

FBody::FBody(EBodyType InType, std::vector<double> Vertices, FVector3D PositionOffset, std::vector<double> InverseMasses, FVector3D Velocity)
	: Type(InType)
	, Positions(std::move(Vertices))
	, InverseMass(std::move(InverseMasses))
{
	if (Positions.empty() || Positions.size() % 3 || !PositionOffset.IsFinite() || !Velocity.IsFinite() ||
		std::any_of(Positions.begin(), Positions.end(), [](double Component)
	{
		return !std::isfinite(Component);
	}))
	{
		throw std::invalid_argument("Invalid mesh vertices or vec3");
	}
	// XYZ 성분 세 개를 입자 하나로 환산한 수이며 역질량 배열 길이의 검증 기준입니다.
	const std::size_t VertexCount = Positions.size() / 3;
	if (InverseMass.empty())
	{
		InverseMass.assign(VertexCount, 1);
	}
	if (InverseMass.size() != VertexCount)
	{
		throw std::invalid_argument("Invalid inverseMass length");
	}
	ValidateInverseMasses(InverseMass);
	for (std::size_t ComponentIndex = 0; ComponentIndex < Positions.size(); ++ComponentIndex)
	{
		Positions[ComponentIndex] += PositionOffset[static_cast<EAxis>(ComponentIndex % 3)];
	}
	PreviousPositions = Positions;
	RestPositions = PreviousPositions;
	Velocities.resize(Positions.size());
	PreSolveVelocities.resize(Positions.size());
	for (std::size_t ComponentIndex = 0; ComponentIndex < Velocities.size(); ++ComponentIndex)
	{
		Velocities[ComponentIndex] = Velocity[static_cast<EAxis>(ComponentIndex % 3)];
	}
}

void FBody::PublishBounds(FVector3D InCenter, FVector3D Minimum, FVector3D Maximum, double InRadius) noexcept
{
	Center = InCenter;
	BoundsMin = Minimum;
	BoundsMax = Maximum;
	Radius = InRadius;
}

void FBody::SavePreSolveVelocities() noexcept
{
	std::copy(Velocities.begin(), Velocities.end(), PreSolveVelocities.begin());
}

void FBody::SetParticlePosition(std::size_t ParticleIndex, FVector3D Position)
{
	SetParticleVector(Positions, ParticleIndex, Position);
}

void FBody::SetParticleVelocity(std::size_t ParticleIndex, FVector3D Velocity)
{
	SetParticleVector(Velocities, ParticleIndex, Velocity);
}

void FBody::SetParticleVector(std::vector<double>& InOutValues, std::size_t ParticleIndex, const FVector3D& Value)
{
	if (ParticleIndex >= GetVertexCount())
	{
		throw std::out_of_range("Particle index out of range");
	}
	if (!Value.IsFinite())
	{
		throw std::invalid_argument("Expected a finite vec3");
	}
	// 입자 번호를 연속 XYZ 저장소의 첫 성분 위치로 변환한 오프셋입니다.
	const std::size_t ParticleOffset = ParticleIndex * 3;
	for (EAxis Axis : AllAxes)
	{
		InOutValues[ParticleOffset + ToIndex(Axis)] = Value[Axis];
	}
}

void FBody::SetParticleInverseMass(std::size_t ParticleIndex, double InInverseMass)
{
	if (ParticleIndex >= GetVertexCount())
	{
		throw std::out_of_range("Particle index out of range");
	}
	if (!std::isfinite(InInverseMass) || InInverseMass < 0)
	{
		throw std::invalid_argument("Invalid inverseMass");
	}
	InverseMass[ParticleIndex] = InInverseMass;
}

void FBody::SetDragTarget(std::optional<FVector3D> Target)
{
	if (Target && !Target->IsFinite())
	{
		throw std::invalid_argument("Expected a finite vec3");
	}
	DragTarget = Target;
}

void FBody::BeginSubstep()
{
	UpdateBounds();
}

void FBody::MoveTo(FVector3D Target)
{
	if (!Target.IsFinite())
	{
		throw std::invalid_argument("Expected a finite vec3");
	}
	UpdateBounds();
	// 몸체 중심을 목표에 맞출 공통 이동량입니다. 기준 위치와 이전 위치는 함께 이동시키지 않습니다.
	const FVector3D Translation = Target - Center;
	for (std::uint32_t ParticleIndex = 0; ParticleIndex < GetVertexCount(); ++ParticleIndex)
	{
		AddParticleVector(Positions, ParticleIndex, Translation);
	}
	UpdateBounds();
}

void FBody::Reset()
{
	std::copy(RestPositions.begin(), RestPositions.end(), Positions.begin());
	std::copy(RestPositions.begin(), RestPositions.end(), PreviousPositions.begin());
	std::fill(Velocities.begin(), Velocities.end(), 0);
	std::fill(PreSolveVelocities.begin(), PreSolveVelocities.end(), 0);
	DragTarget.reset();
	UpdateBounds();
}
