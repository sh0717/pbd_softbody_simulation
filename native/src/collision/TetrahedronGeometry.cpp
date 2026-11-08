#include "collision/TetrahedronGeometry.h"
#include <algorithm>
#include <cmath>

void FTetrahedronGeometry::Update(FVector3D Vertex0, FVector3D Vertex1, FVector3D Vertex2, FVector3D Vertex3, double Epsilon)
{
	bValid = false;
	Origin = Vertex0;

	const FVector3D EdgeFrom0To1 = Vertex1 - Origin;
	const FVector3D EdgeFrom0To2 = Vertex2 - Origin;
	const FVector3D EdgeFrom0To3 = Vertex3 - Origin;
	const FVector3D OppositeFaceNormal = EdgeFrom0To2.Cross(EdgeFrom0To3);

	// 행렬식은 부호 있는 체적의 여섯 배입니다. 역기저 계산 전에 상대 길이 기준으로 퇴화를 거릅니다.
	const double Determinant = EdgeFrom0To1.Dot(OppositeFaceNormal);
	const double MaximumEdgeLength = std::sqrt(std::max({EdgeFrom0To1.Dot(EdgeFrom0To1), EdgeFrom0To2.Dot(EdgeFrom0To2), EdgeFrom0To3.Dot(EdgeFrom0To3)}));

	if (!std::isfinite(Determinant) || !(MaximumEdgeLength > 0) || std::abs(Determinant) <= Epsilon * MaximumEdgeLength * MaximumEdgeLength * MaximumEdgeLength)
	{
		return;
	}

	BarycentricRowU = OppositeFaceNormal * (1 / Determinant);
	BarycentricRowV = EdgeFrom0To3.Cross(EdgeFrom0To1) * (1 / Determinant);
	BarycentricRowW = EdgeFrom0To1.Cross(EdgeFrom0To2) * (1 / Determinant);
	bValid = true;
	BoundsMin = BoundsMax = Origin;

	for (const FVector3D& Position : {Vertex1, Vertex2, Vertex3})
	{
		for (EAxis Axis : AllAxes)
		{
			BoundsMin[Axis] = std::min(BoundsMin[Axis], Position[Axis]);
			BoundsMax[Axis] = std::max(BoundsMax[Axis], Position[Axis]);
		}
	}

	for (EAxis Axis : AllAxes)
	{
		BoundsMin[Axis] -= Epsilon * MaximumEdgeLength;
		BoundsMax[Axis] += Epsilon * MaximumEdgeLength;
	}
}

bool FTetrahedronGeometry::IsPointWithinBounds(FVector3D QueryPoint) const
{
	if (!bValid)
	{
		return false;
	}

	for (EAxis Axis : AllAxes)
	{
		if (QueryPoint[Axis] < BoundsMin[Axis] || QueryPoint[Axis] > BoundsMax[Axis])
		{
			return false;
		}
	}

	return true;
}

bool FTetrahedronGeometry::ContainsPoint(FVector3D QueryPoint, double Epsilon) const
{
	if (!bValid)
	{
		return false;
	}

	const FVector3D RelativePosition = QueryPoint - Origin;
	const double WeightU = BarycentricRowU.Dot(RelativePosition);
	const double WeightV = BarycentricRowV.Dot(RelativePosition);
	const double WeightW = BarycentricRowW.Dot(RelativePosition);
	return WeightU >= -Epsilon && WeightV >= -Epsilon && WeightW >= -Epsilon && 1 - WeightU - WeightV - WeightW >= -Epsilon;
}
