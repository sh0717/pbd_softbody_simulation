#include "simulation/TimeIntegrator.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace
{
	// 목표 변위로부터 드래그 속도를 구하는 이득입니다(1/초).
	constexpr double SoftDragGainPerSecond = 0.4;
	constexpr double BallDragGainPerSecond = 0.3;
} // namespace

void FTimeIntegrator::Predict(FBody& InOutBody, const double SubstepSeconds, const FSettings& Settings)
{
	std::copy(InOutBody.GetPositions().begin(), InOutBody.GetPositions().end(), InOutBody.PreviousPositions.begin());

	InOutBody.BeginSubstep();
	// 중심과 목표 사이 변위를 드래그 속도로 바꾸는 1/초 계수입니다. 연체와 구체의 기존 값을 구분합니다.
	const double DragGainPerSecond = InOutBody.IsSoft() ? SoftDragGainPerSecond : BallDragGainPerSecond;
	// 이번 서브스텝에서 중력 반영 후 속도에 곱할 무차원 감쇠 배율입니다.
	const double VelocityAttenuation = std::exp(-Settings.Damping * SubstepSeconds);

	for (std::uint32_t ParticleIndex = 0; ParticleIndex < InOutBody.GetVertexCount(); ++ParticleIndex)
	{
		PredictParticle(InOutBody, ParticleIndex, SubstepSeconds, DragGainPerSecond, VelocityAttenuation, Settings);
	}

	InOutBody.SavePreSolveVelocities();
}

void FTimeIntegrator::PredictParticle(FBody& InOutBody, const std::uint32_t ParticleIndex, const double SubstepSeconds, const double DragGainPerSecond, const double VelocityAttenuation, const FSettings& Settings)
{
	// 현재 입자의 XYZ 첫 성분 오프셋입니다. 고정 입자는 이 세 성분의 속도만 0으로 만듭니다.
	const std::size_t ParticleOffset = std::size_t(ParticleIndex) * 3;
	if (!InOutBody.GetInverseMass()[ParticleIndex])
	{
		for (EAxis Axis : AllAxes)
		{
			InOutBody.Velocities[ParticleOffset + ToIndex(Axis)] = 0;
		}
		return;
	}
	for (EAxis Axis : AllAxes)
	{
		// 입자 번호와 축을 합쳐 위치·속도 배열의 같은 성분을 가리키는 인덱스입니다.
		const std::size_t ComponentIndex = ParticleOffset + ToIndex(Axis);
		// 중심 읽기와 축별 저장 시점을 기존 순서에 유지합니다.
		if (InOutBody.GetDragTarget())
		{
			InOutBody.Velocities[ComponentIndex] = DragGainPerSecond * ((*InOutBody.GetDragTarget())[Axis] - InOutBody.GetCenter()[Axis]);
		}
		else
		{
			InOutBody.Velocities[ComponentIndex] = (InOutBody.GetVelocities()[ComponentIndex] + Settings.Gravity[Axis] * SubstepSeconds) * VelocityAttenuation;
		}
		InOutBody.Positions[ComponentIndex] += SubstepSeconds * InOutBody.GetVelocities()[ComponentIndex];
	}
}

void FTimeIntegrator::UpdateVelocities(FBody& InOutBody, const double SubstepSeconds)
{
	for (std::size_t ComponentIndex = 0; ComponentIndex < InOutBody.GetPositions().size(); ++ComponentIndex)
	{
		if (InOutBody.GetInverseMass()[ComponentIndex / 3] > 0)
		{
			// 제약으로 보정된 위치의 변화량을 서브스텝 시간으로 나누어 속도를 복원합니다.
			InOutBody.Velocities[ComponentIndex] = (InOutBody.GetPositions()[ComponentIndex] - InOutBody.GetPreviousPositions()[ComponentIndex]) / SubstepSeconds;
		}
		else
		{
			// 역질량이 0인 고정 입자는 움직임을 만들지 않도록 속도를 0으로 유지합니다.
			InOutBody.Velocities[ComponentIndex] = 0;
		}
	}

	InOutBody.UpdateBounds();
}
