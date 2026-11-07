#pragma once
#include "core/Settings.h"
#include "simulation/Body.h"
#include <cstdint>

// 입자 저장소를 재할당하지 않고 위치 예측과 속도 복원을 수행하는 커널입니다.
class FTimeIntegrator
{
public:
	/**
	 * 이전 위치 저장 → BeginSubstep(연체 승수 초기화) → 입자 적분 → 제약 풀이 전 속도 저장을 수행합니다.
	 * @param SubstepSeconds 월드 또는 바인딩에서 검증한 양수인 초 단위 시간입니다.
	 * @note 저장소를 재할당하지 않습니다. 고정 입자는 위치를 유지하고 속도를 0으로 만듭니다.
	 * 서로 다른 물체의 호출은 병렬화할 수 있으나 Settings와 물체 목록은 변경하지 않고 같은 물체의 외부 접근은 직렬화해야 합니다.
	 * 물체별 예측이 모두 끝난 뒤 제약·충돌 단계로 넘어가야 하며, 현재 월드 호출은 직렬 루프입니다.
	 */
	static void Predict(FBody& InOutBody, const double SubstepSeconds, const FSettings& Settings);

	/**
	 * 보정된 위치 차이를 검증된 양수 SubstepSeconds로 나눠 속도를 복원하고 경계를 갱신합니다.
	 * 해당 물체의 위치·이전 위치·역질량만 읽고 속도·중심·경계·반경만 갱신합니다.
	 * @note 서로 다른 물체별로 병렬화할 수 있으나 같은 물체를 동시에 읽거나 수정하지 않아야 합니다.
	 * 모든 호출이 끝난 뒤 접촉 반발·범퍼 처리를 시작해야 합니다. 물체 목록의 생성·삭제도 완료 이후에 수행합니다.
	 */
	static void UpdateVelocities(FBody& InOutBody, const double SubstepSeconds);

private:
	static void PredictParticle(FBody& InOutBody, const std::uint32_t ParticleIndex, const double SubstepSeconds, const double DragGainPerSecond, const double VelocityAttenuation, const FSettings& Settings);
};
