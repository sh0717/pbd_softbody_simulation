#pragma once
#include "core/MathUtils.h"
#include "core/OperationResult.h"
#include <cstddef>
#include <optional>
#include <vector>

class FTimeIntegrator;
class FConstraintSolver;
class FCollisionSolver;
class FSoftContactSolver;
class FBumperSystem;

// 입자 저장소를 소유합니다. 공개 읽기는 const 참조이며 외부 편집은 입자 단위 명령을 사용합니다.
// 생성 후 배열 크기/주소는 파괴까지 유지합니다. 커널도 원소만 수정하며 재할당하지 않습니다.
// 외부에서 원소를 편집하면 UpdateBounds나 프레임 갱신 전까지 경계가 이전 값일 수 있습니다.
// 여기의 const는 C++ 읽기 계약입니다. 호환 Wasm TypedArray는 쓰기 가능한 뷰이며
// 네이티브 저장소의 주소 안정성과 JS ArrayBuffer의 성장·분리 조건은 별개입니다.
// 호스트는 재진입 없이 호출을 직렬화하며 배열 내용은 스텝·초기화·이동 시 변경됩니다.
class FBody
{
public:
	/** 생성 입력의 검증 결과입니다. 실패한 객체는 상태 조회와 파괴만 허용합니다. */
	const FOperationResult& GetInitializationResult() const noexcept
	{
		return InitializationResult;
	}

	enum class EBodyType
	{
		Soft,
		Ball
	};

	virtual ~FBody() = default;

	FBody(const FBody&) = delete;

	FBody& operator=(const FBody&) = delete;

	FBody(FBody&&) = delete;

	FBody& operator=(FBody&&) = delete;

	bool IsSoft() const noexcept
	{
		return Type == EBodyType::Soft;
	}

	std::size_t GetVertexCount() const noexcept
	{
		return InverseMass.size();
	}

	double GetRadius() const noexcept
	{
		return Radius;
	}

	const FVector3D& GetCenter() const noexcept
	{
		return Center;
	}

	const FVector3D& GetBoundsMin() const noexcept
	{
		return BoundsMin;
	}

	const FVector3D& GetBoundsMax() const noexcept
	{
		return BoundsMax;
	}

	const std::optional<FVector3D>& GetDragTarget() const noexcept
	{
		return DragTarget;
	}

	/**
	 * 유한한 드래그 목표를 저장하며 빈 값이면 드래그를 해제합니다.
	 * @note 검증 실패 시 기존 목표와 입자 상태를 보존합니다.
	 */
	FOperationResult SetDragTarget(std::optional<FVector3D> Target);

	/** 인덱스와 값을 모두 검증한 뒤 대상 입자만 변경하며 경계는 별도로 갱신합니다. */
	FOperationResult SetParticlePosition(std::size_t ParticleIndex, FVector3D Position);

	/**
	 * 속도는 장면 길이/초 단위이며 역질량은 음수가 아닌 유한 값입니다.
	 * 위치·속도·역질량 설정에 실패하면 대상 입자를 포함한 기존 저장소를 보존합니다.
	 */
	FOperationResult SetParticleVelocity(std::size_t ParticleIndex, FVector3D Velocity);

	/**
	 * 인덱스와 음수가 아닌 유한 역질량을 검증한 뒤 해당 입자만 변경합니다.
	 * @note 0은 고정 입자입니다. 실패 시 기존 저장소를 보존합니다.
	 */
	FOperationResult SetParticleInverseMass(std::size_t ParticleIndex, double InInverseMass);

	const std::vector<double>& GetPositions() const noexcept
	{
		return Positions;
	}

	const std::vector<double>& GetRestPositions() const noexcept
	{
		return RestPositions;
	}

	const std::vector<double>& GetPreviousPositions() const noexcept
	{
		return PreviousPositions;
	}

	const std::vector<double>& GetVelocities() const noexcept
	{
		return Velocities;
	}

	/** 제약 풀이 전 속도 스냅샷을 빌립니다. 다음 Predict 또는 Reset/Settle까지 보존합니다. */
	const std::vector<double>& GetPreSolveVelocities() const noexcept
	{
		return PreSolveVelocities;
	}

	const std::vector<double>& GetInverseMass() const noexcept
	{
		return InverseMass;
	}

	/** 현재 입자 위치를 기준으로 파생 타입의 경계를 갱신합니다. */
	virtual void UpdateBounds() = 0;

	/** 서브스텝 시작 시 경계를 갱신하며 연체는 제약 승수도 초기화합니다. */
	virtual void BeginSubstep();

	/** 재할당 없이 현재·이전 위치를 기준 형상으로 복원하고 속도와 드래그 목표를 초기화합니다. */
	virtual void Reset();

	/** 현재 위치와 경계만 이동합니다. 유한하지 않은 목표 위치는 수정 전에 거부합니다. */
	FOperationResult MoveTo(FVector3D Target);

protected:
	// 생성자에서 검증 실패를 저장합니다. 월드는 실패한 몸체를 등록하지 않습니다.
	FOperationResult InitializationResult;

	FBody(EBodyType InType, std::vector<double> Vertices, FVector3D PositionOffset, std::vector<double> InverseMasses, FVector3D Velocity);

	/** 파생 클래스가 계산한 경계 결과만 발행하며 입자 저장소는 변경하지 않습니다. */
	void PublishBounds(FVector3D InCenter, FVector3D Minimum, FVector3D Maximum, double InRadius) noexcept;

	/** 적분 직후 속도를 제약 풀이 전 스냅샷에 복사합니다. 저장소를 재할당하지 않습니다. */
	void SavePreSolveVelocities() noexcept;

private:
	/** 인덱스와 벡터 전체를 검증한 뒤 대상 입자의 XYZ만 변경합니다. 검증 실패 시 저장소를 보존합니다. */
	FOperationResult SetParticleVector(std::vector<double>& InOutValues, std::size_t ParticleIndex, const FVector3D& Value);

	friend class FTimeIntegrator;

	friend class FConstraintSolver;

	friend class FCollisionSolver;

	friend class FSoftContactSolver;

	friend class FBumperSystem;

	// 연체와 단일 입자 구체를 구분하며 적분·충돌 처리 경로를 선택합니다.
	EBodyType Type;

	// 입자별 위치 작업 배열입니다([X0,Y0,Z0,X1,Y1,Z1,...], 장면 길이). 예측과 제약 투영이 순서대로 같은 배열을 갱신합니다.
	std::vector<double> Positions;

	// 생성 시 위치 오프셋까지 반영한 기준 위치입니다. MoveTo로 변경하지 않으며 Reset의 복원 원본입니다.
	std::vector<double> RestPositions;

	// 위치 예측 전에 보관한 이전 위치입니다. 제약 투영 후 위치 차이로 속도를 복원하는 데 사용합니다.
	std::vector<double> PreviousPositions;

	// 입자별 속도 작업 배열입니다(XYZ, 장면 길이/초). 적분 → 위치 차이로 복원 → 반발·범퍼 순서로 갱신합니다.
	std::vector<double> Velocities;

	// 중력·감쇠 또는 드래그를 반영한 적분 직후 속도입니다(XYZ, 장면 길이/초). 제약 풀이 전에 저장하고 접촉의 입사 속도 계산에 사용합니다.
	std::vector<double> PreSolveVelocities;

	// 입자별 역질량 가중치입니다. 입자 i는 원소 i에 대응하며 0이면 고정 입자로 처리합니다.
	std::vector<double> InverseMass;

	// 연체는 축 정렬 경계 상자의 중심, 구체는 단일 입자 위치입니다. 질량 중심을 계산한 값은 아닙니다.
	FVector3D Center{};

	// 연체의 현재 축 정렬 경계 상자 최솟값입니다. 구체의 UpdateBounds는 이 값을 재계산하지 않습니다.
	FVector3D BoundsMin{};

	// 연체의 현재 축 정렬 경계 상자 최댓값입니다. 연체 쌍의 광역 충돌 검사에 사용합니다.
	FVector3D BoundsMax{};

	// 연체는 Center에서 가장 먼 입자까지의 거리, 구체는 생성 시 지정한 실제 반지름입니다.
	double Radius = 0;

	// 몸체 중심을 유도할 장면 좌표입니다. 값이 없으면 드래그 대신 중력·감쇠 경로로 적분합니다.
	std::optional<FVector3D> DragTarget;
};
