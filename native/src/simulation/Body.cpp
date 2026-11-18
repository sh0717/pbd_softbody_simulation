#include "simulation/Body.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace
{
	bool AreInverseMassesValid(const std::vector<double>& InverseMasses)
	{
		for (double InverseMassValue : InverseMasses)
		{
			if (!std::isfinite(InverseMassValue) || InverseMassValue < 0)
			{
				return false;
			}
		}

		return true;
	}

	FOperationResult CalculateScaledMovableMass(const std::vector<double>& InverseMasses, double& OutScaledMass, double& OutMinimumInverseMass)
	{
		// 가장 작은 양의 역질량으로 각 입자 질량을 정규화해 1 이하인 항만 더합니다.
		double MinimumInverseMass = std::numeric_limits<double>::infinity();
		for (double InverseMassValue : InverseMasses)
		{
			if (!std::isfinite(InverseMassValue) || InverseMassValue < 0)
			{
				return {EErrorCode::InvalidArgument, "Invalid inverseMass"};
			}
			if (InverseMassValue > 0)
			{
				MinimumInverseMass = std::min(MinimumInverseMass, InverseMassValue);
			}
		}

		OutScaledMass = 0;
		OutMinimumInverseMass = 1;
		if (std::isfinite(MinimumInverseMass))
		{
			for (double InverseMassValue : InverseMasses)
			{
				if (InverseMassValue > 0)
				{
					OutScaledMass += MinimumInverseMass / InverseMassValue;
				}
			}
			OutMinimumInverseMass = MinimumInverseMass;
		}

		return {};
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
		InitializationResult = {EErrorCode::InvalidArgument, "Invalid mesh vertices or vec3"};

		return;
	}

	// XYZ 성분 세 개를 입자 하나로 환산한 수이며 역질량 배열 길이의 검증 기준입니다.
	const std::size_t VertexCount = Positions.size() / 3;

	if (InverseMass.empty())
	{
		InverseMass.assign(VertexCount, 1);
	}

	if (InverseMass.size() != VertexCount)
	{
		InitializationResult = {EErrorCode::InvalidArgument, "Invalid inverseMass length"};

		return;
	}

	if (!AreInverseMassesValid(InverseMass))
	{
		InitializationResult = {EErrorCode::InvalidArgument, "Invalid inverseMass"};

		return;
	}

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

FOperationResult FBody::SetParticlePosition(std::size_t ParticleIndex, FVector3D Position)
{
	return SetParticleVector(Positions, ParticleIndex, Position);
}

FOperationResult FBody::SetParticleVelocity(std::size_t ParticleIndex, FVector3D Velocity)
{
	return SetParticleVector(Velocities, ParticleIndex, Velocity);
}

FOperationResult FBody::SetParticleVector(std::vector<double>& InOutValues, std::size_t ParticleIndex, const FVector3D& Value)
{
	if (ParticleIndex >= GetVertexCount())
	{
		return {EErrorCode::OutOfRange, "Particle index out of range"};
	}

	if (!Value.IsFinite())
	{
		return {EErrorCode::InvalidArgument, "Expected a finite vec3"};
	}

	// 입자 번호를 연속 XYZ 저장소의 첫 성분 위치로 변환한 오프셋입니다.
	const std::size_t ParticleOffset = ParticleIndex * 3;

	for (EAxis Axis : AllAxes)
	{
		InOutValues[ParticleOffset + ToIndex(Axis)] = Value[Axis];
	}

	return {};
}

FOperationResult FBody::SetParticleInverseMass(std::size_t ParticleIndex, double InInverseMass)
{
	if (ParticleIndex >= GetVertexCount())
	{
		return {EErrorCode::OutOfRange, "Particle index out of range"};
	}

	if (!std::isfinite(InInverseMass) || InInverseMass < 0)
	{
		return {EErrorCode::InvalidArgument, "Invalid inverseMass"};
	}

	InverseMass[ParticleIndex] = InInverseMass;

	return {};
}

FOperationResult FBody::GetMovableMass(double& OutMass) const
{
	double ScaledMass = 0;
	double MinimumInverseMass = 0;
	const FOperationResult Result = CalculateScaledMovableMass(InverseMass, ScaledMass, MinimumInverseMass);
	if (!Result)
	{
		return Result;
	}

	const double MovableMass = ScaledMass / MinimumInverseMass;
	if (!std::isfinite(MovableMass))
	{
		return {EErrorCode::OutOfRange, "Movable mass exceeds the finite range"};
	}

	OutMass = MovableMass;
	return {};
}

FOperationResult FBody::ApplyImpulse(const FVector3D& Impulse)
{
	if (!Impulse.IsFinite())
	{
		return {EErrorCode::InvalidArgument, "Expected a finite impulse vec3"};
	}

	double ScaledMass = 0;
	double MinimumInverseMass = 0;
	const FOperationResult Result = CalculateScaledMovableMass(InverseMass, ScaledMass, MinimumInverseMass);
	if (!Result || ScaledMass == 0)
	{
		return Result;
	}

	// Δv = J / M입니다. 스케일이 1 이하이면 질량 나눗셈을 먼저 적용해 미소 역질량을 보존합니다.
	const FVector3D VelocityDelta = MinimumInverseMass <= 1 ? (Impulse / ScaledMass) * MinimumInverseMass : Impulse * (MinimumInverseMass / ScaledMass);
	for (std::uint32_t ParticleIndex = 0; ParticleIndex < GetVertexCount(); ++ParticleIndex)
	{
		if (InverseMass[ParticleIndex] > 0 && !(ReadParticleVector(Velocities, ParticleIndex) + VelocityDelta).IsFinite())
		{
			return {EErrorCode::OutOfRange, "Impulse would produce a non-finite velocity"};
		}
	}

	for (std::uint32_t ParticleIndex = 0; ParticleIndex < GetVertexCount(); ++ParticleIndex)
	{
		if (InverseMass[ParticleIndex] > 0)
		{
			AddParticleVector(Velocities, ParticleIndex, VelocityDelta);
		}
	}
	return {};
}

FOperationResult FBody::SetDragTarget(std::optional<FVector3D> Target)
{
	if (Target && !Target->IsFinite())
	{
		return {EErrorCode::InvalidArgument, "Expected a finite vec3"};
	}

	DragTarget = Target;

	return {};
}

void FBody::BeginSubstep()
{
	UpdateBounds();
}

FOperationResult FBody::MoveTo(FVector3D Target)
{
	if (!Target.IsFinite())
	{
		return {EErrorCode::InvalidArgument, "Expected a finite vec3"};
	}

	UpdateBounds();

	// 몸체 중심을 목표에 맞출 공통 이동량입니다. 기준 위치와 이전 위치는 함께 이동시키지 않습니다.
	const FVector3D Translation = Target - Center;

	for (std::uint32_t ParticleIndex = 0; ParticleIndex < GetVertexCount(); ++ParticleIndex)
	{
		AddParticleVector(Positions, ParticleIndex, Translation);
	}

	UpdateBounds();

	return {};
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
