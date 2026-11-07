#include "collision/SurfaceBvh.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <utility>

namespace
{
	constexpr int SurfaceBvhLeafTriangleCount = 8;

	double SquaredDistanceToBounds(const FVector3D& BoundsMin, const FVector3D& BoundsMax, const FVector3D& QueryPoint)
	{
		double DistanceSquared = 0;
		for (EAxis Axis : AllAxes)
		{
			const double AxisDistance = std::max({BoundsMin[Axis] - QueryPoint[Axis], 0.0, QueryPoint[Axis] - BoundsMax[Axis]});
			DistanceSquared += AxisDistance * AxisDistance;
		}
		return DistanceSquared;
	}

} // namespace

FSurfaceBvh::FSurfaceBvh(FSurfaceTopology Surface, const std::vector<double>& RestPositions)
	: SurfaceTriangleVertexIndices(std::move(Surface.Indices))
	, SurfaceVertexIndices(std::move(Surface.Vertices))
{
	TriangleIndicesByBvhOrder.resize(SurfaceTriangleVertexIndices.size() / 3);
	std::iota(TriangleIndicesByBvhOrder.begin(), TriangleIndicesByBvhOrder.end(), 0);
	std::vector<FVector3D> TriangleCenters(TriangleIndicesByBvhOrder.size());
	for (std::size_t TriangleIndex = 0; TriangleIndex < TriangleCenters.size(); ++TriangleIndex)
	{
		const std::size_t TriangleOffset = TriangleIndex * 3;
		TriangleCenters[TriangleIndex] = (ReadParticleVector(RestPositions, SurfaceTriangleVertexIndices[TriangleOffset]) + ReadParticleVector(RestPositions, SurfaceTriangleVertexIndices[TriangleOffset + 1]) + ReadParticleVector(RestPositions, SurfaceTriangleVertexIndices[TriangleOffset + 2])) * (1.0 / 3);
	}
	Build(0, static_cast<int>(TriangleIndicesByBvhOrder.size()), TriangleCenters);
}

int FSurfaceBvh::Build(int StartOrderIndex, int EndOrderIndex, const std::vector<FVector3D>& TriangleCenters)
{
	if (StartOrderIndex == EndOrderIndex)
	{
		return -1;
	}
	const int NodeIndex = static_cast<int>(SurfaceBvhNodes.size());
	SurfaceBvhNodes.push_back({{}, {}, StartOrderIndex, EndOrderIndex, -1, -1});
	if (EndOrderIndex - StartOrderIndex <= SurfaceBvhLeafTriangleCount)
	{
		return NodeIndex;
	}
	FVector3D BoundsMin{};
	FVector3D BoundsMax{};
	BoundsMin.Fill(std::numeric_limits<double>::infinity());
	BoundsMax.Fill(-std::numeric_limits<double>::infinity());
	for (int OrderIndex = StartOrderIndex; OrderIndex < EndOrderIndex; ++OrderIndex)
	{
		const FVector3D& Center = TriangleCenters[TriangleIndicesByBvhOrder[OrderIndex]];
		for (EAxis Axis : AllAxes)
		{
			BoundsMin[Axis] = std::min(BoundsMin[Axis], Center[Axis]);
			BoundsMax[Axis] = std::max(BoundsMax[Axis], Center[Axis]);
		}
	}
	const FVector3D Range = BoundsMax - BoundsMin;
	EAxis SplitAxis = EAxis::X;
	for (EAxis Axis : {EAxis::Y, EAxis::Z})
	{
		if (Range[Axis] > Range[SplitAxis])
		{
			SplitAxis = Axis;
		}
	}
	std::stable_sort(TriangleIndicesByBvhOrder.begin() + StartOrderIndex, TriangleIndicesByBvhOrder.begin() + EndOrderIndex, [&TriangleCenters, SplitAxis](const std::uint32_t LeftTriangleIndex, const std::uint32_t RightTriangleIndex)
	{
		return TriangleCenters[LeftTriangleIndex][SplitAxis] < TriangleCenters[RightTriangleIndex][SplitAxis];
	});
	const int Middle = (StartOrderIndex + EndOrderIndex) / 2;
	const int LeftChildNodeIndex = Build(StartOrderIndex, Middle, TriangleCenters);
	const int RightChildNodeIndex = Build(Middle, EndOrderIndex, TriangleCenters);
	SurfaceBvhNodes[NodeIndex].LeftChildNodeIndex = LeftChildNodeIndex;
	SurfaceBvhNodes[NodeIndex].RightChildNodeIndex = RightChildNodeIndex;
	return NodeIndex;
}

void FSurfaceBvh::Refit(const std::vector<double>& Positions)
{
	RefitNode(SurfaceBvhNodes.empty() ? -1 : 0, Positions);
}

void FSurfaceBvh::RefitNode(int NodeIndex, const std::vector<double>& Positions)
{
	if (NodeIndex < 0)
	{
		return;
	}
	FSurfaceBvhNode& Node = SurfaceBvhNodes[NodeIndex];
	if (Node.LeftChildNodeIndex >= 0)
	{
		RefitNode(Node.LeftChildNodeIndex, Positions);
		RefitNode(Node.RightChildNodeIndex, Positions);
		for (EAxis Axis : AllAxes)
		{
			Node.BoundsMin[Axis] = std::min(SurfaceBvhNodes[Node.LeftChildNodeIndex].BoundsMin[Axis], SurfaceBvhNodes[Node.RightChildNodeIndex].BoundsMin[Axis]);
			Node.BoundsMax[Axis] = std::max(SurfaceBvhNodes[Node.LeftChildNodeIndex].BoundsMax[Axis], SurfaceBvhNodes[Node.RightChildNodeIndex].BoundsMax[Axis]);
		}
	}
	else
	{
		Node.BoundsMin.Fill(std::numeric_limits<double>::infinity());
		Node.BoundsMax.Fill(-std::numeric_limits<double>::infinity());
		for (int OrderIndex = Node.FirstTriangleOrderIndex; OrderIndex < Node.EndTriangleOrderIndex; ++OrderIndex)
		{
			for (int TriangleCornerIndex = 0; TriangleCornerIndex < 3; ++TriangleCornerIndex)
			{
				const FVector3D VertexPosition = ReadParticleVector(Positions, SurfaceTriangleVertexIndices[TriangleIndicesByBvhOrder[OrderIndex] * 3 + TriangleCornerIndex]);
				for (EAxis Axis : AllAxes)
				{
					Node.BoundsMin[Axis] = std::min(Node.BoundsMin[Axis], VertexPosition[Axis]);
					Node.BoundsMax[Axis] = std::max(Node.BoundsMax[Axis], VertexPosition[Axis]);
				}
			}
		}
	}
}

std::optional<FSurfaceClosestPoint> FSurfaceBvh::FindNearestPoint(FVector3D QueryPoint, const std::vector<double>& Positions, double& InOutTriangleTests)
{
	if (SurfaceBvhNodes.empty())
	{
		return std::nullopt;
	}
	TraversalStack.clear();
	TraversalStack.push_back(0);
	FTriangleClosestPoint NearestPoint;
	NearestPoint.DistanceSquared = std::numeric_limits<double>::infinity();
	std::uint32_t NearestTriangleIndex = 0;
	bool bHasSurfacePoint = false;
	while (!TraversalStack.empty())
	{
		const FSurfaceBvhNode& Node = SurfaceBvhNodes[TraversalStack.back()];
		TraversalStack.pop_back();
		if (SquaredDistanceToBounds(Node.BoundsMin, Node.BoundsMax, QueryPoint) > NearestPoint.DistanceSquared)
		{
			continue;
		}
		if (Node.LeftChildNodeIndex >= 0)
		{
			const FSurfaceBvhNode& LeftNode = SurfaceBvhNodes[Node.LeftChildNodeIndex];
			const FSurfaceBvhNode& RightNode = SurfaceBvhNodes[Node.RightChildNodeIndex];
			// 거리가 같으면 오른쪽을 먼저 꺼냅니다. 아래의 엄격한 최소값 갱신과 함께
			// 기존 표면 접촉의 동률 선택 순서를 유지합니다.
			if (SquaredDistanceToBounds(LeftNode.BoundsMin, LeftNode.BoundsMax, QueryPoint) < SquaredDistanceToBounds(RightNode.BoundsMin, RightNode.BoundsMax, QueryPoint))
			{
				TraversalStack.push_back(Node.RightChildNodeIndex);
				TraversalStack.push_back(Node.LeftChildNodeIndex);
			}
			else
			{
				TraversalStack.push_back(Node.LeftChildNodeIndex);
				TraversalStack.push_back(Node.RightChildNodeIndex);
			}
		}
		else
		{
			for (int OrderIndex = Node.FirstTriangleOrderIndex; OrderIndex < Node.EndTriangleOrderIndex; ++OrderIndex)
			{
				const std::uint32_t TriangleIndex = TriangleIndicesByBvhOrder[OrderIndex];
				const std::uint32_t TriangleOffset = TriangleIndex * 3;
				const FTriangleClosestPoint ClosestPoint = FindClosestPointOnTriangle(QueryPoint, ReadParticleVector(Positions, SurfaceTriangleVertexIndices[TriangleOffset]), ReadParticleVector(Positions, SurfaceTriangleVertexIndices[TriangleOffset + 1]), ReadParticleVector(Positions, SurfaceTriangleVertexIndices[TriangleOffset + 2]));
				++InOutTriangleTests;
				if (ClosestPoint.DistanceSquared < NearestPoint.DistanceSquared)
				{
					NearestPoint = ClosestPoint;
					NearestTriangleIndex = TriangleIndex;
					bHasSurfacePoint = true;
				}
			}
		}
	}
	if (!bHasSurfacePoint)
	{
		return std::nullopt;
	}
	return FSurfaceClosestPoint{{SurfaceTriangleVertexIndices[NearestTriangleIndex * 3], SurfaceTriangleVertexIndices[NearestTriangleIndex * 3 + 1], SurfaceTriangleVertexIndices[NearestTriangleIndex * 3 + 2]}, NearestPoint};
}

FSurfaceTopology FSurfaceBvh::GetTopology() const
{
	return {SurfaceTriangleVertexIndices, SurfaceVertexIndices};
}
