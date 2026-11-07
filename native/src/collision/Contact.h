#pragma once
#include "collision/TriangleGeometry.h"
#include "simulation/Body.h"
#include <algorithm>
#include <array>
#include <cstdint>

struct FObstacle
{
	// 호스트가 부여하는 장애물 식별자입니다. 월드에 등록할 때 0이 아니고 서로 달라야 합니다.
	std::uint32_t Id = 0;

	// true는 구, false는 축 정렬 상자 형상의 충돌 투영을 선택합니다.
	bool bSphere = false;

	// 일반 충돌 처리 외에 재진입·쿨다운 조건에 따른 범퍼 속도 보정을 허용합니다.
	bool bBumper = false;

	// 장애물 중심의 장면 좌표입니다. 상자는 회전 없이 이 중심과 Size로 정의합니다.
	FVector3D Position{};

	// 상자의 축별 전체 길이입니다. 충돌 계산에서는 절반 크기로 바꿔 사용합니다.
	FVector3D Size{1, 1, 1};

	// 구 장애물의 실제 반지름입니다. 입자 구체와의 접촉에서는 두 반지름을 더합니다.
	double Radius = 1;

	// 범퍼가 목표 법선 속도에 더할 값입니다. 단위는 장면 길이/초이며 처리 시 유효성과 상한을 확인합니다.
	double Strength = 0;
};

struct FContact
{
	// 비소유 관찰자입니다. 수집부터 투영·반발·범퍼 처리까지 몸체 수명을 유지합니다.
	// 투영과 반발의 기준이 되는 몸체입니다. 연체 접촉에서는 표면을 뚫고 들어간 입자를 소유합니다.
	FBody* BodyA = nullptr;

	// 상대 몸체의 비소유 포인터입니다. 월드 경계나 정적 장애물과의 접촉이면 nullptr입니다.
	FBody* BodyB = nullptr;

	// BodyA에서 접촉한 입자의 인덱스입니다. XYZ 성분 오프셋은 이 값의 세 배입니다.
	std::uint32_t ParticleIndexA = 0;

	// 입자 쌍 접촉에서 BodyB의 입자 인덱스입니다. 연체 표면 접촉에서는 Indices를 사용합니다.
	std::uint32_t ParticleIndexB = 0;

	// 접촉한 정적 장애물의 ID입니다. 몸체 쌍이나 월드 경계 접촉은 0으로 구분합니다.
	std::uint32_t Obstacle = 0;

	// BodyA를 겹침 밖으로 이동시키는 단위 법선입니다. 위치 투영·입사 속도·반발에 같은 방향을 사용합니다.
	FVector3D Normal{};

	// 예측 상대속도의 법선 성분입니다. 음수는 두 접촉점이 접근하는 상태를 뜻합니다.
	double IncomingNormalVelocity = 0;

	// 연체 접촉 수집 당시 입자에서 최근접 표면까지의 거리입니다. 장면 길이 단위의 침투 진단값입니다.
	double Depth = 0;

	// 연체 투영에서 법선 방향으로 확보할 추가 분리 거리입니다. 단위는 장면 길이입니다.
	double Margin = 0;

	// true는 입자 대 연체 표면 삼각형, false는 입자 쌍 또는 정적 접촉의 반발 계산을 선택합니다.
	bool bSoft = false;

	// 연체 접촉에서 BodyB의 표면 삼각형을 이루는 정점 인덱스 세 개입니다.
	std::array<std::uint32_t, 3> Indices{};

	// Indices와 같은 순서의 무게중심 좌표입니다. 상대 표면의 위치·속도와 유효 역질량을 보간합니다.
	std::array<double, 3> Weights{};
};

struct FStats
{
	// 경계 구·상자 검사를 통과한 연체 쌍의 집계입니다. 구체 쌍 검사 횟수는 포함하지 않습니다.
	double BroadPhasePairs = 0;

	// 사면체 경계 검사 후 실제 무게중심 좌표로 점 포함 여부를 검사한 횟수입니다.
	double CandidateTests = 0;

	// 연체 접촉의 위치 보정을 실제로 수행한 횟수입니다. 투영하지 않은 접촉은 제외합니다.
	double Contacts = 0;

	// 표면 침투를 확인하여 수집 목록에 추가한 연체 접촉 수입니다.
	double GeneratedContacts = 0;

	// 집계한 접촉 중 가장 큰 침투 깊이입니다. 다른 횟수와 달리 합이 아닌 최댓값을 유지합니다.
	double MaxPenetration = 0;

	// 일반 셀 등록 대신 초과 목록에서 조회하는 사면체 수의 집계입니다.
	double OverflowTets = 0;

	// 사면체 후보가 등록된 활성 셀 수의 집계입니다. 이름과 달리 조밀 그리드·희소 트리 경로 모두 포함합니다.
	double HashCells = 0;

	// BVH 순회 중 최근접 점을 직접 계산한 표면 삼각형 검사 횟수입니다.
	double SurfaceTriangleTests = 0;

	void Accumulate(const FStats& Stats)
	{
		CandidateTests += Stats.CandidateTests;
		Contacts += Stats.Contacts;
		GeneratedContacts += Stats.GeneratedContacts;
		MaxPenetration = std::max(MaxPenetration, Stats.MaxPenetration);
		OverflowTets += Stats.OverflowTets;
		HashCells += Stats.HashCells;
		SurfaceTriangleTests += Stats.SurfaceTriangleTests;
	}
};
