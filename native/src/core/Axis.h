#pragma once
#include <array>

// 계산 축을 구분하며 AllAxes는 X → Y → Z 순서로 순회합니다.
enum class EAxis : int
{
	X = 0,
	Y = 1,
	Z = 2
};

inline constexpr std::array<EAxis, 3> AllAxes = {EAxis::X, EAxis::Y, EAxis::Z};

/** 유효한 축을 연속 XYZ 저장소의 0·1·2 인덱스로 변환합니다. */
inline constexpr int ToIndex(EAxis Axis)
{
	return static_cast<int>(Axis);
}
