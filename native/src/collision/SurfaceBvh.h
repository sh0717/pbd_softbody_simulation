#pragma once
#include "collision/SurfaceTopology.h"
#include "collision/TriangleGeometry.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

// BVH 내부 저장소나 입력 위치의 수명과 독립된 최근접 표면 결과입니다.
struct FSurfaceClosestPoint
{
	// 최근접 삼각형의 정점 세 개입니다. ClosestPoint.Weights와 같은 순서입니다.
	std::array<std::uint32_t, 3> VertexIndices{};

	// 최근접 장면 좌표·무게중심 좌표와 질의점까지의 거리 제곱입니다.
	FTriangleClosestPoint ClosestPoint;
};

/**
 * 표면 삼각형을 경계 상자 계층(BVH, Bounding Volume Hierarchy)으로 묶어 최근접점을 찾습니다.
 * 생성 시 기준 위치로 트리를 한 번 구성하고, Refit에서 현재 위치의 경계만 갱신합니다.
 * 검색은 경계 상자까지의 거리 하한으로 먼 가지를 생략하고 잎의 삼각형만 직접 검사합니다.
 * 각 노드의 AABB(축 정렬 경계 상자)는 하위 삼각형의 현재 정점을 모두 감쌉니다.
 *
 * 저장 관계: Nodes[0] → 자식 노드 → 잎의 [FirstTriangleOrderIndex, EndTriangleOrderIndex)
 *            → TriangleOrder[순서 번호] → 원본 삼각형 번호 → Topology.Indices의 정점 세 개
 * 원본 토폴로지는 고정하고 TriangleOrder만 정렬하므로 표면 정점의 접촉 수집 순서를 보존합니다.
 *
 * @pre 위치는 유한한 XYZ 연속 배열이며 Topology의 모든 정점 번호가 유효해야 합니다.
 * @pre 첫 질의 전과 위치 변경 뒤 Refit을 호출하며, 토폴로지가 바뀌면 객체를 다시 만듭니다.
 * @note 토폴로지·트리·재사용 검색 스택을 소유하고 몸체나 위치 배열은 보관하지 않습니다.
 * @note 스택을 재사용하므로 동일 객체의 갱신·최근접 질의는 직렬화합니다.
 */
class FSurfaceBvh
{
public:
	/** 검증된 표면을 소유하고 기준 위치의 삼각형 중심으로 트리를 구성합니다. 경계는 Refit에서 채웁니다. */
	FSurfaceBvh(FSurfaceTopology Surface, const std::vector<double>& RestPositions);

	/** 토폴로지와 삼각형 순서를 유지하며 현재 위치로 노드 경계를 갱신합니다. */
	void Refit(const std::vector<double>& Positions);

	/**
	 * 현재 표면의 최근접 점과 삼각형 정점 번호를 독립 값으로 반환합니다.
	 * @return 표면이 없거나 유한한 최근접 후보를 찾지 못하면 빈 값입니다.
	 * @pre Positions는 마지막으로 Refit에 전달한 위치 값과 같아야 합니다.
	 * @param InOutTriangleTests 최근접 점을 직접 계산한 삼각형 검사 횟수를 누적합니다.
	 * @note 같은 거리에서는 기존 BVH 방문 순서의 첫 결과를 유지합니다.
	 */
	std::optional<FSurfaceClosestPoint> FindNearestPoint(FVector3D QueryPoint, const std::vector<double>& Positions, double& InOutTriangleTests);

	/** 진단용 외부 면·표면 정점의 소유 복사본을 반환합니다. */
	FSurfaceTopology GetTopology() const;

	/** 외부 표면에 등장하는 고유 정점 개수입니다. */
	std::size_t GetSurfaceVertexCount() const noexcept
	{
		return Topology.Vertices.size();
	}

	/** 표면 정점을 최초 등장 순서로 조회합니다. @pre OrderIndex가 정점 개수보다 작아야 합니다. */
	std::uint32_t GetSurfaceVertexIndex(std::size_t OrderIndex) const
	{
		return Topology.Vertices[OrderIndex];
	}

	/** 외부 삼각형 개수입니다. */
	std::size_t GetTriangleCount() const noexcept
	{
		return Topology.Indices.size() / 3;
	}

	/** 최근접 검색에 사용할 표면 노드가 없는지 반환합니다. */
	bool IsEmpty() const noexcept
	{
		return Nodes.empty();
	}

private:
	// 내부 노드는 두 자식을, 잎은 직접 검사할 삼각형 구간을 가집니다. 정점 좌표를 복사하지 않습니다.
	struct FNode
	{
		/** 두 자식이 모두 없는 잎인지 반환합니다. 트리 구축 시 자식은 항상 쌍으로 연결합니다. */
		bool IsLeaf() const noexcept
		{
			return LeftChildNodeIndex < 0;
		}

		/** 점과 AABB 사이 거리 제곱입니다. 상자 안에서는 0이며 하위 삼각형까지 거리의 하한입니다. */
		double GetSquaredDistanceToBounds(FVector3D QueryPoint) const;

		// 이 노드 아래 현재 삼각형을 모두 감싸는 경계 최솟값입니다.
		FVector3D BoundsMin{};

		// 경계 최댓값이며 질의점까지 거리의 하한 계산에 사용합니다.
		FVector3D BoundsMax{};

		// 삼각형 순서 배열에서 이 노드가 담당하는 반열린 구간의 시작입니다.
		int FirstTriangleOrderIndex = 0;

		// 삼각형 순서 구간의 끝입니다. 이 번호는 담당 구간에 포함하지 않습니다.
		int EndTriangleOrderIndex = 0;

		// 왼쪽 자식 노드 번호입니다. -1이면 직접 삼각형을 검사하는 잎입니다.
		int LeftChildNodeIndex = -1;

		// 오른쪽 자식 노드 번호입니다. 내부 노드는 좌우 자식을 함께 가집니다.
		int RightChildNodeIndex = -1;
	};

	// 질의 한 번의 최선 후보입니다. 노드에 저장하지 않으므로 다음 질의로 결과가 새지 않습니다.
	struct FNearestPointSearchState
	{
		// 아직 후보가 없을 때는 무한대입니다. 더 가까운 점을 찾을 때마다 가지치기 기준이 좁아집니다.
		FTriangleClosestPoint ClosestPoint{{}, {}, std::numeric_limits<double>::infinity()};

		// ClosestPoint가 속한 원본 삼각형 번호이며 bHasPoint가 true일 때만 결과 생성에 사용합니다.
		std::uint32_t TriangleIndex = 0;

		// 유한한 거리의 후보를 찾았는지 나타내며 빈 결과와 0번 삼각형을 구분합니다.
		bool bHasPoint = false;
	};

	// 원본 면과 고유 표면 정점을 소유합니다. Indices[3*t..3*t+3)는 원본 삼각형 t의 정점 번호입니다.
	// Vertices의 최초 등장 순서는 방향별 접촉 수집 순서이므로 트리 구축 중에도 바꾸지 않습니다.
	FSurfaceTopology Topology;

	// BVH 순서 번호 → 원본 삼각형 번호의 순열입니다. 노드의 반열린 구간은 이 배열을 가리킵니다.
	// 예: TriangleOrder[2] == 7이면 그 순서의 정점은 Topology.Indices[21], [22], [23]입니다.
	FIndices TriangleOrder;

	// 포인터 대신 배열 인덱스로 연결한 이진 트리입니다. 비어 있지 않으면 0번이 루트입니다.
	// 구축 후 연결·삼각형 구간은 고정하고 Refit에서 BoundsMin/Max만 갱신합니다.
	std::vector<FNode> Nodes;

	// 검색 대기 노드의 LIFO 스택입니다. 먼 자식을 먼저 넣어 가까운 자식을 먼저 꺼냅니다.
	// 매 질의 시작에 비우고 할당 용량만 재사용합니다. 위치 배열이나 검색 결과는 보관하지 않습니다.
	std::vector<int> PendingNodeIndices;

	/** 생성 시 한 번 호출하여 순열·기준 중심·노드를 구성합니다. 빈 표면은 노드를 만들지 않습니다. */
	void BuildTree(const std::vector<double>& RestPositions);

	/** 순서 구간을 이등분하여 부모부터 저장하고 루트 인덱스를 반환합니다. 빈 구간은 -1입니다. */
	int BuildNode(int FirstTriangleOrderIndex, int EndTriangleOrderIndex, const std::vector<FVector3D>& RestTriangleCenters);

	/** 기준 중심의 분포가 가장 넓은 축입니다. 같은 폭은 X → Y → Z 우선순위를 유지합니다. */
	EAxis SelectSplitAxis(int FirstTriangleOrderIndex, int EndTriangleOrderIndex, const std::vector<FVector3D>& RestTriangleCenters) const;

	/** 잎부터 부모 순으로 경계를 갱신합니다. 구축된 유효 노드 인덱스가 필요합니다. */
	void RefitNode(int NodeIndex, const std::vector<double>& Positions);

	/** 잎의 삼각형 정점을 읽어 현재 AABB를 다시 만듭니다. @pre InOutNode는 잎입니다. */
	void RefitLeafBounds(FNode& InOutNode, const std::vector<double>& Positions) const;

	/** 가까운 자식부터 꺼내도록 스택에 넣습니다. 같은 거리는 오른쪽 우선이며 내부 노드만 받습니다. */
	void QueueChildrenNearFirst(const FNode& Node, FVector3D QueryPoint);

	/** 잎의 원래 순회 순서로 후보를 평가하고, 더 가까운 결과와 실제 검사 횟수만 누적합니다. */
	void SearchLeafTriangles(const FNode& Node, FVector3D QueryPoint, const std::vector<double>& Positions, FNearestPointSearchState& InOutSearch, double& InOutTriangleTests) const;

	/** 원본 삼각형 번호를 정점 세 개로 변환합니다. @pre TriangleIndex < GetTriangleCount()입니다. */
	std::array<std::uint32_t, 3> GetTriangleVertexIndices(std::uint32_t TriangleIndex) const;
};
