#pragma once
#include "core/MathUtils.h"

/** 현재 사면체 하나의 역기저와 여유 경계를 소유하며 점 포함 검사를 수행합니다. */
class FTetrahedronGeometry
{
public:
	/** 네 정점에서 역기저와 경계를 갱신합니다. 퇴화 형상은 유효하지 않은 상태로 남깁니다. */
	void Update(FVector3D Vertex0, FVector3D Vertex1, FVector3D Vertex2, FVector3D Vertex3, double Epsilon);

	bool IsValid() const noexcept
	{
		return bValid;
	}

	const FVector3D& GetBoundsMin() const noexcept
	{
		return BoundsMin;
	}

	const FVector3D& GetBoundsMax() const noexcept
	{
		return BoundsMax;
	}

	/** 유효한 사면체의 여유 경계 안에 있는지 검사합니다. */
	bool IsPointWithinBounds(FVector3D QueryPoint) const;

	/** 무게중심 좌표 네 개에 상대 허용 오차를 적용합니다. 유효하지 않은 형상은 false입니다. */
	bool ContainsPoint(FVector3D QueryPoint, double Epsilon) const;

private:
	// 현재 0번 정점 위치입니다. 질의 점의 로컬 좌표를 구할 때 뺍니다.
	FVector3D Origin{};

	// 역기저의 첫 행이며 1번 정점의 무게중심 좌표를 계산합니다.
	FVector3D BarycentricRowU{};

	// 역기저의 둘째 행이며 2번 정점의 무게중심 좌표를 계산합니다.
	FVector3D BarycentricRowV{};

	// 역기저의 셋째 행이며 3번 좌표를 계산합니다. 0번 좌표는 1-U-V-W입니다.
	FVector3D BarycentricRowW{};

	// 현재 정점 경계에서 상대 허용 오차만큼 확장한 최솟값입니다.
	FVector3D BoundsMin{};

	// 현재 정점 경계에서 상대 허용 오차만큼 확장한 최댓값입니다.
	FVector3D BoundsMax{};

	// 마지막 Update가 유한한 행렬식과 퇴화 기준을 통과했는지 나타냅니다.
	bool bValid = false;
};
