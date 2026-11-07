#include "core/Settings.h"
#include <cmath>
#include <initializer_list>
#include <stdexcept>

void FSettings::ValidateSubstep(double SubstepSeconds) const
{
	const double SquaredStep = SubstepSeconds * SubstepSeconds;
	if (!(SubstepSeconds > 0) || !std::isfinite(SubstepSeconds) || !std::isfinite(1 / SquaredStep))
	{
		throw std::range_error("Timestep too small for finite constraint scaling");
	}
	// 부피 솔버는 C=6(V-V0)을 사용하므로 실제 분모에 들어갈 유연도도 36배까지 검증합니다.
	if (bXpbd && (!std::isfinite(EdgeCompliance / SquaredStep) || !std::isfinite(36 * (VolumeCompliance / SquaredStep))))
	{
		throw std::range_error("XPBD compliance scaling must be finite");
	}
}

void FSettings::Validate(double StepSeconds) const
{
	if (!std::isfinite(StepSeconds) || StepSeconds < 0)
	{
		throw std::range_error("dt must be finite and nonnegative");
	}
	if (Substeps < 1 || Iterations < 1)
	{
		throw std::range_error("substeps and solverIterations must be positive integers");
	}
	for (double Value : {EdgeCompliance, VolumeCompliance, Damping})
	{
		if (!std::isfinite(Value) || Value < 0)
		{
			throw std::range_error("Compliance and damping must be nonnegative");
		}
	}
	if (!std::isfinite(Bounds) || Bounds <= 0)
	{
		throw std::range_error("bounds must be positive");
	}
	if (!std::isfinite(Restitution) || Restitution < 0 || Restitution > 1)
	{
		throw std::range_error("Invalid restitution");
	}
	if (!std::isfinite(PbdStiffness) || PbdStiffness < 0 || PbdStiffness > 1)
	{
		throw std::range_error("Invalid PBD stiffness");
	}
	if (!Gravity.IsFinite())
	{
		throw std::range_error("Invalid gravity");
	}
}
