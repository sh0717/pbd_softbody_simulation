#pragma once
#include "core/Settings.h"
#include "simulation/SoftBody.h"
#include <cstddef>

/**
 * 상태 없는 재질 제약 커널입니다. 승수 저장소는 각 연체가 소유하며 정적 함수로 투영합니다.
 * 공통 기호: x_i=현재 입자 위치, w_i=역질량(고정 입자는 0), h=SubstepSeconds, g_i=∇x_i C.
 * D=Σ_i(w_i*|g_i|²), α_C=해당 C 정의에 맞춘 유연도, α!=α_C/h²입니다.
 * PBD: Δλ=-PbdStiffness*C/D.
 * XPBD: Δλ=(-C-α!*λ)/(D+α!) , λ←λ+Δλ, Δx_i=w_i*g_i*Δλ.
 * λ는 서브스텝 시작에 0으로 만들고 같은 서브스텝의 반복 사이에는 유지합니다.
 * @note DeltaLambda는 이번 변화량이며 누적 λ 전체를 입자에 다시 적용하지 않습니다.
 */
class FConstraintSolver
{
public:
	/**
	 * 거리 제약을 입력 순서대로 투영하며 현재 서브스텝의 승수를 누적합니다.
	 * x1=VertexA, x2=VertexB, l=|x1-x2|, l0=RestLength, C=l-l0, n=(x1-x2)/l.
	 * g1=n, g2=-n, D=w1+w2, α_C=EdgeCompliance입니다.
	 * PBD: Δx1=-k*w1/(w1+w2)*(l-l0)*n, Δx2=+k*w2/(w1+w2)*(l-l0)*n, k=PbdStiffness.
	 * XPBD: 공통 Δλ 식을 적용하여 Δx1=w1*n*Δλ, Δx2=-w2*n*Δλ로 보정합니다.
	 * @param SubstepSeconds 양수인 초 단위 시간이며 유연도/h²가 유한해야 합니다.
	 * @pre 호출자가 설정과 ValidateSubstep의 성공을 보장해야 합니다.
	 * @note 시간·유연도 계약 위반은 입자를 수정하기 전에 std::abort로 종료합니다.
	 * 위치·승수 원소만 수정하며 배열 크기와 토폴로지는 유지합니다. 고정 입자는 이동하지 않습니다.
	 * 구현의 EdgeNormal=(x2-x1)/l은 위 n의 반대 방향이며, 붕괴한 변은 기준 형상의 방향을 사용합니다.
	 */
	static void SolveEdges(FSoftBody& InOutBody, const double SubstepSeconds, const FSettings& Settings);

	/**
	 * 부호 있는 체적 제약을 입력 순서대로 투영하며 보정 한도와 승수 누적을 적용합니다.
	 * x0..x3=TetVertices[0..3], V0=RestVolumes[TetIndex]인 물리 체적입니다.
	 * C=6(V-V0)=[(x1-x0)×(x2-x0)]·(x3-x0)-6V0.
	 * g1=(x2-x0)×(x3-x0), g2=(x3-x0)×(x1-x0), g3=(x1-x0)×(x2-x0), g0=-(g1+g2+g3).
	 * D=Σ_i(w_i*|g_i|²)이며 공통 Δλ 식과 Δx_i=w_i*g_i*Δλ를 사용합니다.
	 * α_C=36*VolumeCompliance로 환산하여 물리 체적 기준 설정의 탄성 에너지를 유지합니다.
	 * 이 C의 누적 λ는 물리 체적 V-V0을 제약으로 쓸 때의 1/6이며, RestVolumes의 단위는 바꾸지 않습니다.
	 * 시간·실패·저장소 계약은 SolveEdges와 같습니다. 정점 순서에 따른 체적 부호를 보존합니다.
	 * @note 하나의 사면체의 모든 기울기는 같은 현재 위치에서 평가한 뒤 입자를 이동합니다.
	 * 한 번의 최대 입자 이동이 TetLimits를 넘으면 Δλ를 줄이며 실제 적용한 값만 누적합니다.
	 */
	static void SolveVolumes(FSoftBody& InOutBody, const double SubstepSeconds, const FSettings& Settings);

private:
	/** 단일 제약의 투영을 완료한 뒤 다음 제약을 평가합니다. 배열 크기는 변경하지 않습니다. */
	static void ProjectEdge(FSoftBody& InOutBody, const std::size_t EdgeIndex, const double ScaledCompliance, const double ProjectionStiffness);

	/** 현재 형상에서 모든 기울기를 평가한 뒤 보정 제한과 승수 누적을 적용합니다. */
	static void ProjectVolume(FSoftBody& InOutBody, const std::size_t TetIndex, const double ScaledCompliance, const double ProjectionStiffness);
};
