#pragma once
#include "collision/TetrahedronCellGrid.h"
#include "collision/TetrahedronGeometry.h"
#include "core/MathUtils.h"
#include <cstddef>
#include <vector>

class FSoftBody;

/**
 * 현재 위치의 사면체 기하와 셀 후보를 소유하며 점이 포함된 사면체를 조회합니다.
 * @pre 생성에 사용한 몸체의 토폴로지를 유지하고 첫 질의 전과 위치 변경 뒤 Rebuild를 호출합니다.
 * @note 몸체나 위치 배열을 보관하지 않습니다. 동일 객체의 갱신과 질의는 직렬화합니다.
 */
class FTetrahedronPointQuery
{
public:
	/**
	 * 0인 셀 크기는 기준 형상의 양수 변 길이 평균으로 선택합니다.
	 * @pre InCellSize는 0 또는 유한한 양수, InEpsilon은 유한한 양수, InMaxCells는 1 이상이어야 합니다.
	 */
	FTetrahedronPointQuery(const FSoftBody& Body, double InCellSize, double InEpsilon, int InMaxCells);

	/**
	 * 몸체 경계·사면체 기하·셀 후보를 현재 위치로 갱신합니다.
	 * @note 동적 할당 자원 소진은 종료 정책을 따릅니다. 위치 변경 뒤 다음 질의 전에 갱신해야 합니다.
	 */
	void Rebuild(FSoftBody& Body);

	/**
	 * 포함된 사면체를 결정적 후보 순서로 조회합니다.
	 * @pre 같은 몸체의 현재 위치로 Rebuild가 성공했어야 합니다.
	 * @param OutTetrahedra 널이면 포함 여부만 검사하며, 그 외에는 일치 인덱스를 뒤에 추가합니다.
	 * @param bFirst true이면 최소 번호가 아닌 실제 순회의 첫 일치에서 중단합니다.
	 * @param InOutCandidateTests 경계 검사를 통과한 무게중심 좌표 검사 횟수를 누적합니다.
	 */
	bool QueryContainingTetrahedra(FVector3D QueryPoint, FIndices* OutTetrahedra, bool bFirst, double& InOutCandidateTests) const;

	double GetCellSize() const noexcept
	{
		return CellGrid.GetCellSize();
	}

	/** 퇴화 여부와 관계없이 원본 몸체의 사면체 개수를 반환합니다. */
	std::size_t GetTetrahedronCount() const noexcept
	{
		return Tetrahedra.size();
	}

	std::size_t GetActiveCellCount() const noexcept
	{
		return CellGrid.GetActiveCellCount();
	}

	std::size_t GetOverflowTetrahedronCount() const noexcept
	{
		return CellGrid.GetOverflowTetrahedronCount();
	}

private:
	// 기하 퇴화·무게중심 포함·경계 여유에 적용하는 상대 허용 오차입니다.
	double Epsilon;

	// 원본 사면체 번호와 1:1 대응하는 현재 기하입니다. 토폴로지가 유지되는 동안 크기는 고정합니다.
	std::vector<FTetrahedronGeometry> Tetrahedra;

	// 조밀·희소 셀 후보와 초과 목록을 소유하며 기하 판정과 독립적으로 후보를 순회합니다.
	FTetrahedronCellGrid CellGrid;
};
