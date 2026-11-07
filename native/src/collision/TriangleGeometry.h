#pragma once
#include "core/MathUtils.h"

// 무게중심 좌표는 입력 정점 순서이며 DistanceSquared는 장면 길이의 제곱 단위입니다.
struct FTriangleClosestPoint
{
	// 입력 정점 A·B·C에 대한 최근접 점의 무게중심 좌표입니다. 같은 순서로 위치를 보간합니다.
	std::array<double, 3> Weights{};

	// 질의 점에서 삼각형 또는 퇴화한 경계 선분에 내린 최근접 장면 좌표입니다.
	FVector3D Position{};

	// 질의 점과 Position 사이 거리의 제곱입니다. 제곱근 없이 후보의 근접 순서를 비교합니다.
	double DistanceSquared = 0;
};

/**
 * 유한한 입력이 필요합니다. 퇴화 삼각형은 AB·BC·CA 선분으로 축소합니다.
 * 같은 거리에서는 먼저 검사한 선분을 유지하며 반환값은 몸체 수명과 독립적입니다.
 */
FTriangleClosestPoint FindClosestPointOnTriangle(FVector3D QueryPoint, FVector3D VertexA, FVector3D VertexB, FVector3D VertexC);
