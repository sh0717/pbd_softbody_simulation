#pragma once
#include "core/MathUtils.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

class FSoftBody;

/**
 * 사면체의 현재 기하와 조밀 그리드·희소 셀 트리·초과 후보를 소유합니다.
 * @pre 생성에 사용한 몸체의 토폴로지를 유지하고 첫 질의 전과 위치 변경 뒤 Rebuild를 호출합니다.
 * @note 몸체나 위치 배열을 보관하지 않습니다. 동일 객체의 갱신과 질의는 직렬화합니다.
 */
class FTetrahedronSpatialIndex
{
public:
	/**
	 * 0인 셀 크기는 기준 형상의 양수 변 길이 평균으로 선택합니다.
	 * @pre InCellSize는 0 또는 유한한 양수, InEpsilon은 유한한 양수, InMaxCells는 1 이상이어야 합니다.
	 */
	FTetrahedronSpatialIndex(const FSoftBody& Body, double InCellSize, double InEpsilon, int InMaxCells);

	/**
	 * 몸체 경계·사면체 기하·셀 후보를 현재 위치로 갱신합니다.
	 * @note 할당 실패 시 기본 예외 보장을 제공합니다. 다음 질의 전에 다시 갱신해야 합니다.
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

	/** 자동 선택 또는 명시 설정을 반영한 셀 한 변의 장면 길이입니다. */
	double GetCellSize() const noexcept
	{
		return CellSize;
	}

	/** 퇴화 여부와 관계없이 원본 몸체의 사면체 개수를 반환합니다. */
	std::size_t GetTetrahedronCount() const noexcept
	{
		return Tetrahedra.size();
	}

	/** 후보가 하나 이상 등록된 셀 개수이며 조밀 저장소 전체 크기와 구분합니다. */
	std::size_t GetActiveCellCount() const noexcept
	{
		return ActiveCellCount;
	}

	/** 셀 한도나 좌표 범위 때문에 별도 목록에서 조회하는 사면체 개수입니다. */
	std::size_t GetOverflowTetrahedronCount() const noexcept
	{
		return OverflowTetrahedronIndices.size();
	}

private:
	// 희소 셀 키를 X → Y → Z 사전식 순서로 비교하며 후보 저장 순서는 바꾸지 않습니다.
	struct FCellCoordinatesLess
	{
		bool operator()(const FVector3D& LeftCoordinates, const FVector3D& RightCoordinates) const
		{
			return std::array<double, 3>{LeftCoordinates.X, LeftCoordinates.Y, LeftCoordinates.Z} < std::array<double, 3>{RightCoordinates.X, RightCoordinates.Y, RightCoordinates.Z};
		}
	};

	struct FTetrahedronGeometry
	{
		// 사면체 0번 정점 위치입니다. 질의 점에서 빼서 역기저에 적용할 로컬 좌표를 구합니다.
		FVector3D Origin{};

		// 역기저의 첫 행으로 1번 정점의 무게중심 좌표를 계산합니다.
		FVector3D BarycentricRowU{};

		// 역기저의 둘째 행으로 2번 정점의 무게중심 좌표를 계산합니다.
		FVector3D BarycentricRowV{};

		// 역기저의 셋째 행으로 3번 정점 좌표를 계산하며 0번 좌표는 1-U-V-W입니다.
		FVector3D BarycentricRowW{};

		// 현재 기하에 상대 허용 오차의 여유를 더한 경계 최솟값입니다.
		FVector3D BoundsMin{};

		// 현재 기하에 상대 허용 오차의 여유를 더한 경계 최댓값입니다.
		FVector3D BoundsMax{};

		// 유한한 행렬식과 퇴화 기준을 통과해 셀 등록·포함 검사에 사용할 수 있는지 나타냅니다.
		bool bValid = false;
	};

	// 셀 한 변의 장면 길이입니다. 위치를 이 값으로 나누고 내림하여 셀 키를 계산합니다.
	double CellSize;

	// 기하 퇴화·무게중심 포함·경계 여유에 적용하는 상대 허용 오차입니다.
	double Epsilon;

	// 사면체 하나가 이 개수보다 많은 셀을 덮으면 초과 목록으로 보냅니다.
	int MaxCells;

	// 원본 사면체 번호와 1:1 대응하는 현재 역기저·경계 캐시입니다.
	std::vector<FTetrahedronGeometry> Tetrahedra;

	// 모든 점 질의에서 셀 후보 다음에 검사할 사면체 번호이며 원본 등록 순서를 유지합니다.
	FIndices OverflowTetrahedronIndices;

	// 정수 값 double의 셀 키를 사용하여 범위 밖 float-to-int 변환을 피합니다.
	std::map<FVector3D, FIndices, FCellCoordinatesLess> SparseCellTetrahedra;

	// 조밀 그리드의 전역 시작 셀 좌표입니다. 장면 위치와 구분합니다.
	FVector3D DenseGridOrigin{};

	// 축별 셀 개수입니다. 로컬 셀을 X + SizeX*(Y + SizeY*Z)로 평탄화합니다.
	std::array<int, 3> DenseGridSize{};

	// 셀별 연결 목록의 첫 노드 번호이며 -1은 빈 셀입니다.
	std::vector<int> DenseCellFirstNodeIndices;

	// 연결 목록 노드가 가리키는 사면체 번호입니다. 한 사면체가 여러 셀에 등록될 수 있습니다.
	std::vector<int> DenseNodeTetrahedronIndices;

	// 같은 셀의 다음 후보 번호이며 -1은 목록 끝입니다. 새 후보는 셀의 맨 앞에 삽입합니다.
	std::vector<int> DenseNodeNextIndices;

	// 전체 셀 개수와 좌표가 안전 범위에 있어 조밀 경로를 사용하는지 나타냅니다.
	bool bUsesDenseGrid = false;

	// 후보가 하나 이상 등록된 활성 셀 개수입니다.
	std::size_t ActiveCellCount = 0;

	void ConfigureCellGrid(FSoftBody& Body);

	bool RefitTetrahedron(const FSoftBody& Body, std::uint32_t TetIndex);

	void AddDenseCellReference(std::size_t CellIndex, std::uint32_t TetIndex);

	void RegisterTetrahedronInCells(std::uint32_t TetIndex);

	bool ContainsPoint(std::uint32_t TetIndex, FVector3D QueryPoint, double& InOutCandidateTests) const;
};
