#include "collision/SoftContactSolver.h"
#include "collision/SurfaceTopology.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

void FContactOptions::Validate() const
{
	if ((CellSize != 0 && (!std::isfinite(CellSize) || CellSize <= 0)) || MaxCells < 1 || !std::isfinite(Epsilon) || Epsilon <= 0 || !std::isfinite(Margin) || Margin < 0)
	{
		throw std::range_error("Invalid contact options");
	}
}

void FSoftContactSolver::Configure(const FContactOptions& InOptions)
{
	InOptions.Validate();
	Options = InOptions;
	BodyCaches.clear();
	Stats = {};
}

FSoftContactSolver::FBodySpatialCache FSoftContactSolver::CreateBodySpatialCache(FSoftBody* Body)
{
	FSurfaceTopology Surface = BuildSurfaceTopology(Body->GetTetIndices(), Body->GetSurfaceIndices());
	return {FTetrahedronSpatialIndex(*Body, Options.CellSize, Options.Epsilon, Options.MaxCells), FSurfaceBvh(std::move(Surface), Body->GetRestPositions())};
}

void FSoftContactSolver::RefitBodySpatialCache(FBodySpatialCache& InOutBodyCache, FSoftBody& Body)
{
	// 사면체 포함 검사와 최근접 외부 면 검색은 같은 위치 상태를 사용해야 합니다.
	InOutBodyCache.TetrahedronIndex.Rebuild(Body);
	InOutBodyCache.SurfaceBvh.Refit(Body.GetPositions());
}

FSoftContactSolver::FBodySpatialCache& FSoftContactSolver::PrepareBody(FSoftBody& Body)
{
	const auto Existing = BodyCaches.find(&Body);
	if (Existing != BodyCaches.end())
	{
		RefitBodySpatialCache(Existing->second, Body);
		return Existing->second;
	}
	// 공간 셀 인덱스와 BVH를 모두 사용할 수 있을 때까지 새 토폴로지는 외부에 등록하지 않습니다.
	FBodySpatialCache Candidate = CreateBodySpatialCache(&Body);
	RefitBodySpatialCache(Candidate, Body);
	return BodyCaches.emplace(&Body, std::move(Candidate)).first->second;
}

void FSoftContactSolver::Prepare(const std::vector<FSoftBody*>& Bodies)
{
	for (const FSoftBody* Body : Bodies)
	{
		if (!Body)
		{
			throw std::invalid_argument("Null contact body");
		}
	}
	FStats PreparedStats{};
	for (FSoftBody* Body : Bodies)
	{
		const FBodySpatialCache& BodyCache = PrepareBody(*Body);
		PreparedStats.OverflowTets += static_cast<double>(BodyCache.TetrahedronIndex.GetOverflowTetrahedronCount());
		PreparedStats.HashCells += static_cast<double>(BodyCache.TetrahedronIndex.GetActiveCellCount());
	}
	// 요청한 모든 몸체의 준비가 끝난 경우에만 일괄 진단 정보를 교체합니다.
	Stats = PreparedStats;
}

FIndices FSoftContactSolver::Query(FSoftBody* Body, FVector3D QueryPoint, bool bFirst)
{
	FIndices ContainingTetrahedra;
	BodyCaches.at(Body).TetrahedronIndex.QueryContainingTetrahedra(QueryPoint, &ContainingTetrahedra, bFirst, Stats.CandidateTests);
	return ContainingTetrahedra;
}

void FSoftContactSolver::CollectDirectionalContacts(FSoftBody* SourceBody, FSoftBody* TargetBody, std::vector<FContact>& InOutContacts)
{
	FBodySpatialCache& TargetEntry = BodyCaches.at(TargetBody);
	const FBodySpatialCache& SourceEntry = BodyCaches.at(SourceBody);
	if (TargetEntry.SurfaceBvh.IsEmpty())
	{
		return;
	}
	const bool bHasSurfaceVertices = SourceEntry.SurfaceBvh.GetSurfaceVertexCount() > 0;
	std::size_t SurfaceVertexCount = SourceBody->GetVertexCount();
	if (bHasSurfaceVertices)
	{
		SurfaceVertexCount = SourceEntry.SurfaceBvh.GetSurfaceVertexCount();
	}
	for (std::size_t SurfaceVertexIndex = 0; SurfaceVertexIndex < SurfaceVertexCount; ++SurfaceVertexIndex)
	{
		std::uint32_t SourceVertexIndex = static_cast<std::uint32_t>(SurfaceVertexIndex);
		if (bHasSurfaceVertices)
		{
			SourceVertexIndex = SourceEntry.SurfaceBvh.GetSurfaceVertexIndex(SurfaceVertexIndex);
		}
		const FVector3D SourcePosition = ReadParticleVector(SourceBody->GetPositions(), SourceVertexIndex);
		if (!TargetEntry.TetrahedronIndex.QueryContainingTetrahedra(SourcePosition, nullptr, true, Stats.CandidateTests))
		{
			continue;
		}
		const std::optional<FSurfaceClosestPoint> SurfaceHit = TargetEntry.SurfaceBvh.FindNearestPoint(SourcePosition, TargetBody->GetPositions(), Stats.SurfaceTriangleTests);
		if (!SurfaceHit)
		{
			continue;
		}
		// 대상 표면의 최근접 점과 그 삼각형의 무게중심 좌표입니다. 위치·속도 보간에 같은 좌표를 사용합니다.
		const FTriangleClosestPoint& ClosestPoint = SurfaceHit->ClosestPoint;
		// 포함된 입자에서 외부 표면까지의 거리입니다. 너무 작은 침투는 허용 오차로 걸러냅니다.
		const double PenetrationDepth = std::sqrt(ClosestPoint.DistanceSquared);
		if (!(PenetrationDepth > TargetEntry.TetrahedronIndex.GetCellSize() * Options.Epsilon))
		{
			continue;
		}
		// 접촉은 현재 기하 스냅샷에서 구성하고 모든 몸체 쌍의 수집이 끝난 뒤 투영합니다.
		FContact Contact;
		Contact.BodyA = SourceBody;
		Contact.ParticleIndexA = SourceVertexIndex;
		Contact.BodyB = TargetBody;
		Contact.bSoft = true;
		Contact.Depth = PenetrationDepth;
		Contact.Margin = Options.Margin;
		Contact.Normal = (ClosestPoint.Position - SourcePosition) * (1 / PenetrationDepth);
		Contact.Weights = ClosestPoint.Weights;
		FVector3D RelativeVelocity = ReadParticleVector(SourceBody->GetPreSolveVelocities(), SourceVertexIndex);
		for (int TriangleCornerIndex = 0; TriangleCornerIndex < 3; ++TriangleCornerIndex)
		{
			Contact.Indices[TriangleCornerIndex] = SurfaceHit->VertexIndices[TriangleCornerIndex];
			RelativeVelocity = RelativeVelocity - ReadParticleVector(TargetBody->GetPreSolveVelocities(), Contact.Indices[TriangleCornerIndex]) * Contact.Weights[TriangleCornerIndex];
		}
		Contact.IncomingNormalVelocity = RelativeVelocity.Dot(Contact.Normal);
		InOutContacts.push_back(Contact);
		++Stats.GeneratedContacts;
		Stats.MaxPenetration = std::max(Stats.MaxPenetration, PenetrationDepth);
	}
}

void FSoftContactSolver::Generate(FSoftBody* SourceBody, FSoftBody* TargetBody, std::vector<FContact>& InOutContacts)
{
	CollectDirectionalContacts(SourceBody, TargetBody, InOutContacts);
	CollectDirectionalContacts(TargetBody, SourceBody, InOutContacts);
}

bool FSoftContactSolver::Project(const FContact& Contact)
{
	FVector3D RelativePosition = ReadParticleVector(Contact.BodyA->GetPositions(), Contact.ParticleIndexA);
	const double SourceInverseMass = Contact.BodyA->GetInverseMass()[Contact.ParticleIndexA];

	// 소스 역질량에 대상 삼각형의 Σ(역질량_i*무게중심좌표_i²)를 더해 투영의 분모를 구성합니다.
	double EffectiveInverseMass = SourceInverseMass;
	for (int TriangleCornerIndex = 0; TriangleCornerIndex < 3; ++TriangleCornerIndex)
	{
		RelativePosition = RelativePosition - ReadParticleVector(Contact.BodyB->GetPositions(), Contact.Indices[TriangleCornerIndex]) * Contact.Weights[TriangleCornerIndex];
		EffectiveInverseMass += Contact.BodyB->GetInverseMass()[Contact.Indices[TriangleCornerIndex]] * Contact.Weights[TriangleCornerIndex] * Contact.Weights[TriangleCornerIndex];
	}
	// 현재 상대위치의 법선 성분에서 여유 거리를 뺀 값입니다. 음수일 때만 추가 분리가 필요합니다.
	const double SignedSeparation = RelativePosition.Dot(Contact.Normal) - Contact.Margin;
	if (SignedSeparation >= 0 || !(EffectiveInverseMass > 0))
	{
		return false;
	}
	// 부족한 분리 거리를 유효 역질량으로 나눈 위치 제약 승수입니다. 양쪽 몸체에 반대 보정을 적용합니다.
	const double ConstraintMultiplier = -SignedSeparation / EffectiveInverseMass;
	AddParticleVector(Contact.BodyA->Positions, Contact.ParticleIndexA, Contact.Normal * (SourceInverseMass * ConstraintMultiplier));
	for (int TriangleCornerIndex = 0; TriangleCornerIndex < 3; ++TriangleCornerIndex)
	{
		AddParticleVector(Contact.BodyB->Positions, Contact.Indices[TriangleCornerIndex], Contact.Normal * (-Contact.BodyB->GetInverseMass()[Contact.Indices[TriangleCornerIndex]] * Contact.Weights[TriangleCornerIndex] * ConstraintMultiplier));
	}
	++Stats.Contacts;
	return true;
}

std::optional<FContactBodyInfo> FSoftContactSolver::GetBodyInfo(const FSoftBody* Body) const
{
	const auto Found = BodyCaches.find(Body);
	if (Found == BodyCaches.end())
	{
		return std::nullopt;
	}
	const FBodySpatialCache& BodyCache = Found->second;
	FSurfaceTopology Surface = BodyCache.SurfaceBvh.GetTopology();
	return FContactBodyInfo{std::move(Surface.Indices), std::move(Surface.Vertices), BodyCache.TetrahedronIndex.GetTetrahedronCount(), BodyCache.SurfaceBvh.GetTriangleCount(), BodyCache.TetrahedronIndex.GetCellSize(), BodyCache.TetrahedronIndex.GetActiveCellCount(), BodyCache.TetrahedronIndex.GetOverflowTetrahedronCount()};
}
