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
	FOperationResult SolveDynamic(const std::vector<std::unique_ptr<FBody>>& Bodies, FSoftContactSolver& InOutSoftContactSolver, FSoftContactStats& InOutStats);

	/**
	 * 이동 가능한 입자를 월드 경계와 입력 순서의 정적 장애물 밖으로 투영합니다.
	 * 고정 환경과 물체 하나의 접촉만 계산하므로 다른 물체 목록은 사용하지 않습니다.
	 * @param bRecordContacts true이면 반발·범퍼에 사용할 접촉을 기록합니다.
	 */
	void SolveStatic(FBody& InOutBody, const std::vector<FObstacle>& Obstacles, const FSettings& Settings, bool bRecordContacts = true);

	/**
	 * 기록한 접촉 순서대로 입자 쌍 또는 연체 표면의 법선 상대속도를 보정합니다.
	 * 입사 법선 속도를 u, 반발 계수를 e라 하면 목표 속도는 u < 0일 때 -e*u, 그 외에는 u입니다.
	 * @pre 위치 투영과 위치 차이에 의한 속도 복원을 마친 뒤 호출합니다.
	 * @note 위치는 변경하지 않으며 마찰에 의한 접선 속도 보정은 수행하지 않습니다.
	 */
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

	/**
	 * 이전 동적 충돌의 임시 목록을 비우고 입력 순서대로 연체와 구체의 비소유 포인터를 수집합니다.
	 * 구체가 있으면 구와의 광역 검사에 사용할 연체 경계를 현재 위치로 갱신합니다.
	 * @param Bodies 월드가 소유하는 몸체 목록입니다. 몸체 소유권을 이전하지 않습니다.
	 * @note 반발에 사용할 Contacts는 유지하며 BeginSubstep에서만 비웁니다.
	 */
	void CollectBodies(const std::vector<std::unique_ptr<FBody>>& Bodies);

	/**
	 * 중복 없는 구체 쌍을 먼저 분리한 뒤 연체 입자와 구체의 접촉을 입력 순서대로 처리합니다.
	 * 연체와 구체는 경계 구 검사를 통과한 경우 각 입자를 검사하며 위치 보정과 접촉 기록을 즉시 수행합니다.
	 * @pre CollectBodies로 SoftBodies와 Balls를 준비해야 합니다.
	 * @note 최소 분리 거리는 구체 쌍의 반지름 합이며 연체 입자와 구체에서는 구체의 반지름입니다.
	 */
	void SolveBallContacts();

	/**
	 * 구 접촉 처리 후의 연체 경계를 갱신하고 경계 구와 AABB 검사를 통과한 쌍을 수집합니다.
	 * SoftPairs는 입력 순서의 중복 없는 쌍을, SoftContactParticipants는 참여 몸체를 중복 없이 보관합니다.
	 * @pre CollectBodies로 임시 목록을 초기화하고 SolveBallContacts를 마쳐야 합니다.
	 * @note 후보 쌍 선정 단계이므로 실제 침투 판정과 입자 위치 보정은 수행하지 않습니다.
	 */
	void CollectSoftPairs();

	/**
	 * 참여 연체의 공간 캐시를 준비하고 모든 후보 쌍의 양방향 접촉을 수집한 뒤 순서대로 투영합니다.
	 * 모든 쌍은 같은 위치 상태에서 접촉을 생성하며 실제 투영한 접촉만 Contacts에 추가합니다.
	 * 투영을 마치면 참여 몸체의 경계를 갱신합니다. 후보 쌍이 없으면 즉시 반환합니다.
	 * @param InOutSoftContactSolver 현재 위치의 사면체 인덱스와 표면 BVH를 갱신하고 접촉을 생성·투영합니다.
	 * @param InOutStats 이번 호출의 연체 후보 쌍·기하 검사·접촉 진단을 기존 집계에 누적합니다.
	 * @pre CollectSoftPairs로 후보 쌍과 참여 몸체를 준비하고 참조하는 몸체의 수명을 유지해야 합니다.
	 */
	/** SolveDynamic에서 준비한 몸체 쌍의 접촉 수집과 투영을 완료합니다. */
	void SolveSoftPairs(FSoftContactSolver& InOutSoftContactSolver, FSoftContactStats& InOutStats);

	/**
	 * 입자 중심의 각 성분을 [-Limit, Limit]으로 제한하고 X → Y → Z 순서로 경계 접촉을 기록합니다.
	 * 양의 경계 법선은 음의 축 방향, 음의 경계 법선은 양의 축 방향으로 월드 내부를 향합니다.
	 * @param Limit 입자 반지름을 이미 제외한 중심의 허용 반폭입니다. 단위는 장면 길이입니다.
	 * @param bRecordContacts true이면 경계에 닿은 경우도 반발용 Contacts에 추가합니다.
	 * @pre VertexIndex는 이동 가능한 입자이며 Limit는 유한한 0 이상의 값이어야 합니다.
	 */
	void SolveBoundaryContact(FBody& InOutBody, std::uint32_t VertexIndex, double Limit, bool bRecordContacts);

	/**
	 * 축 정렬 Box와 입자 중심의 최근접 점을 구하여 반지름을 포함한 겹침을 위치 보정합니다.
	 * x는 입자 중심, n은 상자 최근접 점에서 입자 중심을 향하는 단위 법선입니다.
	 * 상자 밖에서는 최근접 점까지의 거리 d와 반지름 r로 x += n*(r-d)를 적용합니다.
	 * 상자 내부·표면 또는 거리 제곱이 1E-30 이하이면 가장 가까운 면의 바깥으로 중심을 옮깁니다.
	 * @param ParticleRadius 연체 입자는 0, 구체는 양의 반지름이며 단위는 장면 길이입니다.
	 * @param OutNormal true 반환 시 상자에서 입자를 밀어내는 단위 법선입니다. false 반환 시 사용하지 않습니다.
	 * @return 접촉을 처리하면 true이며 정확히 닿아 보정량이 0인 경우도 포함합니다. 접촉 기록은 호출자가 담당합니다.
	 * @pre 입자 인덱스와 Box 형상이 유효하고 ParticleRadius는 유한한 0 이상의 값이어야 합니다.
	 */
	static bool ProjectBoxObstacle(FBody& InOutBody, std::uint32_t VertexIndex, const FObstacle& Obstacle, double ParticleRadius, FVector3D& OutNormal);

	/**
	 * A 입자와 B 표면 삼각형 사이의 법선 상대속도를 목표 값으로 맞춥니다.
	 * n은 Contact.Normal, b_i는 무게중심 좌표, wA와 wBi는 각 입자의 역질량입니다.
	 * 표면 속도 vS = Σ(b_i*vBi), 유효 역질량 W = wA + Σ(wBi*b_i*b_i)입니다.
	 * u = (vA-vS)·n, 충격량 j = (TargetNormalVelocity-u)/W로 계산합니다.
	 * 속도 보정은 vA += n*wA*j, vBi -= n*wBi*b_i*j이며 위치와 접선 속도는 변경하지 않습니다.
	 * @param TargetNormalVelocity 목표 법선 상대속도입니다. 단위는 장면 길이/초입니다.
	 * @param InverseMassA Contact.ParticleIndexA가 가리키는 A 입자의 역질량입니다.
	 * @pre Contact는 유효한 양쪽 몸체·표면 정점·무게중심 좌표·단위 법선을 가진 연체 접촉이어야 합니다.
	 * @note W <= 0이면 속도를 변경하지 않습니다.
	 */
	static void RestituteSoftContact(const FContact& Contact, double TargetNormalVelocity, double InverseMassA);

	/**
	 * 입자 쌍 또는 입자와 고정 환경 사이의 법선 상대속도를 목표 값으로 맞춥니다.
	 * n은 Contact.Normal, wA와 wB는 역질량, u = (vA-vB)·n은 현재 법선 상대속도입니다.
	 * 충격량 j = (TargetNormalVelocity-u)/(wA+wB)로 vA += n*wA*j, vB -= n*wB*j를 적용합니다.
	 * @param TargetNormalVelocity 목표 법선 상대속도입니다. 단위는 장면 길이/초입니다.
	 * @param InverseMassA Contact.ParticleIndexA가 가리키는 A 입자의 역질량입니다.
	 * @pre 접촉의 몸체·입자 인덱스·단위 법선이 유효해야 합니다. 고정 환경이면 BodyB는 nullptr입니다.
	 * @note 고정 환경의 wB와 vB는 0이며 wA+wB == 0이면 속도를 변경하지 않습니다.
	 */
	static void RestituteParticleContact(const FContact& Contact, double TargetNormalVelocity, double InverseMassA);

	/**
	 * 접촉 법선과 예측 상대속도의 법선 성분을 반발 처리용 목록에 기록합니다.
	 * @param BodyB 정적 접촉이면 nullptr이며 상대 몸체의 속도는 0으로 계산합니다.
	 */
	void RecordContact(FBody& BodyA, std::uint32_t VertexIndexA, FBody* BodyB, std::uint32_t VertexIndexB, FVector3D ContactNormal, std::uint32_t ObstacleId = 0);

	/**
	 * 겹친 입자 쌍을 역질량으로 가중하여 분리한 뒤 접촉을 기록합니다.
	 * xA, xB는 현재 위치, wA, wB는 역질량, R은 ContactRadius입니다.
	 * 거리 d = |xA - xB|, 분리 법선 n = (xA - xB) / d이며 n은 B에서 A를 향합니다.
	 * 침투량 p = max(0, R - d), 공통 보정량 s = p / (wA + wB)입니다.
	 * 위치 보정은 xA += n * wA * s, xB -= n * wB * s로 적용합니다.
	 * @param ContactRadius 최소 분리 거리입니다. 구 쌍은 반지름 합, 연체 입자와 구는 구의 반지름입니다.
	 * @pre 입자 인덱스가 유효하고 ContactRadius는 유한한 양수여야 합니다.
	 * @note 두 입자가 모두 고정되어 있으면 상태를 변경하지 않습니다.
	 * @note d > R이면 접촉을 기록하지 않습니다. d <= 1E-15이면 n = (1, 0, 0)을 사용합니다.
	 * @note d = R이면 위치 보정 없이 접촉만 기록하며, 반발 속도 보정은 Restitute에서 수행합니다.
	 */
	void SolveParticlePairContact(FBody& InOutBodyA, std::uint32_t VertexIndexA, FBody& InOutBodyB, std::uint32_t VertexIndexB, double ContactRadius);
};
