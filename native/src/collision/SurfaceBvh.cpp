#include "collision/SurfaceBvh.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <utility>

namespace
{
	// 잎 하나에서 직접 검사할 삼각형 수의 상한입니다.
	constexpr int MaxTrianglesPerLeaf = 8;
} // namespace

double FSurfaceBvh::FNode::GetSquaredDistanceToBounds(FVector3D QueryPoint) const
{
	double DistanceSquared = 0;

	for (EAxis Axis : AllAxes)
	{
		// 축 구간의 안쪽은 0, 바깥쪽은 가장 가까운 끝점까지의 거리입니다.
		const double AxisDistance = std::max({BoundsMin[Axis] - QueryPoint[Axis], 0.0, QueryPoint[Axis] - BoundsMax[Axis]});
		DistanceSquared += AxisDistance * AxisDistance;
	}

	return DistanceSquared;
}

FSurfaceBvh::FSurfaceBvh(FSurfaceTopology Surface, const std::vector<double>& RestPositions)
	: Topology(std::move(Surface))
{
	BuildTree(RestPositions);
}

void FSurfaceBvh::BuildTree(const std::vector<double>& RestPositions)
{
	// 원본 면 순서는 보존하고 BVH만 재정렬할 삼각형 번호의 순열을 만듭니다.
	TriangleOrder.resize(GetTriangleCount());
	std::iota(TriangleOrder.begin(), TriangleOrder.end(), 0);

	std::vector<FVector3D> RestTriangleCenters(TriangleOrder.size());

	for (std::size_t TriangleIndex = 0; TriangleIndex < RestTriangleCenters.size(); ++TriangleIndex)
	{
		const std::array<std::uint32_t, 3> VertexIndices = GetTriangleVertexIndices(static_cast<std::uint32_t>(TriangleIndex));
		RestTriangleCenters[TriangleIndex] = (ReadParticleVector(RestPositions, VertexIndices[0]) + ReadParticleVector(RestPositions, VertexIndices[1]) + ReadParticleVector(RestPositions, VertexIndices[2])) * (1.0 / 3);
	}

	BuildNode(0, static_cast<int>(TriangleOrder.size()), RestTriangleCenters);
}

int FSurfaceBvh::BuildNode(int FirstTriangleOrderIndex, int EndTriangleOrderIndex, const std::vector<FVector3D>& RestTriangleCenters)
{
	if (FirstTriangleOrderIndex == EndTriangleOrderIndex)
	{
		return -1;
	}

	const int NodeIndex = static_cast<int>(Nodes.size());
	Nodes.push_back({{}, {}, FirstTriangleOrderIndex, EndTriangleOrderIndex, -1, -1});

	if (EndTriangleOrderIndex - FirstTriangleOrderIndex <= MaxTrianglesPerLeaf)
	{
		return NodeIndex;
	}

	const EAxis SplitAxis = SelectSplitAxis(FirstTriangleOrderIndex, EndTriangleOrderIndex, RestTriangleCenters);

	// 중심이 같은 삼각형은 기존 순서를 보존해야 같은 거리의 접촉 선택도 유지됩니다.
	std::stable_sort(TriangleOrder.begin() + FirstTriangleOrderIndex, TriangleOrder.begin() + EndTriangleOrderIndex, [&RestTriangleCenters, SplitAxis](const std::uint32_t LeftTriangleIndex, const std::uint32_t RightTriangleIndex)
	{
		return RestTriangleCenters[LeftTriangleIndex][SplitAxis] < RestTriangleCenters[RightTriangleIndex][SplitAxis];
	});

	// 좌표의 중간값이 아니라 정렬한 삼각형 개수의 절반으로 나눠 트리 깊이를 제한합니다.
	const int MiddleOrderIndex = (FirstTriangleOrderIndex + EndTriangleOrderIndex) / 2;
	const int LeftChildNodeIndex = BuildNode(FirstTriangleOrderIndex, MiddleOrderIndex, RestTriangleCenters);
	const int RightChildNodeIndex = BuildNode(MiddleOrderIndex, EndTriangleOrderIndex, RestTriangleCenters);

	// 재귀의 push_back이 노드 저장소를 재할당할 수 있으므로 참조를 보관하지 않고 번호로 다시 접근합니다.
	Nodes[NodeIndex].LeftChildNodeIndex = LeftChildNodeIndex;
	Nodes[NodeIndex].RightChildNodeIndex = RightChildNodeIndex;

	return NodeIndex;
}

EAxis FSurfaceBvh::SelectSplitAxis(int FirstTriangleOrderIndex, int EndTriangleOrderIndex, const std::vector<FVector3D>& RestTriangleCenters) const
{
	// 분할에만 사용하는 중심 분포입니다. 실제 삼각형을 감싸는 노드 경계와 구분합니다.
	FVector3D CenterBoundsMin{};
	FVector3D CenterBoundsMax{};
	CenterBoundsMin.Fill(std::numeric_limits<double>::infinity());
	CenterBoundsMax.Fill(-std::numeric_limits<double>::infinity());

	for (int OrderIndex = FirstTriangleOrderIndex; OrderIndex < EndTriangleOrderIndex; ++OrderIndex)
	{
		const FVector3D& Center = RestTriangleCenters[TriangleOrder[OrderIndex]];

		for (EAxis Axis : AllAxes)
		{
			CenterBoundsMin[Axis] = std::min(CenterBoundsMin[Axis], Center[Axis]);
			CenterBoundsMax[Axis] = std::max(CenterBoundsMax[Axis], Center[Axis]);
		}
	}

	const FVector3D CenterExtent = CenterBoundsMax - CenterBoundsMin;
	EAxis SplitAxis = EAxis::X;

	for (EAxis Axis : {EAxis::Y, EAxis::Z})
	{
		if (CenterExtent[Axis] > CenterExtent[SplitAxis])
		{
			SplitAxis = Axis;
		}
	}

	return SplitAxis;
}

void FSurfaceBvh::Refit(const std::vector<double>& Positions)
{
	if (!IsEmpty())
	{
		RefitNode(0, Positions);
	}
}

void FSurfaceBvh::RefitNode(int NodeIndex, const std::vector<double>& Positions)
{
	FNode& Node = Nodes[NodeIndex];

	if (Node.IsLeaf())
	{
		RefitLeafBounds(Node, Positions);

		return;
	}

	// 잎에서 위로 올라가며 합칩니다. Refit은 노드를 추가하지 않으므로 Node 참조가 유지됩니다.
	RefitNode(Node.LeftChildNodeIndex, Positions);
	RefitNode(Node.RightChildNodeIndex, Positions);

	const FNode& LeftChild = Nodes[Node.LeftChildNodeIndex];
	const FNode& RightChild = Nodes[Node.RightChildNodeIndex];

	for (EAxis Axis : AllAxes)
	{
		Node.BoundsMin[Axis] = std::min(LeftChild.BoundsMin[Axis], RightChild.BoundsMin[Axis]);
		Node.BoundsMax[Axis] = std::max(LeftChild.BoundsMax[Axis], RightChild.BoundsMax[Axis]);
	}
}

void FSurfaceBvh::RefitLeafBounds(FNode& InOutNode, const std::vector<double>& Positions) const
{
	InOutNode.BoundsMin.Fill(std::numeric_limits<double>::infinity());
	InOutNode.BoundsMax.Fill(-std::numeric_limits<double>::infinity());

	for (int OrderIndex = InOutNode.FirstTriangleOrderIndex; OrderIndex < InOutNode.EndTriangleOrderIndex; ++OrderIndex)
	{
		const std::array<std::uint32_t, 3> VertexIndices = GetTriangleVertexIndices(TriangleOrder[OrderIndex]);

		for (std::uint32_t VertexIndex : VertexIndices)
		{
			const FVector3D VertexPosition = ReadParticleVector(Positions, VertexIndex);

			for (EAxis Axis : AllAxes)
			{
				InOutNode.BoundsMin[Axis] = std::min(InOutNode.BoundsMin[Axis], VertexPosition[Axis]);
				InOutNode.BoundsMax[Axis] = std::max(InOutNode.BoundsMax[Axis], VertexPosition[Axis]);
			}
		}
	}
}

std::optional<FSurfaceClosestPoint> FSurfaceBvh::FindNearestPoint(FVector3D QueryPoint, const std::vector<double>& Positions, double& InOutTriangleTests)
{
	if (IsEmpty())
	{
		return std::nullopt;
	}

	PendingNodeIndices.clear();
	PendingNodeIndices.push_back(0);

	FNearestPointSearchState Search;

	while (!PendingNodeIndices.empty())
	{
		const FNode& Node = Nodes[PendingNodeIndices.back()];
		PendingNodeIndices.pop_back();

		// 상자조차 현재 최선보다 멀면 그 안의 모든 삼각형도 멉니다. 같은 거리는 방문합니다.
		if (Node.GetSquaredDistanceToBounds(QueryPoint) > Search.ClosestPoint.DistanceSquared)
		{
			continue;
		}

		if (Node.IsLeaf())
		{
			SearchLeafTriangles(Node, QueryPoint, Positions, Search, InOutTriangleTests);
		}
		else
		{
			QueueChildrenNearFirst(Node, QueryPoint);
		}
	}

	if (!Search.bHasPoint)
	{
		return std::nullopt;
	}

	return FSurfaceClosestPoint{GetTriangleVertexIndices(Search.TriangleIndex), Search.ClosestPoint};
}

void FSurfaceBvh::QueueChildrenNearFirst(const FNode& Node, FVector3D QueryPoint)
{
	const double LeftDistanceSquared = Nodes[Node.LeftChildNodeIndex].GetSquaredDistanceToBounds(QueryPoint);
	const double RightDistanceSquared = Nodes[Node.RightChildNodeIndex].GetSquaredDistanceToBounds(QueryPoint);

	// LIFO이므로 먼 쪽을 먼저 넣습니다. 동률은 오른쪽을 먼저 방문하는 기존 선택 규칙입니다.
	if (LeftDistanceSquared < RightDistanceSquared)
	{
		PendingNodeIndices.push_back(Node.RightChildNodeIndex);
		PendingNodeIndices.push_back(Node.LeftChildNodeIndex);
	}
	else
	{
		PendingNodeIndices.push_back(Node.LeftChildNodeIndex);
		PendingNodeIndices.push_back(Node.RightChildNodeIndex);
	}
}

void FSurfaceBvh::SearchLeafTriangles(const FNode& Node, FVector3D QueryPoint, const std::vector<double>& Positions, FNearestPointSearchState& InOutSearch, double& InOutTriangleTests) const
{
	for (int OrderIndex = Node.FirstTriangleOrderIndex; OrderIndex < Node.EndTriangleOrderIndex; ++OrderIndex)
	{
		const std::uint32_t TriangleIndex = TriangleOrder[OrderIndex];
		const std::array<std::uint32_t, 3> VertexIndices = GetTriangleVertexIndices(TriangleIndex);
		const FTriangleClosestPoint ClosestPoint = FindClosestPointOnTriangle(QueryPoint, ReadParticleVector(Positions, VertexIndices[0]), ReadParticleVector(Positions, VertexIndices[1]), ReadParticleVector(Positions, VertexIndices[2]));
		++InOutTriangleTests;

		// 엄격한 < 비교로 동률에서는 먼저 방문한 삼각형과 그 무게중심 좌표를 그대로 유지합니다.
		if (ClosestPoint.DistanceSquared < InOutSearch.ClosestPoint.DistanceSquared)
		{
			InOutSearch.ClosestPoint = ClosestPoint;
			InOutSearch.TriangleIndex = TriangleIndex;
			InOutSearch.bHasPoint = true;
		}
	}
}

std::array<std::uint32_t, 3> FSurfaceBvh::GetTriangleVertexIndices(std::uint32_t TriangleIndex) const
{
	const std::size_t TriangleOffset = static_cast<std::size_t>(TriangleIndex) * 3;
	return {Topology.Indices[TriangleOffset], Topology.Indices[TriangleOffset + 1], Topology.Indices[TriangleOffset + 2]};
}

FSurfaceTopology FSurfaceBvh::GetTopology() const
{
	return Topology;
}
