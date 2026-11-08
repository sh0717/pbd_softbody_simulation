#include "collision/SoftContactSolver.h"
#include "collision/SurfaceTopology.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <utility>
#include <vector>

FOperationResult FContactOptions::Validate() const
{
	if ((CellSize != 0 && (!std::isfinite(CellSize) || CellSize <= 0)) || MaxCells < 1 || !std::isfinite(Epsilon) || Epsilon <= 0 || !std::isfinite(Margin) || Margin < 0)
	{
		return {EErrorCode::OutOfRange, "Invalid contact options"};
	}

	return {};
}

FOperationResult FSoftContactSolver::Configure(const FContactOptions& InOptions)
{
	const FOperationResult Status = InOptions.Validate();

	if (!Status)
	{
		return Status;
	}

	Options = InOptions;
	BodyCaches.clear();
	Stats = {};

	return {};
}

void FSoftContactSolver::RefitBodySpatialCache(FBodySpatialCache& InOutBodyCache, FSoftBody& Body)
{
	// 사면체 포함 검사와 최근접 외부 면 검색은 같은 위치 상태를 사용해야 합니다.
	InOutBodyCache.TetrahedronQuery.Rebuild(Body);
	InOutBodyCache.SurfaceBvh.Refit(Body.GetPositions());
}

FOperationResult FSoftContactSolver::Prepare(const std::vector<FSoftBody*>& Bodies)
{
	for (const FSoftBody* Body : Bodies)
	{
		if (!Body)
		{
			return {EErrorCode::InvalidArgument, "Null contact body"};
		}

		if (!Body->GetInitializationResult())
		{
			return Body->GetInitializationResult();
		}
	}

	FSoftContactStats PreparedStats{};

	for (FSoftBody* Body : Bodies)
	{
		auto PreparedCache = BodyCaches.find(Body);

		if (PreparedCache == BodyCaches.end())
		{
			// 복구 가능한 토폴로지 오류는 준비 경계에서만 반환합니다.
			FOperationResult TopologyResult;
			FSurfaceTopology Surface = BuildSurfaceTopology(Body->GetTetIndices(), Body->GetSurfaceIndices(), &TopologyResult);

			if (!TopologyResult)
			{
				return TopologyResult;
			}

			// 두 공간 구조를 같은 위치로 완성한 후보만 등록합니다.
			FBodySpatialCache Candidate{FTetrahedronPointQuery(*Body, Options.CellSize, Options.Epsilon, Options.MaxCells), FSurfaceBvh(std::move(Surface), Body->GetRestPositions())};
			RefitBodySpatialCache(Candidate, *Body);
			PreparedCache = BodyCaches.emplace(Body, std::move(Candidate)).first;
		}
		else
		{
			RefitBodySpatialCache(PreparedCache->second, *Body);
		}

		const FBodySpatialCache& BodyCache = PreparedCache->second;
		PreparedStats.OverflowTets += static_cast<double>(BodyCache.TetrahedronQuery.GetOverflowTetrahedronCount());
		PreparedStats.HashCells += static_cast<double>(BodyCache.TetrahedronQuery.GetActiveCellCount());
	}

	// 요청한 모든 몸체의 준비가 끝난 경우에만 일괄 진단 정보를 교체합니다.
	Stats = PreparedStats;

	return {};
}

FIndices FSoftContactSolver::Query(FSoftBody* Body, FVector3D QueryPoint, bool bFirst, FOperationResult* OutResult)
{
	FIndices ContainingTetrahedra;
	const auto Found = BodyCaches.find(Body);

	if (Found == BodyCaches.end())
	{
		PublishOperationResult({EErrorCode::OutOfRange, "Call prepare before querying contacts"}, OutResult);
		return {};
	}

	Found->second.TetrahedronQuery.QueryContainingTetrahedra(QueryPoint, &ContainingTetrahedra, bFirst, Stats.CandidateTests);
	PublishOperationResult({}, OutResult);
	return ContainingTetrahedra;
}

void FSoftContactSolver::CollectDirectionalContacts(FSoftBody* SourceBody, FSoftBody* TargetBody, std::vector<FContact>& InOutContacts)
{
	FBodySpatialCache& TargetEntry = BodyCaches.find(TargetBody)->second;
	const FBodySpatialCache& SourceEntry = BodyCaches.find(SourceBody)->second;

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

		if (TargetEntry.TetrahedronQuery.QueryContainingTetrahedra(SourcePosition, nullptr, true, Stats.CandidateTests) == false)
		{
			continue;
		}

		const std::optional<FSurfaceClosestPoint> SurfaceHit = TargetEntry.SurfaceBvh.FindNearestPoint(SourcePosition, TargetBody->GetPositions(),
			Stats.SurfaceTriangleTests);

		if (!SurfaceHit)
		{
			continue;
		}

		// 대상 표면의 최근접 점과 그 삼각형의 무게중심 좌표입니다. 위치·속도 보간에 같은 좌표를 사용합니다.
		const FTriangleClosestPoint& ClosestPoint = SurfaceHit->ClosestPoint;

		// 포함된 입자에서 외부 표면까지의 거리입니다. 너무 작은 침투는 허용 오차로 걸러냅니다.
		const double PenetrationDepth = std::sqrt(ClosestPoint.DistanceSquared);

		if (!(PenetrationDepth > TargetEntry.TetrahedronQuery.GetCellSize() * Options.Epsilon))
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
	if (!IsPrepared(SourceBody) || !IsPrepared(TargetBody))
	{
		std::fputs("Call prepare before generating contacts\n", stderr);
		std::abort();
	}

	CollectDirectionalContacts(SourceBody, TargetBody, InOutContacts);
	CollectDirectionalContacts(TargetBody, SourceBody, InOutContacts);
}

bool FSoftContactSolver::Project(const FContact& Contact)
{
	// xA는 소스 입자 위치, x_i는 대상 삼각형의 정점 위치이며 i는 0..2입니다.
	// beta_i = Weights[i]는 합이 1인 무게중심 좌표, wA와 w_i는 역질량입니다. 고정 입자의 역질량은 0입니다.
	// 현재 표면 접촉점 q = Σ(beta_i * x_i)를 보간하므로 아래 반복문이 끝나면 RelativePosition = xA - q입니다.
	// 접촉 생성 시 저장한 beta_i와 단위 법선 n = Normal은 유지하고, 위치만 현재 값으로 다시 읽습니다.
	FVector3D RelativePosition = ReadParticleVector(Contact.BodyA->GetPositions(), Contact.ParticleIndexA);
	const double SourceInverseMass = Contact.BodyA->GetInverseMass()[Contact.ParticleIndexA];

	// 제약 C = (xA - q)·n - Margin의 위치 기울기는 grad_xA(C) = n, grad_x_i(C) = -beta_i * n입니다.
	// PBD 분모 D = Σ(역질량 * |제약 기울기|²)이므로 |n| = 1일 때 D = wA + Σ(w_i * beta_i²)입니다.
	// beta_i가 제곱되는 이유는 정점 보정에 beta_i가 곱해지고, 그 보정이 q에 반영될 때 다시 beta_i가 곱해지기 때문입니다.
	double EffectiveInverseMass = SourceInverseMass;

	for (int TriangleCornerIndex = 0; TriangleCornerIndex < 3; ++TriangleCornerIndex)
	{
		RelativePosition = RelativePosition - ReadParticleVector(Contact.BodyB->GetPositions(), Contact.Indices[TriangleCornerIndex]) * Contact.Weights[TriangleCornerIndex];
		EffectiveInverseMass += Contact.BodyB->GetInverseMass()[Contact.Indices[TriangleCornerIndex]] * Contact.Weights[TriangleCornerIndex] * Contact.Weights[TriangleCornerIndex];
	}

	// C >= 0이면 법선 방향으로 Margin 이상의 간격을 확보한 상태이며, C < 0이면 -C만큼 추가 분리가 필요합니다.
	// 접촉 생성 당시 n은 침투 입자에서 표면으로 향하므로 침투 깊이가 h이면 C = -h - Margin입니다.
	// 현재 위치로 C를 재평가하며, D가 양수가 아니면 이 제약을 위치 보정으로 해소할 수 없습니다.
	const double SignedSeparation = RelativePosition.Dot(Contact.Normal) - Contact.Margin;

	if (SignedSeparation >= 0 || !(EffectiveInverseMass > 0))
	{
		return false;
	}

	// lambda = -C / D > 0으로 두고, 각 입자에 Δx = 역질량 * lambda * 제약 기울기를 적용합니다.
	// n과 beta_i가 고정된 이 제약에서는 ΔC = lambda * D = -C이므로 수치 오차를 제외하면 보정 직후 C = 0입니다.
	const double ConstraintMultiplier = -SignedSeparation / EffectiveInverseMass;

	// ΔxA = wA * lambda * n: 소스 입자를 법선 방향으로 이동합니다.
	AddParticleVector(Contact.BodyA->Positions, Contact.ParticleIndexA, Contact.Normal * (SourceInverseMass * ConstraintMultiplier));

	for (int TriangleCornerIndex = 0; TriangleCornerIndex < 3; ++TriangleCornerIndex)
	{
		// Δx_i = -w_i * beta_i * lambda * n: 대상 정점은 반대 방향으로 이동하며, 역질량이 0이면 움직이지 않습니다.
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
	return FContactBodyInfo{std::move(Surface.Indices), std::move(Surface.Vertices), BodyCache.TetrahedronQuery.GetTetrahedronCount(), BodyCache.SurfaceBvh.GetTriangleCount(), BodyCache.TetrahedronQuery.GetCellSize(), BodyCache.TetrahedronQuery.GetActiveCellCount(), BodyCache.TetrahedronQuery.GetOverflowTetrahedronCount()};
}
