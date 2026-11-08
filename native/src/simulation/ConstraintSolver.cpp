#include "simulation/ConstraintSolver.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace
{
	// 길이가 붕괴한 변은 기준 형상의 방향을 사용하여 0으로 나누지 않습니다.
	constexpr double MinimumDirectionLength = 1E-15;

	// C=6(V-V0)의 기울기 제곱합은 물리 체적 기준의 36배이므로 퇴화 판정 기준도 같은 비율로 맞춥니다.
	constexpr double MinimumWeightedGradientSquared = 36E-300;

	/** 외부 경계에서 검증한 시간·유연도 계약이 깨지면 배포 빌드에서도 계산 전에 종료합니다. */
	void RequireValidConstraintInput(const FSettings& Settings, const double SubstepSeconds)
	{
		const double SquaredStep = SubstepSeconds * SubstepSeconds;

		if (!(SubstepSeconds > 0) || !std::isfinite(SubstepSeconds) || !std::isfinite(1 / SquaredStep) || (Settings.bXpbd && (!std::isfinite(Settings.EdgeCompliance / SquaredStep) || !std::isfinite(36 * (Settings.VolumeCompliance / SquaredStep)))))
		{
			std::fputs("Invalid constraint timestep or compliance scaling\n", stderr);
			std::abort();
		}
	}
} // namespace

void FConstraintSolver::SolveEdges(FSoftBody& InOutBody, const double SubstepSeconds, const FSettings& Settings)
{
	RequireValidConstraintInput(Settings, SubstepSeconds);

	// 거리 제약 분모에 사용할 유연도/h²입니다. PBD 경로에서는 0으로 두어 해당 항을 제거합니다.
	double ScaledCompliance = 0;

	if (Settings.bXpbd)
	{
		ScaledCompliance = Settings.EdgeCompliance / (SubstepSeconds * SubstepSeconds);
	}

	// 한 번의 투영에 적용할 보정 배율입니다. XPBD는 1, PBD는 사용자가 지정한 강성을 사용합니다.
	const double ProjectionStiffness = Settings.bXpbd ? 1 : Settings.PbdStiffness;

	for (std::size_t EdgeIndex = 0; EdgeIndex < InOutBody.GetRestLengths().size(); ++EdgeIndex)
	{
		ProjectEdge(InOutBody, EdgeIndex, ScaledCompliance, ProjectionStiffness);
	}
}

void FConstraintSolver::ProjectEdge(FSoftBody& InOutBody, const std::size_t EdgeIndex, const double ScaledCompliance, const double ProjectionStiffness)
{
	// 변의 양 끝 입자 번호입니다. 두 인덱스는 같은 변의 기준 길이·누적 승수와 대응합니다.
	const std::uint32_t VertexA = InOutBody.GetEdgeIndices()[EdgeIndex * 2];
	const std::uint32_t VertexB = InOutBody.GetEdgeIndices()[EdgeIndex * 2 + 1];
	const double InverseMassA = InOutBody.GetInverseMass()[VertexA];
	const double InverseMassB = InOutBody.GetInverseMass()[VertexB];

	// 두 끝점의 이동 가능성을 합친 값입니다. 길이 제약 승수의 분모에도 같은 합을 사용합니다.
	const double InverseMassSum = InverseMassA + InverseMassB;

	if (InverseMassSum == 0)
	{
		return;
	}

	// 처음에는 A에서 B로 향하는 변 벡터이며 아래에서 현재 또는 기준 형상의 단위 방향으로 바꿉니다.
	FVector3D EdgeNormal = ReadParticleVector(InOutBody.GetPositions(), VertexB) - ReadParticleVector(InOutBody.GetPositions(), VertexA);
	const double EdgeLength = EdgeNormal.Length();

	// 현재 변과 같은 인덱스의 기준 길이입니다. 붕괴 방향 복원과 거리 오차 계산에 함께 사용합니다.
	const double RestLength = InOutBody.GetRestLengths()[EdgeIndex];

	if (EdgeLength > MinimumDirectionLength)
	{
		EdgeNormal = EdgeNormal * (1 / EdgeLength);
	}
	else if (RestLength <= MinimumDirectionLength)
	{
		return;
	}
	else
	{
		EdgeNormal = (ReadParticleVector(InOutBody.GetRestPositions(), VertexB) - ReadParticleVector(InOutBody.GetRestPositions(), VertexA)) * (1 / RestLength);
	}

	// C=현재 길이-기준 길이이며, EdgeLambdas[e]는 이번 서브스텝에서 이 변에 누적한 λ입니다.
	// 첫 투영의 λ=0이면 Δλ=-C/(wA+wB+α/h²)입니다. 이후에는 -α/h²*λ로 이전 보정을 반영합니다.
	// DeltaLambda는 이번 투영의 변화량이며 음수일 수도 있습니다. PBD는 α/h²=0이므로 누적값을 읽어도 보정에 영향을 주지 않습니다.
	const double DeltaLambda = (-(EdgeLength - RestLength) - ScaledCompliance * InOutBody.GetEdgeLambdas()[EdgeIndex]) / (InverseMassSum + ScaledCompliance) * ProjectionStiffness;

	// 누적 λ를 갱신하되 입자는 이번 Δλ만큼만 움직입니다. 누적 λ로 다시 움직이면 이전 보정을 중복 적용합니다.
	InOutBody.EdgeLambdas[EdgeIndex] += DeltaLambda;
	AddParticleVector(InOutBody.Positions, VertexA, EdgeNormal * (-InverseMassA * DeltaLambda));
	AddParticleVector(InOutBody.Positions, VertexB, EdgeNormal * (InverseMassB * DeltaLambda));
}

void FConstraintSolver::SolveVolumes(FSoftBody& InOutBody, const double SubstepSeconds, const FSettings& Settings)
{
	RequireValidConstraintInput(Settings, SubstepSeconds);

	// C=6(V-V0)이므로 α도 6²배로 환산합니다. 설정은 물리 체적 기준이며 기존 재질의 유연성을 유지합니다.
	// 먼저 h²로 나눈 뒤 36을 곱해, 큰 유연도와 큰 시간 간격 조합에서 불필요한 중간 오버플로를 피합니다.
	double ScaledCompliance = 0;

	if (Settings.bXpbd)
	{
		ScaledCompliance = 36 * (Settings.VolumeCompliance / (SubstepSeconds * SubstepSeconds));
	}

	// 한 번의 투영에 적용할 보정 배율입니다. XPBD는 1, PBD는 사용자가 지정한 강성을 사용합니다.
	const double ProjectionStiffness = Settings.bXpbd ? 1 : Settings.PbdStiffness;

	for (std::size_t TetIndex = 0; TetIndex < InOutBody.GetRestVolumes().size(); ++TetIndex)
	{
		ProjectVolume(InOutBody, TetIndex, ScaledCompliance, ProjectionStiffness);
	}
}

void FConstraintSolver::ProjectVolume(FSoftBody& InOutBody, const std::size_t TetIndex, const double ScaledCompliance, const double ProjectionStiffness)
{
	// 현재 사면체의 네 입자 번호이며 VolumeGradients와 같은 꼭짓점 순서를 사용합니다.
	const std::array<std::uint32_t, 4> TetVertices{InOutBody.GetTetIndices()[TetIndex * 4], InOutBody.GetTetIndices()[TetIndex * 4 + 1], InOutBody.GetTetIndices()[TetIndex * 4 + 2], InOutBody.GetTetIndices()[TetIndex * 4 + 3]};

	const FVector3D Origin = ReadParticleVector(InOutBody.GetPositions(), TetVertices[0]);
	const FVector3D EdgeFrom0To1 = ReadParticleVector(InOutBody.GetPositions(), TetVertices[1]) - Origin;
	const FVector3D EdgeFrom0To2 = ReadParticleVector(InOutBody.GetPositions(), TetVertices[2]) - Origin;
	const FVector3D EdgeFrom0To3 = ReadParticleVector(InOutBody.GetPositions(), TetVertices[3]) - Origin;

	// C=6(V-V0)의 위치 기울기입니다. 물리 체적 V의 기울기와 달리 외적 결과를 6으로 나누지 않습니다.
	// 모든 기울기는 입자를 움직이기 전의 같은 형상에서 구하며, 역질량과 Δλ를 곱해 각 입자 보정에 사용합니다.
	std::array<FVector3D, 4> VolumeGradients{};
	VolumeGradients[1] = EdgeFrom0To2.Cross(EdgeFrom0To3);
	VolumeGradients[2] = EdgeFrom0To3.Cross(EdgeFrom0To1);
	VolumeGradients[3] = EdgeFrom0To1.Cross(EdgeFrom0To2);
	VolumeGradients[0] = (VolumeGradients[1] + VolumeGradients[2] + VolumeGradients[3]) * (-1);

	// 삼중곱은 현재 부호 있는 체적의 6배입니다. RestVolumes는 물리 체적 V0이므로 같은 비율로 맞춥니다.
	const double VolumeError = EdgeFrom0To1.Dot(VolumeGradients[1]) - 6 * InOutBody.GetRestVolumes()[TetIndex];

	// Σ(w_i*|∇C_i|²)입니다. 체적 제약 승수의 분모에 사용할 유효 역질량 항을 누적합니다.
	double WeightedGradientSquared = 0;

	// max(w_i²*|∇C_i|²)입니다. 승수 증가량으로 생길 최대 입자 이동량을 제한하는 데 사용합니다.
	double MaximumWeightedGradientSquared = 0;

	for (int CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
	{
		const double InverseMass = InOutBody.GetInverseMass()[TetVertices[CornerIndex]];
		const double GradientSquared = VolumeGradients[CornerIndex].Dot(VolumeGradients[CornerIndex]);
		WeightedGradientSquared += InverseMass * GradientSquared;
		MaximumWeightedGradientSquared = std::max(MaximumWeightedGradientSquared, InverseMass * InverseMass * GradientSquared);
	}

	if (WeightedGradientSquared <= MinimumWeightedGradientSquared)
	{
		return;
	}

	// VolumeLambdas[t]는 C=6(V-V0)에 대한 현재 서브스텝의 누적 λ이며 이전 프레임의 값을 재사용하지 않습니다.
	// Δλ=(-C-α/h²*λ)/(Σw|∇C|²+α/h²)로 이번 보정만 구합니다. α/h²*λ는 이전 투영이 만든 탄성 반응입니다.
	double DeltaLambda = (-VolumeError - ScaledCompliance * InOutBody.GetVolumeLambdas()[TetIndex]) / (WeightedGradientSquared + ScaledCompliance) * ProjectionStiffness;

	// 현재 사면체의 기준 최대 변 길이로 정한 한 번의 입자 이동 거리 상한입니다.
	const double MaximumCorrection = InOutBody.GetTetLimits()[TetIndex];

	if (DeltaLambda * DeltaLambda * MaximumWeightedGradientSquared > MaximumCorrection * MaximumCorrection)
	{
		DeltaLambda *= MaximumCorrection / (std::abs(DeltaLambda) * std::sqrt(MaximumWeightedGradientSquared));
	}

	// 실제 적용할 보정량으로 제한한 뒤 λ를 누적합니다. 제한 전 Δλ를 저장하면 누적값과 실제 입자 이동이 어긋납니다.
	InOutBody.VolumeLambdas[TetIndex] += DeltaLambda;

	for (int CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
	{
		// Δx_i=w_i*∇C_i*Δλ입니다. 전체 누적 λ가 아니라 방금 구한 변화량만 반영합니다.
		AddParticleVector(InOutBody.Positions, TetVertices[CornerIndex], VolumeGradients[CornerIndex] * (InOutBody.GetInverseMass()[TetVertices[CornerIndex]] * DeltaLambda));
	}
}
