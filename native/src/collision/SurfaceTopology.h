#pragma once
#include "core/MathUtils.h"

// 외부 면과 중복을 제거한 정점을 최초 출현 순서대로 소유합니다.
// 정점 나열 방향은 사면체 입력 순서를 따르며 결과 면을 정렬하지 않습니다.
struct FSurfaceTopology
{
	// 한 사면체에만 속하는 외부 면의 정점 인덱스를 삼각형당 세 개씩 저장합니다.
	FIndices Indices;

	// Indices에 등장하는 정점의 중복 없는 목록입니다. 방향별 접촉 생성에 사용할 순회 순서를 보존합니다.
	FIndices Vertices;
};

/**
 * 원소별 인덱스 수, 사면체 정점의 중복, 면의 공유 수와 전달된 외부 표면을 검증합니다.
 * 인덱스 범위는 FSoftBody가 검증하며 이 함수는 위치 배열에 접근하지 않습니다.
 * 전달된 표면이 비어 있으면 경계를 자동으로 추출합니다.
 */
FSurfaceTopology BuildSurfaceTopology(const FIndices& Tetrahedra, const FIndices& SuppliedSurface);
