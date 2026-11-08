#pragma once
#include "collision/Contact.h"
#include "collision/SurfaceBvh.h"
#include "collision/TetrahedronPointQuery.h"
#include "simulation/SoftBody.h"
#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

struct FContactOptions
{
	// 장면 길이 단위입니다. 0이면 기준 형상에서 길이가 양수인 사면체 변의 평균을 사용합니다.
	double CellSize = 0;

	// 퇴화 판정과 포함 검사에 사용하는 상대 허용 오차입니다. 길이 척도와 결합하거나 무차원 좌표에 적용합니다.
	double Epsilon = 1E-10;

	// 생성한 접촉을 투영할 때 추가로 확보할 법선 방향 여유 거리입니다. 단위는 장면 길이입니다.
	double Margin = 0;

	// 사면체가 이 수보다 많은 셀에 걸치면 별도 목록에서 조회합니다.
	int MaxCells = 512;

	/** 셀 크기·상대 허용 오차·여유 거리·최대 셀 수의 유효 범위를 검증합니다. */
	FOperationResult Validate() const;
};

// 배열을 소유하는 진단 스냅샷이며 이후 Prepare/Configure와 독립적입니다.
struct FContactBodyInfo
{
	// 경계 삼각형의 정점 인덱스를 세 개씩 소유하는 복사본입니다.
	FIndices SurfaceIndices;

	// 외부 표면에 등장하는 고유 정점 인덱스입니다. 최초 출현 순서를 유지합니다.
	FIndices SurfaceVertices;

	// 몸체의 사면체 개수입니다. 퇴화 판정을 통과한 개수만을 뜻하지는 않습니다.
	std::size_t TetCount = 0;

	// 추출한 외부 경계 삼각형 개수이며 SurfaceIndices.size()/3과 같습니다.
	std::size_t SurfaceTriangleCount = 0;

	// 자동 선택 또는 명시 설정을 반영한 이 몸체의 실제 셀 한 변 길이입니다.
	double CellSize = 0;

	// 기존 진단 이름입니다. 조밀 그리드 또는 희소 셀 트리에서 사면체 후보가 등록된 활성 셀 개수입니다.
	std::size_t HashCellCount = 0;

	// 셀 범위·등록 한도 때문에 별도 초과 목록으로 보낸 사면체 개수입니다.
	std::size_t OverflowTets = 0;
};

/**
 * 공간 셀 인덱스와 표면 BVH 캐시를 소유하며 접촉을 수집하고 투영합니다.
 * @note 호출을 직렬화합니다. 몸체는 비소유 관찰자이며 캐시와 접촉 사용이 끝날 때까지
 * 살아 있어야 합니다.
 */
class FSoftContactSolver
{
public:
	/** 먼저 검증합니다. 성공하면 캐시와 기존 접촉을 무효화합니다. */
	FOperationResult Configure(const FContactOptions& InOptions);

	const FContactOptions& GetOptions() const noexcept
	{
		return Options;
	}

	const FSoftContactStats& GetStats() const noexcept
	{
		return Stats;
	}

	/**
	 * 준비된 몸체의 표면·공간 진단 정보를 소유하는 복사본으로 반환합니다.
	 * @return 등록되지 않은 몸체이면 빈 값입니다.
	 */
	std::optional<FContactBodyInfo> GetBodyInfo(const FSoftBody* Body) const;

	/**
	 * 위치 편집 후 공간 캐시를 갱신하며 토폴로지와 몸체 수명은 캐시 사용 중 유지합니다.
	 * 널 입력은 캐시·진단 변경 전에 거부합니다. 새 몸체 캐시는 공간 셀 인덱스·BVH 완성 뒤 등록하고
	 * 일괄 통계는 모든 준비 성공 뒤 교체합니다. 토폴로지는 변경하지 않으며 Configure가
	 * 전체 등록을 지웁니다.
	 * @note 동적 할당 자원 소진은 종료 정책을 따릅니다. 검증 실패는 상태값으로 반환합니다.
	 * 기존에 등록한 몸체는 캐시를 사용하는 동안 살아 있어야 합니다.
	 */
	FOperationResult Prepare(const std::vector<FSoftBody*>& Bodies);

	/** 마지막 Configure 이후 Prepare에 성공하여 공간 캐시를 가진 몸체인지 반환합니다. */
	bool IsPrepared(const FSoftBody* Body) const noexcept
	{
		return BodyCaches.find(Body) != BodyCaches.end();
	}

	/**
	 * 준비된 몸체에서 점을 포함하는 사면체 인덱스를 독립 배열로 반환합니다.
	 * @note bFirst는 최소 인덱스가 아닌 결정적 순회의 첫 일치에서 중단합니다.
	 */
	FIndices Query(FSoftBody* Body, FVector3D QueryPoint, bool bFirst = false, FOperationResult* OutResult = nullptr);

	/**
	 * 위치를 바꾸지 않고 양방향 접촉을 추가합니다. 모든 몸체 쌍의 접촉 수집 후 투영해야 합니다.
	 * @pre 두 몸체가 IsPrepared를 만족해야 하며 위반하면 std::abort로 종료합니다.
	 */
	void Generate(FSoftBody* SourceBody, FSoftBody* TargetBody, std::vector<FContact>& InOutContacts);

	/**
	 * 접촉 법선의 분리 거리와 유효 역질량으로 위치를 보정합니다.
	 * @return 위치를 투영했으면 true이며 분리 상태이거나 모두 고정이면 false입니다.
	 * @pre 접촉의 몸체·인덱스와 무게중심 좌표가 유효해야 합니다.
	 */
	bool Project(const FContact& Contact);

private:
	struct FBodySpatialCache
	{
		// 현재 사면체의 역기저·셀 후보를 소유하며 위치 변경 뒤 재구축합니다.
		FTetrahedronPointQuery TetrahedronQuery;

		// 외부 표면·BVH·질의 스택을 소유하며 같은 위치 상태로 경계를 갱신합니다.
		FSurfaceBvh SurfaceBvh;
	};

	// 검증한 접촉 설정입니다. Configure 성공 시 기존 공간 캐시를 지웁니다.
	FContactOptions Options;

	// 최근 Prepare 이후 질의·접촉 생성·투영 진단이며 일괄 준비 성공 시 교체합니다.
	FSoftContactStats Stats;

	// 몸체 주소별 두 공간 컴포넌트를 소유합니다. 키는 몸체를 소유하지 않습니다.
	std::unordered_map<const FSoftBody*, FBodySpatialCache> BodyCaches;

	void RefitBodySpatialCache(FBodySpatialCache& InOutBodyCache, FSoftBody& Body);

	void CollectDirectionalContacts(FSoftBody* SourceBody, FSoftBody* TargetBody, std::vector<FContact>& InOutContacts);
};
