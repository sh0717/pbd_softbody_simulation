#pragma once
#include "simulation/Body.h"

// 테트라 메시, 기준 형상과 XPBD 라그랑주 승수를 소유합니다.
// 입력 토폴로지와 역질량은 생성 시 검증하며 이후 인덱스·기준 형상 배열은 고정입니다.
// 기준 길이는 장면 길이 단위이며 기준 체적은 입력 정점 순서에 따른 부호 있는 길이의 세제곱입니다.
// BeginSubstep과 ResetLambdas가 승수를 0으로 만들고 각 솔버 반복에서 누적합니다.
// Reset은 토폴로지·재질을 유지하며 초기 위치·경계와 속도 0·승수 0을 복구합니다.
class FSoftBody final : public FBody
{
public:
	/**
	 * 입자·변·사면체·표면 배열을 소유하고 기준 길이·체적과 제약 승수를 초기화합니다.
	 * @param InverseMasses 정점별 역질량입니다. 빈 배열이면 모든 정점에 1을 적용합니다.
	 * @note 인덱스 묶음 크기와 범위는 기준 형상 계산 전에 검증합니다.
	 */
	FSoftBody(std::vector<double> Vertices, FIndices Edges, FIndices Tetrahedra, FIndices Surface, FVector3D PositionOffset = {}, std::vector<double> InverseMasses = {}, FVector3D Velocity = {});

	/** 현재 모든 입자로 상자 경계·중심과 중심 기준 최대 반지름을 다시 계산합니다. */
	void UpdateBounds() override;

	/** 거리·체적 제약 승수를 0으로 만들고 현재 형상의 경계를 갱신합니다. */
	void BeginSubstep() override;

	/** 기준 위치·이전 위치·경계를 복원하고 속도·드래그 목표·승수를 초기화합니다. */
	void Reset() override;

	/** 할당 용량과 토폴로지를 유지한 채 거리·체적 제약 승수만 0으로 만듭니다. */
	void ResetLambdas();

	const FIndices& GetEdgeIndices() const noexcept
	{
		return EdgeIndices;
	}

	const FIndices& GetTetIndices() const noexcept
	{
		return TetIndices;
	}

	const FIndices& GetSurfaceIndices() const noexcept
	{
		return SurfaceIndices;
	}

	const std::vector<double>& GetRestLengths() const noexcept
	{
		return RestLengths;
	}

	const std::vector<double>& GetRestVolumes() const noexcept
	{
		return RestVolumes;
	}

	const std::vector<double>& GetTetLimits() const noexcept
	{
		return TetLimits;
	}

	const std::vector<double>& GetEdgeLambdas() const noexcept
	{
		return EdgeLambdas;
	}

	const std::vector<double>& GetVolumeLambdas() const noexcept
	{
		return VolumeLambdas;
	}

private:
	/** 입력 변 순서대로 생성 시 위치에서 기준 길이를 계산하고 저장합니다. */
	void InitializeRestEdgeLengths();

	/** 사면체 순서대로 부호 있는 기준 체적과 최대 변 길이에 따른 보정 한도를 저장합니다. */
	void InitializeRestVolumesAndLimits();

	friend class FConstraintSolver;

	// 변 하나당 두 정점 인덱스를 연속 저장합니다. 변 e는 [2*e, 2*e+1]에 대응합니다.
	FIndices EdgeIndices;

	// 사면체 하나당 네 정점 인덱스를 저장합니다. 사면체 t의 첫 원소는 4*t입니다.
	FIndices TetIndices;

	// 입력 표면 삼각형의 정점 인덱스를 세 개씩 저장합니다. 빈 입력의 경계 추출은 접촉 캐시가 담당합니다.
	FIndices SurfaceIndices;

	// 변별 기준 길이입니다. RestLengths[e]는 EdgeIndices의 e번째 변에 대응하며 장면 길이 단위입니다.
	std::vector<double> RestLengths;

	// 사면체별 기준 체적입니다. RestVolumes[t]는 t번째 사면체의 정점 순서에 따른 부호를 보존합니다.
	std::vector<double> RestVolumes;

	// 변별 거리 제약의 누적 라그랑주 승수입니다. 서브스텝 시작·ResetLambdas에서 0으로 초기화합니다.
	std::vector<double> EdgeLambdas;

	// 사면체별 C=6(V-V0)의 누적 λ입니다. 같은 서브스텝의 반복 사이에 유지하며 시작·ResetLambdas에서 0으로 만듭니다.
	// 물리 체적 V-V0 기준 승수의 1/6이며 위치 보정에는 이번 Δλ만 사용합니다.
	std::vector<double> VolumeLambdas;

	// 사면체별 위치 보정 거리 상한입니다. 생성 시 여섯 변 중 최대 길이를 저장하며 체적 승수 보정에 사용합니다.
	std::vector<double> TetLimits;
};
