#include "simulation/SoftBody.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace
{
	bool AreMeshIndicesValid(const FIndices& Indices, std::size_t IndicesPerElement, std::size_t VertexCount)
	{
		if (Indices.size() % IndicesPerElement ||
			std::any_of(Indices.begin(), Indices.end(), [VertexCount](const std::uint32_t VertexIndex)
		{
			return VertexIndex >= VertexCount;
		}))
		{
			return false;
		}

		return true;
	}
} // namespace

FSoftBody::FSoftBody(std::vector<double> Vertices, FIndices Edges, FIndices Tetrahedra, FIndices Surface, FVector3D PositionOffset, std::vector<double> InverseMasses, FVector3D Velocity)
	: FBody(EBodyType::Soft, std::move(Vertices), PositionOffset, std::move(InverseMasses), Velocity)
	, EdgeIndices(std::move(Edges))
	, TetIndices(std::move(Tetrahedra))
	, SurfaceIndices(std::move(Surface))
{
	if (!InitializationResult)
	{
		return;
	}

	const std::size_t VertexCount = GetVertexCount();

	if (!AreMeshIndicesValid(EdgeIndices, 2, VertexCount) || !AreMeshIndicesValid(TetIndices, 4, VertexCount) || !AreMeshIndicesValid(SurfaceIndices, 3, VertexCount))
	{
		InitializationResult = {EErrorCode::InvalidArgument, "Invalid mesh indices"};

		return;
	}

	InitializeRestEdgeLengths();
	InitializeRestVolumesAndLimits();
	EdgeLambdas.resize(RestLengths.size());
	VolumeLambdas.resize(RestVolumes.size());
	UpdateBounds();
}

void FSoftBody::InitializeRestEdgeLengths()
{
	// 입자 XYZ 저장소를 복사하지 않고 읽습니다. 생성 시에는 기준 형상, 경계 갱신 시에는 현재 형상입니다.
	const std::vector<double>& PositionValues = GetPositions();

	// EdgeOffset은 변 번호가 아닌 인덱스 배열 오프셋이며 한 변을 처리할 때 두 칸씩 이동합니다.
	for (std::size_t EdgeOffset = 0; EdgeOffset < EdgeIndices.size(); EdgeOffset += 2)
	{
		RestLengths.push_back((ReadParticleVector(PositionValues, EdgeIndices[EdgeOffset + 1]) - ReadParticleVector(PositionValues, EdgeIndices[EdgeOffset])).Length());
	}
}

void FSoftBody::InitializeRestVolumesAndLimits()
{
	// 입자 XYZ 저장소를 복사하지 않고 읽습니다. 생성 시에는 기준 형상, 경계 갱신 시에는 현재 형상입니다.
	const std::vector<double>& PositionValues = GetPositions();

	// TetOffset부터 네 원소가 현재 사면체를 구성하며 체적·보정 한도는 사면체당 한 개씩 저장합니다.
	for (std::size_t TetOffset = 0; TetOffset < TetIndices.size(); TetOffset += 4)
	{
		RestVolumes.push_back(CalculateSignedTetrahedronVolume(PositionValues, TetIndices[TetOffset], TetIndices[TetOffset + 1], TetIndices[TetOffset + 2], TetIndices[TetOffset + 3]));

		// 사면체의 여섯 꼭짓점 쌍 중 최대 거리 제곱입니다. 제곱근을 취해 위치 보정 상한으로 저장합니다.
		double DiameterSquared = 0;

		for (int CornerA = 0; CornerA < 4; ++CornerA)
		{
			for (int CornerB = CornerA + 1; CornerB < 4; ++CornerB)
			{
				const FVector3D CornerDisplacement = ReadParticleVector(PositionValues, TetIndices[TetOffset + CornerA]) - ReadParticleVector(PositionValues, TetIndices[TetOffset + CornerB]);
				DiameterSquared = std::max(DiameterSquared, CornerDisplacement.Dot(CornerDisplacement));
			}
		}

		TetLimits.push_back(std::sqrt(DiameterSquared));
	}
}

void FSoftBody::UpdateBounds()
{
	// 모든 현재 입자를 축별로 축약하여 얻을 상자 경계의 최솟값입니다.
	FVector3D Minimum;

	// 같은 입자 순회에서 누적할 상자 경계의 최댓값입니다. 이전 프레임 경계는 재사용하지 않습니다.
	FVector3D Maximum;
	Minimum.Fill(std::numeric_limits<double>::infinity());
	Maximum.Fill(-std::numeric_limits<double>::infinity());

	// 입자 XYZ 저장소를 복사하지 않고 읽습니다. 생성 시에는 기준 형상, 경계 갱신 시에는 현재 형상입니다.
	const std::vector<double>& PositionValues = GetPositions();

	for (std::size_t ComponentIndex = 0; ComponentIndex < PositionValues.size(); ++ComponentIndex)
	{
		const EAxis Axis = static_cast<EAxis>(ComponentIndex % 3);
		Minimum[Axis] = std::min(Minimum[Axis], PositionValues[ComponentIndex]);
		Maximum[Axis] = std::max(Maximum[Axis], PositionValues[ComponentIndex]);
	}

	// 최소·최대 경계의 중점입니다. 이 점을 중심으로 모든 입자를 포함하는 구 반지름을 계산합니다.
	const FVector3D BoundsCenter = (Minimum + Maximum) * 0.5;

	// 경계 중심에서 가장 먼 입자까지의 거리 제곱을 누적하여 광역 충돌용 반지름을 구합니다.
	double RadiusSquared = 0;

	for (std::uint32_t VertexIndex = 0; VertexIndex < GetVertexCount(); ++VertexIndex)
	{
		const FVector3D DisplacementFromCenter = ReadParticleVector(PositionValues, VertexIndex) - BoundsCenter;
		RadiusSquared = std::max(RadiusSquared, DisplacementFromCenter.Dot(DisplacementFromCenter));
	}

	PublishBounds(BoundsCenter, Minimum, Maximum, std::sqrt(RadiusSquared));
}

void FSoftBody::ResetLambdas()
{
	// λ는 한 서브스텝의 제약 반응입니다. 과거 서브스텝의 값을 새 위치 예측에 섞지 않습니다.
	std::fill(EdgeLambdas.begin(), EdgeLambdas.end(), 0);
	std::fill(VolumeLambdas.begin(), VolumeLambdas.end(), 0);
}

void FSoftBody::BeginSubstep()
{
	// 제약 반복마다 초기화하면 XPBD의 누적항이 사라지므로 서브스텝 진입 시 한 번만 초기화합니다.
	ResetLambdas();
	UpdateBounds();
}

void FSoftBody::Reset()
{
	FBody::Reset();
	ResetLambdas();
}
