#pragma once
#include "collision/SurfaceTopology.h"
#include "collision/TriangleGeometry.h"
#include <array>
#include <cstddef>
#include <cstdint>
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
 * 외부 표면 토폴로지·BVH·질의 스택을 소유하며 몸체와 위치 배열은 보관하지 않습니다.
 * @pre 입력 위치는 토폴로지의 정점 번호와 대응하며 첫 질의 전과 위치 변경 뒤 Refit을 호출합니다.
 * @note 동일 객체의 갱신·최근접 질의는 직렬화합니다.
 */
class FSurfaceBvh
{
public:
	/** 검증된 표면을 소유하고 기준 위치의 중심으로 결정적인 BVH 순서를 구성합니다. */
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
		return SurfaceVertexIndices.size();
	}

	/** 표면 정점을 최초 등장 순서로 조회합니다. @pre OrderIndex가 정점 개수보다 작아야 합니다. */
	std::uint32_t GetSurfaceVertexIndex(std::size_t OrderIndex) const
	{
		return SurfaceVertexIndices[OrderIndex];
	}

	/** 외부 삼각형 개수입니다. */
	std::size_t GetTriangleCount() const noexcept
	{
		return SurfaceTriangleVertexIndices.size() / 3;
	}

	/** 최근접 검색에 사용할 표면 노드가 없는지 반환합니다. */
	bool IsEmpty() const noexcept
	{
		return SurfaceBvhNodes.empty();
	}

private:
	struct FSurfaceBvhNode
	{
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

	// 삼각형 t의 정점 세 개를 [3*t, 3*t+3)에 소유하며 원본 외부 면 순서를 유지합니다.
	FIndices SurfaceTriangleVertexIndices;

	// 최초 등장 순서의 고유 표면 정점이며 방향별 접촉 수집 순서를 결정합니다.
	FIndices SurfaceVertexIndices;

	// BVH 구간의 순서 번호를 원래 표면 삼각형 번호로 변환합니다.
	FIndices TriangleIndicesByBvhOrder;

	// 최근접 검색용 노드이며 비어 있지 않으면 0번이 루트입니다.
	std::vector<FSurfaceBvhNode> SurfaceBvhNodes;

	// 최근접 질의마다 비우고 할당 용량을 재사용하는 대기 노드 번호입니다.
	std::vector<int> TraversalStack;

	int Build(int StartOrderIndex, int EndOrderIndex, const std::vector<FVector3D>& TriangleCenters);

	void RefitNode(int NodeIndex, const std::vector<double>& Positions);
};
