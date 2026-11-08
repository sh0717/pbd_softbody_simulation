#pragma once
#include "core/Vector3D.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using FIndices = std::vector<std::uint32_t>;

/**
 * 연속 XYZ 저장소에서 입자 한 개의 위치나 속도를 독립 벡터 값으로 읽습니다.
 * @pre VertexIndex에 해당하는 세 성분이 Values 범위 안에 있어야 합니다.
 */
inline FVector3D ReadParticleVector(const std::vector<double>& Values, const std::uint32_t VertexIndex)
{
	// 입자 인덱스를 연속 XYZ 배열의 첫 성분 오프셋으로 바꾼 값입니다.
	const std::size_t VertexOffset = std::size_t(VertexIndex) * 3;
	return {Values[VertexOffset], Values[VertexOffset + 1], Values[VertexOffset + 2]};
}

/**
 * 연속 XYZ 저장소의 한 입자에 벡터를 X → Y → Z 순서로 더합니다.
 * @pre VertexIndex에 해당하는 세 성분이 유효해야 합니다. 저장소를 재할당하지 않습니다.
 */
inline void AddParticleVector(std::vector<double>& InOutValues, const std::uint32_t VertexIndex, const FVector3D& DeltaVector)
{
	// 입자 인덱스를 연속 XYZ 배열의 첫 성분 오프셋으로 바꾼 값입니다.
	const std::size_t VertexOffset = std::size_t(VertexIndex) * 3;

	for (EAxis Axis : AllAxes)
	{
		InOutValues[VertexOffset + ToIndex(Axis)] += DeltaVector[Axis];
	}
}

/** 네 정점이 유효해야 합니다. 정점 순서에 따른 부호 있는 체적을 반환합니다. */
inline double CalculateSignedTetrahedronVolume(const std::vector<double>& Positions, const std::uint32_t VertexA = 0, const std::uint32_t VertexB = 1, const std::uint32_t VertexC = 2, const std::uint32_t VertexD = 3)
{
	return (ReadParticleVector(Positions, VertexB) - ReadParticleVector(Positions, VertexA)).Dot((ReadParticleVector(Positions, VertexC) - ReadParticleVector(Positions, VertexA)).Cross(ReadParticleVector(Positions, VertexD) - ReadParticleVector(Positions, VertexA))) / 6;
}
