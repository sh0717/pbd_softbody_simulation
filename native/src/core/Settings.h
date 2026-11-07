#pragma once
#include "core/Vector3D.h"

struct FSettings
{
	// 호스트가 기본 스텝 간격으로 사용하는 초 단위 시간입니다. 실제 Step 호출은 전달받은 Dt를 사용합니다.
	double Dt = 1.0 / 60;

	// XPBD 거리 제약의 유연도입니다. 0이면 단단한 제약이며 솔버는 서브스텝 시간의 제곱으로 나눠 사용합니다.
	double EdgeCompliance = 1e-6;

	// 물리 체적 V-V0 기준의 XPBD 유연도입니다. 솔버는 C=6(V-V0)에 맞춰 36*VolumeCompliance/h²로 환산합니다.
	double VolumeCompliance = 1e-8;

	// PBD 보정량에 곱하는 [0,1] 강성입니다. XPBD 경로에서는 보정 배율 1을 사용합니다.
	double PbdStiffness = 1;

	// 원점 중심 월드 경계의 축별 반폭입니다. 구체 중심의 허용 범위는 여기서 반지름을 뺍니다.
	double Bounds = 10;

	// 접근 중인 법선 속도를 반전·감쇠하는 [0,1] 반발 계수입니다.
	double Restitution = 0.5;

	// 속도에 exp(-Damping*h)를 적용하는 음수가 아닌 감쇠율입니다. 단위는 1/초입니다.
	double Damping = 0;

	// 한 스텝을 나누는 양수 구간 수입니다. 각 적분 시간은 실제 Dt/Substeps입니다.
	int Substeps = 3;

	// 각 서브스텝에서 거리·체적·충돌 제약을 반복 투영하는 횟수입니다.
	int Iterations = 2;

	// true이면 유연도와 누적 승수를 사용하는 XPBD 계수, false이면 PBD 강성 계수를 선택합니다.
	bool bXpbd = true;

	// 입자 속도에 더할 중력 가속도입니다. 단위는 장면 길이/초²이며 드래그 중에는 해당 적분 경로를 생략합니다.
	FVector3D Gravity{};

	/**
	 * 설정 범위와 초 단위 스텝 간격을 검증하며 잘못된 값은 예외로 거부합니다.
	 * @note StepSeconds는 0을 허용합니다. 양수 서브스텝의 파생 계수는 ValidateSubstep에서 검증합니다.
	 */
	void Validate(double StepSeconds) const;

	/** 위치 예측이나 제약 직접 투영 전에 시간 간격으로 계산한 계수를 검증합니다. */
	void ValidateSubstep(double SubstepSeconds) const;
};
