#pragma once
#include "core/MathUtils.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

/** 사면체 경계와 겹치는 셀별 후보 번호를 소유합니다. 실제 점 포함 검사는 호출자가 수행합니다. */
class FTetrahedronCellGrid
{
public:
	/** @pre InCellSize와 InEpsilon은 유한한 양수, InMaxCells는 1 이상이어야 합니다. */
	FTetrahedronCellGrid(double InCellSize, double InEpsilon, int InMaxCells);

	/** 이전 후보를 지우고 현재 몸체 경계에서 조밀·희소 저장 방식을 선택합니다. */
	void Reset(FVector3D BoundsMin, FVector3D BoundsMax);

	/** 유효한 사면체의 여유 경계가 덮는 셀 또는 초과 목록에 번호를 등록합니다. */
	void RegisterTetrahedron(std::uint32_t TetrahedronIndex, FVector3D BoundsMin, FVector3D BoundsMax);

	/**
	 * 점의 셀 후보 뒤에 초과 후보를 방문합니다. 등록 이후 셀을 바꾸지 않고 호출합니다.
	 * @param VisitCandidate true를 반환하면 즉시 순회를 멈춥니다.
	 * @return 방문자가 중단을 요청했으면 true입니다. 후보의 실제 포함 여부와는 독립적입니다.
	 * @note 조밀 목록은 역등록 순서, 희소·초과 목록은 등록 순서를 유지합니다.
	 */
	template <typename VisitorType>
	bool VisitCandidates(FVector3D QueryPoint, const VisitorType& VisitCandidate) const
	{
		const FVector3D Cell = CalculateCellCoordinates(QueryPoint);

		if (bUsesDenseGrid)
		{
			// 전역 셀 키에서 시작 셀을 뺀 로컬 좌표이며 배열 접근 전에 축별 범위를 확인합니다.
			const FVector3D LocalCell = Cell - DenseGridOrigin;

			if (LocalCell.X >= 0 && LocalCell.Y >= 0 && LocalCell.Z >= 0 && LocalCell.X < DenseGridSize[0] && LocalCell.Y < DenseGridSize[1] && LocalCell.Z < DenseGridSize[2])
			{
				const std::size_t CellIndex = static_cast<std::size_t>(LocalCell.X + DenseGridSize[0] * (LocalCell.Y + DenseGridSize[1] * LocalCell.Z));

				for (int NodeIndex = DenseCellFirstNodeIndices[CellIndex]; NodeIndex != -1; NodeIndex = DenseNodeNextIndices[NodeIndex])
				{
					if (VisitCandidate(static_cast<std::uint32_t>(DenseNodeTetrahedronIndices[NodeIndex])))
					{
						return true;
					}
				}
			}
		}
		else
		{
			const auto CellCandidates = SparseCellTetrahedra.find(Cell);

			if (CellCandidates != SparseCellTetrahedra.end())
			{
				for (std::uint32_t TetrahedronIndex : CellCandidates->second)
				{
					if (VisitCandidate(TetrahedronIndex))
					{
						return true;
					}
				}
			}
		}

		for (std::uint32_t TetrahedronIndex : OverflowTetrahedronIndices)
		{
			if (VisitCandidate(TetrahedronIndex))
			{
				return true;
			}
		}

		return false;
	}

	double GetCellSize() const noexcept
	{
		return CellSize;
	}

	/** 후보가 하나 이상 등록된 셀 수이며 조밀 저장소 전체 크기와 구분합니다. */
	std::size_t GetActiveCellCount() const noexcept
	{
		return bUsesDenseGrid ? ActiveDenseCellCount : SparseCellTetrahedra.size();
	}

	std::size_t GetOverflowTetrahedronCount() const noexcept
	{
		return OverflowTetrahedronIndices.size();
	}

private:
	struct FCellCoordinatesLess
	{
		bool operator()(const FVector3D& LeftCoordinates, const FVector3D& RightCoordinates) const
		{
			return std::array<double, 3>{LeftCoordinates.X, LeftCoordinates.Y, LeftCoordinates.Z} < std::array<double, 3>{RightCoordinates.X, RightCoordinates.Y, RightCoordinates.Z};
		}
	};

	// 셀 한 변의 장면 길이이며 생성 뒤 고정합니다.
	double CellSize;

	// 몸체 경계를 조밀 그리드로 바꿀 때 상대 여유 거리를 계산합니다.
	double Epsilon;

	// 사면체 하나의 셀 등록 상한이며 초과하면 별도 목록으로 보냅니다.
	int MaxCells;

	// 셀 후보 다음에 검사할 사면체 번호이며 등록 순서를 유지합니다.
	FIndices OverflowTetrahedronIndices;

	// 정수 값 double의 셀 키를 X→Y→Z 순서로 비교하여 범위 밖 정수 변환을 피합니다.
	std::map<FVector3D, FIndices, FCellCoordinatesLess> SparseCellTetrahedra;

	// 조밀 그리드의 전역 시작 셀 좌표이며 장면 위치와 구분합니다.
	FVector3D DenseGridOrigin{};

	// 축별 셀 개수이며 로컬 좌표를 X+SizeX*(Y+SizeY*Z)로 평탄화합니다.
	std::array<int, 3> DenseGridSize{};

	// 셀별 연결 목록의 첫 노드 번호이며 -1은 빈 셀입니다.
	std::vector<int> DenseCellFirstNodeIndices;

	// 각 노드가 가리키는 사면체 번호입니다. 한 사면체가 여러 셀에 등록될 수 있습니다.
	std::vector<int> DenseNodeTetrahedronIndices;

	// 같은 셀의 다음 노드 번호이며 -1은 목록 끝입니다. 새 후보는 맨 앞에 삽입합니다.
	std::vector<int> DenseNodeNextIndices;

	// 마지막 Reset에서 전체 셀 수와 좌표가 안전한 조밀 경로를 선택했는지 나타냅니다.
	bool bUsesDenseGrid = false;

	// 현재 조밀 경로에서 후보가 등록된 셀 개수입니다. 희소 경로는 map의 크기를 사용합니다.
	std::size_t ActiveDenseCellCount = 0;

	FVector3D CalculateCellCoordinates(FVector3D Position) const;

	void AddDenseCellReference(std::size_t CellIndex, std::uint32_t TetrahedronIndex);
};
