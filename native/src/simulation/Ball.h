#pragma once
#include "simulation/Body.h"

// 단일 입자의 위치와 반지름으로 구체 경계를 갱신합니다.
class FBall final : public FBody
{
public:
	/**
	 * 초기 위치·속도·역질량을 가진 단일 입자 구체를 생성합니다.
	 * @param Radius 장면 길이 단위의 유한한 양수 반지름입니다.
	 * @param InverseMass 음수가 아닌 유한 값이며 0이면 고정 입자입니다.
	 * @note 위치·속도는 각각 장면 길이, 장면 길이/초 단위입니다. 잘못된 입력은 GetInitializationResult로 반환하며 실패한 객체는 등록하지 않습니다.
	 */
	FBall(double Radius, FVector3D Position, FVector3D Velocity = {}, double InverseMass = 1);

	/**
	 * 현재 입자 위치를 중심으로 발행하며 반지름은 유지합니다.
	 * @note 위치·속도와 생성 시 초기화한 상자 경계는 변경하지 않습니다.
	 */
	void UpdateBounds() override;
};
