#pragma once
#include "collision/Contact.h"
#include "core/Settings.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

class FSoftBody;
class FSoftContactSolver;

// 접촉 목록과 재사용 임시 저장소를 소유하고 동적·정적 충돌 및 반발을 처리합니다.
// 몸체 포인터는 비소유 관찰자이며 다음 접촉 갱신까지 몸체가 살아 있어야 합니다.
// 접촉 목록과 임시 저장소를 공유하므로 같은 솔버에 대한 호출은 직렬화합니다.
class FCollisionSolver
{
public:
	/** 이전 서브스텝의 접촉을 비우며 할당 용량은 다음 수집에 재사용합니다. */
	void BeginSubstep()
	{
		Contacts.clear();
	}

	/**
	 * 현재 접촉 목록을 읽기 전용으로 빌려줍니다.
	 * @note 다음 접촉 수집·초기화 또는 솔버 파괴 전까지만 사용합니다.
	 */
	const std::vector<FContact>& GetContacts() const noexcept
	{
		return Contacts;
	}

	/**
	 * 구 접촉을 처리한 뒤 연체 쌍의 접촉을 같은 기하 상태에서 수집하고 투영합니다.
	 * 전체 물체 목록에서 충돌 쌍을 선택하므로 한 물체가 아닌 Bodies를 받습니다.
	 * @note 입력 몸체는 호출 동안 살아 있어야 하며 소유권을 이전하지 않습니다.
	 */
	void SolveDynamic(const std::vector<std::unique_ptr<FBody>>& Bodies, FSoftContactSolver& InOutSoftSolver, FStats& InOutStats);

	/**
	 * 이동 가능한 입자를 월드 경계와 입력 순서의 정적 장애물 밖으로 투영합니다.
	 * 고정 환경과 물체 하나의 접촉만 계산하므로 다른 물체 목록은 사용하지 않습니다.
	 * @param bRecordContacts true이면 반발·범퍼에 사용할 접촉을 기록합니다.
	 */
	void SolveStatic(FBody& InOutBody, const std::vector<FObstacle>& Obstacles, const FSettings& Settings, bool bRecordContacts = true);

	/** 기록한 입사 법선 속도와 반발 계수로 접촉 입자의 속도를 보정합니다. */
	void Restitute(const FSettings& Settings);

	/**
	 * 정적 겹침을 반복 투영하고 실제로 이동한 입자의 작업 속도와 제약 풀이 전 속도만 0으로 만듭니다.
	 * @note 동적 접촉을 수집하거나 시뮬레이션 시간을 진행하지 않습니다.
	 */
	void Settle(const std::vector<std::unique_ptr<FBody>>& Bodies, const std::vector<FObstacle>& Obstacles, const FSettings& Settings);

private:
	// 현재 서브스텝에서 기록한 접촉입니다. 속도 복원 뒤 반발·범퍼 처리가 소비합니다.
	std::vector<FContact> Contacts;

	// 비소유 임시 저장소입니다. SolveDynamic마다 비우고 다음 호출에서 할당 용량을 재사용합니다.
	std::vector<FSoftBody*> SoftBodies;

	// 현재 동적 충돌 호출에 포함된 구체의 비소유 목록입니다. 단일 입자끼리 먼저 검사합니다.
	std::vector<FBody*> Balls;

	// 경계 구·상자 검사를 통과한 연체 쌍입니다. 입력 순서를 보존하여 접촉 수집 순서를 결정합니다.
	std::vector<std::pair<FSoftBody*, FSoftBody*>> SoftPairs;

	// SoftPairs에 등장하는 몸체를 중복 없이 보관합니다. 접촉 수집 전에 공간 캐시를 한 번씩 준비합니다.
	std::vector<FSoftBody*> SoftContactParticipants;

	// 모든 연체 쌍에서 모은 투영 전 접촉입니다. 전체 수집을 마친 뒤 순서대로 위치를 보정합니다.
	std::vector<FContact> SoftContacts;

	void CollectBodies(const std::vector<std::unique_ptr<FBody>>& Bodies);

	void SolveBallContacts();

	void CollectSoftPairs();

	void SolveSoftPairs(FSoftContactSolver& InOutSoftSolver, FStats& InOutStats);

	/** 정적 접촉은 정점마다 경계의 X/Y/Z, 입력 장애물 순서로 처리합니다. */
	void SolveBoundaryContact(FBody& InOutBody, std::uint32_t VertexIndex, double Limit, bool bRecordContacts);

	static bool ProjectBoxObstacle(FBody& InOutBody, std::uint32_t VertexIndex, const FObstacle& Obstacle, double ParticleRadius, FVector3D& OutNormal);

	static void RestituteSoftContact(const FContact& Contact, double TargetNormalVelocity, double InverseMassA);

	static void RestituteParticleContact(const FContact& Contact, double TargetNormalVelocity, double InverseMassA);

	/**
	 * 접촉 법선과 예측 상대속도의 법선 성분을 반발 처리용 목록에 기록합니다.
	 * @param BodyB 정적 접촉이면 nullptr이며 상대 몸체의 속도는 0으로 계산합니다.
	 */
	void RecordContact(FBody& BodyA, std::uint32_t VertexIndexA, FBody* BodyB, std::uint32_t VertexIndexB, FVector3D ContactNormal, std::uint32_t ObstacleId = 0);

	/**
	 * 겹친 입자 쌍을 역질량으로 가중하여 분리한 뒤 접촉을 기록합니다.
	 * @note 두 입자가 모두 고정되어 있으면 상태를 변경하지 않습니다.
	 */
	void SolveParticlePairContact(FBody& InOutBodyA, std::uint32_t VertexIndexA, FBody& InOutBodyB, std::uint32_t VertexIndexB, double ContactRadius);
};
