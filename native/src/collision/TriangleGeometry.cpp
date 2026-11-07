#include "collision/TriangleGeometry.h"
#include <limits>

namespace
{
	// 외적 크기의 제곱과 변 길이 제곱의 제곱은 모두 길이의 네제곱 단위입니다.
	constexpr double TriangleDegeneracyRatioSquared = 1E-28;

	std::array<double, 3> DegenerateTriangleWeights(FVector3D QueryPoint, const std::array<FVector3D, 3>& Vertices)
	{
		std::array<double, 3> Weights{};
		// 지금까지 검사한 퇴화 경계 선분 중 최소 거리 제곱입니다. 같은 거리는 이전 선분을 유지합니다.
		double BestDistanceSquared = std::numeric_limits<double>::infinity();
		// 엄격한 비교로 같은 거리에서는 AB → BC → CA 중 먼저 검사한 선분을 유지합니다.
		for (int Start = 0; Start < 3; ++Start)
		{
			const int End = (Start + 1) % 3;
			const FVector3D Segment = Vertices[End] - Vertices[Start];
			const double SegmentLengthSquared = Segment.Dot(Segment);
			// 질의 점을 선분에 투영한 [0,1] 매개변수입니다. 길이가 0인 선분은 시작점으로 처리합니다.
			const double Fraction = SegmentLengthSquared > 0 ? std::clamp((QueryPoint - Vertices[Start]).Dot(Segment) / SegmentLengthSquared, 0.0, 1.0) : 0;
			const FVector3D ProjectedPoint = Vertices[Start] + Segment * Fraction;
			const FVector3D Difference = QueryPoint - ProjectedPoint;
			const double DistanceSquared = Difference.Dot(Difference);
			if (DistanceSquared < BestDistanceSquared)
			{
				BestDistanceSquared = DistanceSquared;
				Weights = {};
				Weights[Start] = 1 - Fraction;
				Weights[End] = Fraction;
			}
		}
		return Weights;
	}

	std::array<double, 3> TriangleRegionWeights(FVector3D QueryPoint, FVector3D VertexB, FVector3D VertexC, const FVector3D& EdgeAB, const FVector3D& EdgeAC, const FVector3D& FromA)
	{
		const double ABFromA = EdgeAB.Dot(FromA);
		const double ACFromA = EdgeAC.Dot(FromA);
		if (ABFromA <= 0 && ACFromA <= 0)
		{
			return {1, 0, 0}; // 정점 A의 최근접 영역입니다.
		}
		const FVector3D FromB = QueryPoint - VertexB;
		const FVector3D FromC = QueryPoint - VertexC;
		const double ABFromB = EdgeAB.Dot(FromB);
		const double ACFromB = EdgeAC.Dot(FromB);
		const double ABFromC = EdgeAB.Dot(FromC);
		const double ACFromC = EdgeAC.Dot(FromC);
		const double RegionAB = ABFromA * ACFromB - ABFromB * ACFromA;
		const double RegionAC = ABFromC * ACFromA - ABFromA * ACFromC;
		const double RegionBC = ABFromB * ACFromC - ABFromC * ACFromB;
		if (ABFromB >= 0 && ACFromB <= ABFromB)
		{
			return {0, 1, 0};
		}
		if (RegionAB <= 0 && ABFromA >= 0 && ABFromB <= 0)
		{
			const double AlongAB = ABFromA / (ABFromA - ABFromB);
			return {1 - AlongAB, AlongAB, 0};
		}
		if (ACFromC >= 0 && ABFromC <= ACFromC)
		{
			return {0, 0, 1};
		}
		if (RegionAC <= 0 && ACFromA >= 0 && ACFromC <= 0)
		{
			const double AlongAC = ACFromA / (ACFromA - ACFromC);
			return {1 - AlongAC, 0, AlongAC};
		}
		if (RegionBC <= 0 && ACFromB - ABFromB >= 0 && ABFromC - ACFromC >= 0)
		{
			const double AlongBC = (ACFromB - ABFromB) / (ACFromB - ABFromB + ABFromC - ACFromC);
			return {0, 1 - AlongBC, AlongBC};
		}
		const double InverseRegionSum = 1 / (RegionBC + RegionAC + RegionAB);
		const double WeightB = RegionAC * InverseRegionSum;
		const double WeightC = RegionAB * InverseRegionSum;
		return {1 - WeightB - WeightC, WeightB, WeightC};
	}
} // namespace

FTriangleClosestPoint FindClosestPointOnTriangle(FVector3D QueryPoint, FVector3D VertexA, FVector3D VertexB, FVector3D VertexC)
{
	const FVector3D EdgeAB = VertexB - VertexA;
	const FVector3D EdgeAC = VertexC - VertexA;
	const FVector3D FromA = QueryPoint - VertexA;
	const FVector3D Normal = EdgeAB.Cross(EdgeAC);
	const double MaximumEdgeLengthSquared = std::max(EdgeAB.Dot(EdgeAB), EdgeAC.Dot(EdgeAC));
	const bool bDegenerate = Normal.Dot(Normal) <= TriangleDegeneracyRatioSquared * MaximumEdgeLengthSquared * MaximumEdgeLengthSquared;
	std::array<double, 3> Weights{};
	if (bDegenerate)
	{
		Weights = DegenerateTriangleWeights(QueryPoint, {VertexA, VertexB, VertexC});
	}
	else
	{
		Weights = TriangleRegionWeights(QueryPoint, VertexB, VertexC, EdgeAB, EdgeAC, FromA);
	}
	const FVector3D ClosestPoint = VertexA * Weights[0] + VertexB * Weights[1] + VertexC * Weights[2];
	const FVector3D Difference = QueryPoint - ClosestPoint;
	return {Weights, ClosestPoint, Difference.Dot(Difference)};
}
