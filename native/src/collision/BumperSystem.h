#pragma once
#include "collision/Contact.h"
#include <limits>
#include <map>

struct FHit
{
	// 비소유 관찰자입니다. 이벤트를 읽는 동안 해당 몸체가 살아 있어야 합니다.
	FBody* Body;

	// 범퍼가 발동한 장애물의 ID입니다. 장애물 벡터의 숫자 인덱스와 구분합니다.
	std::uint32_t Obstacle;

	// 범퍼가 발동한 시뮬레이션 시간입니다. 실제 벽시계와 구분하며 단위는 초입니다.
	double Time;

	// 상한을 적용한 범퍼 강도입니다. 목표 법선 속도에 더한 장면 길이/초 단위 값입니다.
	double Strength;

	// 이 발동에서 중복을 제거하고 처리한 몸체 정점 수입니다. 접촉 레코드 수와 다를 수 있습니다.
	std::size_t Vertices;
};

// 몸체·범퍼별 쿨다운과 현재 프레임 이벤트를 소유합니다.
// 상태 키의 몸체 포인터는 관찰자이며 Reset 또는 파괴까지 몸체가 살아 있어야 합니다.
class FBumperSystem
{
public:
	/** 이번 프레임의 충돌 이벤트를 비우며 몸체·범퍼별 쿨다운은 유지합니다. */
	void BeginFrame()
	{
		Hits.clear();
	}

	/**
	 * 접촉을 몸체·범퍼별로 묶고 재진입·쿨다운 조건을 만족하는 입자에 속도를 더합니다.
	 * @param SubstepSeconds 양수인 초 단위 서브스텝 간격입니다.
	 * @note 몸체별 중복 정점은 한 번만 처리하며 입력 접촉의 최초 출현 순서를 유지합니다.
	 */
	void Apply(const std::vector<FContact>& Contacts, const std::vector<FObstacle>& Obstacles, double SimulationTime, double SubstepSeconds, double Restitution);

	/** 충돌 이벤트·쿨다운·대여 포인터와 장애물 인덱스를 모두 초기화합니다. */
	void Reset()
	{
		Hits.clear();
		States.clear();
		ClearGroups();
		ObstacleIndices.clear();
	}

	/**
	 * 이번 프레임의 범퍼 이벤트를 읽기 전용으로 빌려줍니다.
	 * @note 다음 BeginFrame·Apply·Reset 또는 파괴 전에만 사용합니다.
	 */
	const std::vector<FHit>& GetHits() const noexcept
	{
		return Hits;
	}

private:
	struct FState
	{
		// 이 몸체·범퍼 조합의 마지막 접촉 시간입니다. 재진입 판정에 사용하며 초 단위입니다.
		double LastTouchTimeSeconds = -std::numeric_limits<double>::infinity();

		// 마지막 범퍼 발동 시간입니다. 새 접촉이어도 쿨다운이 끝나지 않으면 재발동하지 않습니다.
		double LastFireTimeSeconds = -std::numeric_limits<double>::infinity();

		// 이전 접촉을 처리한 서브스텝 길이입니다. 시간 간격이 변해도 접촉 단절 판정의 여유를 유지합니다.
		double LastSubstepSeconds = 0;
	};

	struct FGroup
	{
		// 현재 Apply 동안 속도를 보정할 몸체의 비소유 포인터입니다.
		FBody* Body = nullptr;

		// 현재 입력 장애물 벡터에서 빌린 범퍼입니다. 호출을 마칠 때 대여 포인터를 비웁니다.
		const FObstacle* Obstacle = nullptr;

		// 같은 몸체·범퍼에 속한 입력 접촉의 비소유 목록입니다. 최초 출현 순서대로 처리합니다.
		std::vector<const FContact*> Contacts;
	};

	// (몸체 주소, 장애물 ID)별 접촉·발동 시각입니다. 장애물 제거 시 해당 쿨다운 상태를 정리합니다.
	std::map<std::pair<FBody*, std::uint32_t>, FState> States;

	// 이번 프레임의 범퍼 발동 이벤트를 소유합니다. BeginFrame이나 Reset에서 비웁니다.
	std::vector<FHit> Hits;

	// 활성 구간만 사용하고 그룹별 접촉 저장 용량을 다음 Apply에서 재사용합니다.
	// 성공한 Apply/Reset 뒤에는 빌린 포인터를 모두 비웁니다. 할당 실패 후 다음 호출도
	// 먼저 ClearGroups를 실행하므로 이전 입력의 포인터를 역참조하지 않습니다.
	std::vector<FGroup> Groups;

	// Groups에서 이번 Apply에 사용하는 앞쪽 원소 수입니다. 뒤쪽 원소는 용량 재사용용입니다.
	std::size_t ActiveGroups = 0;

	// Apply마다 다시 구축합니다. 숫자 인덱스는 장애물 벡터의 저장소를 빌리지 않습니다.
	// (ID, 입력 인덱스) 순으로 정렬하여 같은 ID 중 입력상 첫 장애물을 선택합니다.
	std::vector<std::pair<std::uint32_t, std::size_t>> ObstacleIndices;

	// 정점별 마지막 처리 세대입니다. 같은 그룹에서 한 정점을 여러 접촉이 가리켜도 한 번만 보정합니다.
	std::vector<std::uint32_t> VertexGenerations;

	// 현재 범퍼 그룹의 중복 제거 표식입니다. 정수 순환 시 정점 표식을 모두 비우고 다시 시작합니다.
	std::uint32_t VertexGeneration = 0;

	void ClearGroups() noexcept;

	void IndexObstacles(const std::vector<FObstacle>& Obstacles);

	const FObstacle* FindObstacle(const std::vector<FObstacle>& Obstacles, std::uint32_t ObstacleId) const;

	void CollectGroups(const std::vector<FContact>& Contacts, const std::vector<FObstacle>& Obstacles);

	void ApplyGroup(const FGroup& Group, double SimulationTime, double SubstepSeconds, double Restitution);

	void PruneStates(const std::vector<FObstacle>& Obstacles);
};
