#pragma once
#include "collision/BumperSystem.h"
#include "collision/CollisionSolver.h"
#include "collision/SoftContactSolver.h"
#include "simulation/Ball.h"
#include "simulation/ConstraintSolver.h"
#include "simulation/ParallelExecutor.h"
#include "simulation/SoftBody.h"
#include "simulation/TimeIntegrator.h"
#include <memory>
#include <vector>

/**
 * 물체와 하위 시스템을 소유하고 서브스텝과 솔버 실행 순서를 결정합니다.
 * @note 반환하는 물체 포인터는 관찰자이며 월드 파괴 시 무효화됩니다.
 * 물체 추가 시 기존 물체 주소는 유지됩니다. 호출자는 월드와 솔버를 직렬로 사용합니다.
 */
class FPhysicsWorld
{
public:
	/** 작업자 상한이 0이면 순차 실행합니다. 외부 호출은 내부 병렬화 여부와 무관하게 직렬화합니다. */
	explicit FPhysicsWorld(std::size_t WorkerLimit = FParallelExecutor::DefaultWorkerCount())
		: ConstraintExecutor(WorkerLimit)
	{
	}

	/** 제약 계산용으로 생성된 백그라운드 스레드 수입니다. 호출 스레드는 제외합니다. */
	std::size_t GetConstraintWorkerCount() const noexcept
	{
		return ConstraintExecutor.GetWorkerCount();
	}

	/**
	 * 연체를 생성하여 월드가 단독 소유하고 비소유 관찰 포인터를 반환합니다.
	 * @note 월드가 파괴되면 반환 포인터와 모든 입자 뷰가 무효화됩니다.
	 */
	FSoftBody* AddSoft(std::vector<double> Positions, FIndices Edges, FIndices Tetrahedra, FIndices Surface, FVector3D Offset = {}, std::vector<double> InverseMasses = {}, FVector3D Velocity = {}, FOperationResult* OutResult = nullptr);

	/** 단일 입자 구체를 생성하여 월드가 소유하고 관찰 포인터를 반환합니다. */
	FBall* AddBall(double Radius, FVector3D Position, FVector3D Velocity = {}, double InverseMass = 1, FOperationResult* OutResult = nullptr);

	/**
	 * 검증한 초 단위 Dt만큼 위치 예측 → 제약·접촉 반복 → 속도 복원·반발·범퍼를 진행합니다.
	 * @return 양수 스텝 성공은 true, 유효한 dt=0은 false입니다. 검증 실패는 OutResult로 반환하며
	 * dt=0과 검증 실패 모두 입자·승수·통계·충돌 이벤트·시간을 보존합니다.
	 * @note 동적 할당 자원 소진은 프로세스 종료 정책을 따릅니다.
	 */
	bool Step(double Dt, FOperationResult* OutResult = nullptr);

	/** 시뮬레이션 시간을 진행하지 않고 겹침을 해소합니다. 이동한 입자의 속도만 0으로 만듭니다. */
	void Settle();

	/**
	 * 몸체 토폴로지·설정·장애물·관찰 주소를 유지하며 기준 상태로 복구합니다.
	 * 시간·통계·충돌 이벤트·접촉 임시 저장소를 초기화하며 연체 캐시는 다음 준비 때 갱신합니다.
	 */
	void Reset();

	/**
	 * 후보 전체와 후보 Dt를 검증한 뒤 교체합니다. 실패 시 설정과 실행 상태를 보존합니다.
	 * 성공 시에도 입자·승수·통계·충돌 이벤트·시간은 유지하며 다음 실행부터 새 값을 사용합니다.
	 */
	FOperationResult Configure(const FSettings& InSettings);

	const FSettings& GetSettings() const noexcept
	{
		return Settings;
	}

	const std::vector<FObstacle>& GetObstacles() const noexcept
	{
		return Obstacles;
	}

	/**
	 * 모든 Box 장애물의 0이 아닌 고유 ID, 유한한 중심과 양수 축 길이를 검증한 뒤 목록을 교체합니다.
	 * @note 검증 실패 시 기존 장애물을 보존하며 범퍼 강도는 범퍼 처리 시 검사합니다.
	 */
	FOperationResult SetObstacles(std::vector<FObstacle> InObstacles);

	const std::vector<std::unique_ptr<FBody>>& GetBodies() const noexcept
	{
		return Bodies;
	}

	FSoftContactSolver& GetSoftContacts() noexcept
	{
		return SoftSolver;
	}

	const FSoftContactStats& GetStats() const noexcept
	{
		return Stats;
	}

	/** 다음 양수 Step, Reset 또는 월드 파괴 전까지 유효한 읽기 전용 대여입니다. */
	const std::vector<FHit>& GetHits() const noexcept
	{
		return Bumpers.GetHits();
	}

	double GetSimulationTime() const noexcept
	{
		return SimulationTime;
	}

private:
	/** Settings와 SubstepSeconds > 0, 유한한 1/(SubstepSeconds*SubstepSeconds)가 Step에서 검증되어야 합니다. */
	FOperationResult AdvanceSubstep(double SubstepSeconds);

	FOperationResult SolveIteration(double SubstepSeconds);

	// 다음 스텝부터 사용할 검증된 물리 설정입니다. Configure는 입자 상태를 직접 변경하지 않습니다.
	FSettings Settings;

	// 월드의 모든 몸체를 단독 소유합니다. 벡터 확장 시에도 각 몸체 객체의 주소는 유지됩니다.
	std::vector<std::unique_ptr<FBody>> Bodies;

	// 현재 정적 장애물 설정을 값으로 소유합니다. 충돌·범퍼가 같은 목록을 읽습니다.
	std::vector<FObstacle> Obstacles;

	// 서브스텝 접촉과 동적·정적 충돌의 재사용 저장소를 소유합니다.
	FCollisionSolver Collisions;

	// 연체별 공간 셀 인덱스와 표면 BVH를 소유하며 연체 쌍의 접촉을 생성·투영합니다.
	FSoftContactSolver SoftSolver;

	// 몸체·범퍼별 쿨다운과 이번 프레임의 발동 이벤트를 소유합니다.
	FBumperSystem Bumpers;

	// 이번 양수 Step의 연체 충돌 진단을 서브스텝·반복에 걸쳐 누적합니다. 몸체별 고유 개수와 구분합니다.
	FSoftContactStats Stats;

	// 완료한 서브스텝의 누적 시간입니다. 단위는 초이며 Reset에서 0으로 복원합니다.
	double SimulationTime = 0;

	// SolveIteration 동안 Bodies가 소유한 연체를 빌립니다. 몸체 추가 후 다음 반복에서 다시 구성합니다.
	std::vector<FSoftBody*> ConstraintBodies;

	// 마지막에 선언하여 몸체와 빌린 목록보다 먼저 작업자를 종료·join합니다.
	FParallelExecutor ConstraintExecutor;
};
