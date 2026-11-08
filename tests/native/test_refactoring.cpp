#include "../../native/bindings/ContactSession.h"
#include "../../native/bindings/OwnerRegistry.h"
#include "collision/SurfaceBvh.h"
#include "collision/SurfaceTopology.h"
#include "collision/TetrahedronPointQuery.h"
#include "collision/TriangleGeometry.h"
#include "simulation/PhysicsWorld.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

	template <typename T, typename = void>
	struct THasResize : std::false_type
	{
	};

	template <typename T>
	struct THasResize<T, std::void_t<decltype(std::declval<T&>().resize(std::size_t{}))>> : std::true_type
	{
	};

	template <typename T, typename = void>
	struct THasReserve : std::false_type
	{
	};

	template <typename T>
	struct THasReserve<T, std::void_t<decltype(std::declval<T&>().reserve(std::size_t{}))>> : std::true_type
	{
	};

	template <typename T, typename = void>
	struct THasClear : std::false_type
	{
	};

	template <typename T>
	struct THasClear<T, std::void_t<decltype(std::declval<T&>().clear())>> : std::true_type
	{
	};

	template <typename Value, typename... Results>
	constexpr bool bReadonlyVectorResults = (std::is_same<Results, const std::vector<Value>&>::value && ...);
	static_assert(bReadonlyVectorResults<double, decltype(std::declval<FBody&>().GetPositions()), decltype(std::declval<const FBody&>().GetPositions()), decltype(std::declval<FBody&>().GetRestPositions()), decltype(std::declval<const FBody&>().GetRestPositions()), decltype(std::declval<FBody&>().GetPreviousPositions()), decltype(std::declval<const FBody&>().GetPreviousPositions()), decltype(std::declval<FBody&>().GetVelocities()), decltype(std::declval<const FBody&>().GetVelocities()), decltype(std::declval<FBody&>().GetPreSolveVelocities()), decltype(std::declval<const FBody&>().GetPreSolveVelocities()), decltype(std::declval<FBody&>().GetInverseMass()), decltype(std::declval<const FBody&>().GetInverseMass())>);
	static_assert(bReadonlyVectorResults<double, decltype(std::declval<FSoftBody&>().GetEdgeLambdas()), decltype(std::declval<const FSoftBody&>().GetEdgeLambdas()), decltype(std::declval<FSoftBody&>().GetVolumeLambdas()), decltype(std::declval<const FSoftBody&>().GetVolumeLambdas()), decltype(std::declval<FSoftBody&>().GetRestLengths()), decltype(std::declval<const FSoftBody&>().GetRestLengths()), decltype(std::declval<FSoftBody&>().GetRestVolumes()), decltype(std::declval<const FSoftBody&>().GetRestVolumes()), decltype(std::declval<FSoftBody&>().GetTetLimits()), decltype(std::declval<const FSoftBody&>().GetTetLimits())>);
	static_assert(bReadonlyVectorResults<std::uint32_t, decltype(std::declval<FSoftBody&>().GetEdgeIndices()), decltype(std::declval<const FSoftBody&>().GetEdgeIndices()), decltype(std::declval<FSoftBody&>().GetTetIndices()), decltype(std::declval<const FSoftBody&>().GetTetIndices()), decltype(std::declval<FSoftBody&>().GetSurfaceIndices()), decltype(std::declval<const FSoftBody&>().GetSurfaceIndices())>);
	using ReadonlyValues = decltype(std::declval<FBody&>().GetPositions());
	static_assert(!THasResize<ReadonlyValues>::value && !THasReserve<ReadonlyValues>::value && !THasClear<ReadonlyValues>::value);
	static_assert(!std::is_assignable<decltype(std::declval<ReadonlyValues>()[0]), double>::value);
	static_assert(std::is_same_v<decltype(std::declval<FPhysicsWorld&>().GetSettings()), const FSettings&>);
	static_assert(std::is_same_v<decltype(std::declval<const FPhysicsWorld&>().GetSettings()), const FSettings&>);

	void Require(bool bCondition, const char* Reason)
	{
		if (!bCondition)
		{
			std::cerr << "FAIL: " << Reason << '\n';
			std::exit(EXIT_FAILURE);
		}
	}

	template <EErrorCode Expected, typename Function>
	void Rejects(Function Action, const char* Reason)
	{
		const FOperationResult Status = Action();
		Require(Status.Code == Expected && !Status.Message.empty(), Reason);
	}

	std::vector<double> Tetra()
	{
		return {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
	}

	void TetrahedronQueryPreservesCandidateOrderAndRebuild()
	{
		std::vector<double> Positions = Tetra();
		const std::vector<double> OtherTetrahedron = Tetra();
		Positions.insert(Positions.end(), OtherTetrahedron.begin(), OtherTetrahedron.end());

		const FIndices Tetrahedra{0, 1, 2, 3, 4, 5, 6, 7};
		FSoftBody Body(Positions, {}, Tetrahedra, {});
		FTetrahedronPointQuery Dense(Body, 1, 1E-10, 512);
		Dense.Rebuild(Body);

		double CandidateTests = 5;
		FIndices Found{99};
		Require(Dense.QueryContainingTetrahedra({0.1, 0.2, 0.3}, &Found, false, CandidateTests) && Found == FIndices{99, 1, 0} && CandidateTests == 7, "dense query changed candidate order, append semantics, or diagnostic accumulation");
		Found.clear();
		CandidateTests = 0;
		Require(Dense.QueryContainingTetrahedra({0.1, 0.2, 0.3}, &Found, true, CandidateTests) && Found == FIndices{1} && CandidateTests == 1, "first dense match was replaced with the lowest tetrahedron index");
		CandidateTests = 0;
		Require(!Dense.QueryContainingTetrahedra({0.9, 0.9, 0.9}, nullptr, true, CandidateTests) && CandidateTests == 2, "barycentric rejection did not count both bounded candidates");

		FTetrahedronPointQuery Overflow(Body, 1, 1E-10, 1);
		Overflow.Rebuild(Body);
		Found.clear();
		CandidateTests = 0;
		Require(Overflow.GetActiveCellCount() == 0 && Overflow.GetOverflowTetrahedronCount() == 2 && Overflow.QueryContainingTetrahedra({0.1, 0.2, 0.3}, &Found, false, CandidateTests) && Found == FIndices{0, 1} && CandidateTests == 2, "overflow candidates lost original tetrahedron order or containment");

		// 사용하지 않는 먼 정점으로 전체 경계만 넓혀 희소 셀 경로를 선택합니다.
		Positions.insert(Positions.end(), {1E6, 1E6, 1E6});

		FSoftBody SparseBody(Positions, {}, Tetrahedra, {});
		FTetrahedronPointQuery Sparse(SparseBody, 1, 1E-10, 512);
		Sparse.Rebuild(SparseBody);
		Found.clear();
		CandidateTests = 0;
		Require(Sparse.GetActiveCellCount() > 0 && Sparse.GetOverflowTetrahedronCount() == 0 && Sparse.QueryContainingTetrahedra({0.1, 0.2, 0.3}, &Found, false, CandidateTests) && Found == FIndices{0, 1} && CandidateTests == 2, "sparse candidates lost their insertion order or containment");

		for (std::size_t VertexIndex = 0; VertexIndex < Body.GetVertexCount(); ++VertexIndex)
		{
			Body.SetParticlePosition(VertexIndex, ReadParticleVector(Body.GetPositions(), static_cast<std::uint32_t>(VertexIndex)) + FVector3D{10, 0, 0});
		}

		Dense.Rebuild(Body);
		Found.clear();
		CandidateTests = 0;
		Require(Body.GetBoundsMin().X == 10 && Dense.QueryContainingTetrahedra({10.1, 0.2, 0.3}, &Found, false, CandidateTests) && Found == FIndices{1, 0}, "rebuild did not update body bounds and translated containment");
		CandidateTests = 0;
		Require(!Dense.QueryContainingTetrahedra({0.1, 0.2, 0.3}, nullptr, true, CandidateTests) && CandidateTests == 0, "rebuild retained stale cell candidates at the previous position");
	}

	void TetrahedronQueryRebuildSwitchesStorageAndRecoversGeometry()
	{
		std::vector<double> Positions = Tetra();
		const std::vector<double> Duplicate = Tetra();
		Positions.insert(Positions.end(), Duplicate.begin(), Duplicate.end());
		Positions.insert(Positions.end(), {2, 2, 2});

		FSoftBody Body(Positions, {}, {0, 1, 2, 3, 4, 5, 6, 7}, {});
		FTetrahedronPointQuery Query(Body, 1, 1E-10, 512);

		for (int Cycle = 0; Cycle < 3; ++Cycle)
		{
			for (const bool bSparse : {false, true, false})
			{
				// 사면체에 속하지 않는 정점으로 저장 방식만 전환합니다.
				Body.SetParticlePosition(8, bSparse ? FVector3D{1E6, 1E6, 1E6} : FVector3D{2, 2, 2});
				Query.Rebuild(Body);

				FIndices Found;
				double CandidateTests = 0;
				const FIndices Expected = bSparse ? FIndices{0, 1} : FIndices{1, 0};
				Require(Query.QueryContainingTetrahedra({0.1, 0.2, 0.3}, &Found, false, CandidateTests) && Found == Expected && CandidateTests == 2, "storage switch retained stale candidates or changed their order");
			}

			Body.SetParticlePosition(7, {0, 0, 0});
			Query.Rebuild(Body);

			FIndices Found;
			double CandidateTests = 0;
			Require(Query.QueryContainingTetrahedra({0.1, 0.2, 0.3}, &Found, false, CandidateTests) && Found == FIndices{0} && CandidateTests == 1, "collapsed tetrahedron retained valid geometry");
			Body.SetParticlePosition(7, {0, 0, -1});
			Query.Rebuild(Body);
			Found.clear();
			CandidateTests = 0;
			Require(Query.QueryContainingTetrahedra({0.1, 0.2, -0.3}, &Found, false, CandidateTests) && Found == FIndices{1} && CandidateTests == 1, "inverted tetrahedron did not recover after collapse");
			Body.SetParticlePosition(7, {0, 0, 1});
		}
	}

	void SurfaceBvhPreservesTieOrderAndIndependentValues()
	{
		// 같은 위치의 서로 다른 삼각형 아홉 개로 내부 노드와 동률 최근접 후보를 함께 만듭니다.
		std::vector<double> Positions;
		FSurfaceTopology Surface;

		for (std::uint32_t TriangleIndex = 0; TriangleIndex < 9; ++TriangleIndex)
		{
			Positions.insert(Positions.end(), {0, 0, 0, 1, 0, 0, 0, 1, 0});

			for (std::uint32_t CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
			{
				Surface.Indices.push_back(TriangleIndex * 3 + CornerIndex);
				Surface.Vertices.push_back(TriangleIndex * 3 + CornerIndex);
			}
		}

		FSurfaceBvh Bvh(Surface, Positions);
		Bvh.Refit(Positions);
		Surface.Indices[0] = 999;

		double TriangleTests = 3;
		const std::optional<FSurfaceClosestPoint> Hit = Bvh.FindNearestPoint({0.25, 0.25, 1}, Positions, TriangleTests);
		Require(Hit.has_value() && Hit->VertexIndices == std::array<std::uint32_t, 3>{12, 13, 14} && Hit->ClosestPoint.Position == FVector3D{0.25, 0.25, 0} && Hit->ClosestPoint.Weights == std::array<double, 3>{0.5, 0.25, 0.25} && Hit->ClosestPoint.DistanceSquared == 1 && TriangleTests == 12, "BVH changed stable construction, right-first tie traversal, or strict nearest selection");

		FSurfaceTopology Snapshot = Bvh.GetTopology();
		Snapshot.Indices[0] = 888;
		Require(Bvh.GetTopology().Indices[0] == 0 && Bvh.GetSurfaceVertexCount() == 27 && Bvh.GetSurfaceVertexIndex(12) == 12, "BVH topology aliases constructor input or diagnostic snapshots");

		for (std::size_t Offset = 2; Offset < Positions.size(); Offset += 3)
		{
			Positions[Offset] = 2;
		}

		Bvh.Refit(Positions);
		TriangleTests = 0;

		const std::optional<FSurfaceClosestPoint> MovedHit = Bvh.FindNearestPoint({0.25, 0.25, 3}, Positions, TriangleTests);
		Require(MovedHit.has_value() && MovedHit->VertexIndices == Hit->VertexIndices && MovedHit->ClosestPoint.Position == FVector3D{0.25, 0.25, 2} && Hit->ClosestPoint.Position == FVector3D{0.25, 0.25, 0} && TriangleTests == 9, "BVH refit, reusable traversal, or owned nearest-point result is stale");

		FSurfaceBvh Empty({}, {});
		Empty.Refit({});
		TriangleTests = 7;
		Require(Empty.IsEmpty() && !Empty.FindNearestPoint({}, {}, TriangleTests).has_value() && TriangleTests == 7, "empty surface manufactured a nearest point or triangle tests");
	}

	void SurfaceBvhPrunesAndRefitsReorderedGeometry()
	{
		// 서로 떨어진 YZ 평면 삼각형 32개입니다. 최근접점은 X만 평면에 맞추면 알 수 있습니다.
		std::vector<double> Positions;
		FSurfaceTopology Surface;

		for (std::uint32_t TriangleIndex = 0; TriangleIndex < 32; ++TriangleIndex)
		{
			const double PlaneX = TriangleIndex * 4.0;
			Positions.insert(Positions.end(), {PlaneX, 0, 0, PlaneX, 2, 0, PlaneX, 0, 2});

			for (std::uint32_t CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
			{
				Surface.Indices.push_back(TriangleIndex * 3 + CornerIndex);
				Surface.Vertices.push_back(TriangleIndex * 3 + CornerIndex);
			}
		}

		const std::vector<double> RestPositions = Positions;
		FSurfaceBvh Bvh(Surface, RestPositions);

		for (int Phase = 0; Phase < 3; ++Phase)
		{
			Positions = RestPositions;

			if (Phase == 1)
			{
				// 잎 사이의 공간 순서를 섞고 전체를 이동합니다. 최초 분할을 유지한 Refit을 검사합니다.
				for (std::uint32_t TriangleIndex = 0; TriangleIndex < 32; ++TriangleIndex)
				{
					const double PlaneX = -200.0 + ((TriangleIndex * 13) % 32) * 4.0;

					for (std::size_t CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
					{
						Positions[TriangleIndex * 9 + CornerIndex * 3] = PlaneX;
					}
				}
			}

			Bvh.Refit(Positions);

			// 양쪽 가지와 각 잎의 경계를 번갈아 조회해 재사용 스택과 가지치기를 함께 확인합니다.
			for (std::uint32_t TriangleIndex : {0U, 31U, 7U, 24U, 8U, 23U, 15U, 16U})
			{
				const double PlaneX = Positions[TriangleIndex * 9];
				double TriangleTests = 5;
				const std::optional<FSurfaceClosestPoint> Hit = Bvh.FindNearestPoint({PlaneX + 0.25, 0.5, 0.5}, Positions, TriangleTests);
				Require(Hit.has_value() && Hit->VertexIndices == std::array<std::uint32_t, 3>{TriangleIndex * 3, TriangleIndex * 3 + 1, TriangleIndex * 3 + 2} && Hit->ClosestPoint.Position == FVector3D{PlaneX, 0.5, 0.5} && Hit->ClosestPoint.Weights == std::array<double, 3>{0.5, 0.25, 0.25} && Hit->ClosestPoint.DistanceSquared == 0.0625, "BVH pruning or refit lost the analytic nearest triangle after spatial reordering");
				Require(TriangleTests > 5 && TriangleTests <= 37, "BVH triangle-test counter did not accumulate actual evaluations");

				if (Phase != 1)
				{
					Require(TriangleTests == 13, "BVH did not prune the three separated leaves or restore pruning after refit");
				}
			}

			Require(Bvh.GetTopology().Indices == Surface.Indices && Bvh.GetTopology().Vertices == Surface.Vertices && Bvh.GetTriangleCount() == 32, "BVH refit modified original topology or surface order");
		}
	}

	void SurfaceBvhRefitsCollapsedLeaf()
	{
		FSurfaceTopology Surface{{0, 1, 2}, {0, 1, 2}};
		std::vector<double> Positions{0, 0, 0, 2, 0, 0, 0, 2, 0};
		FSurfaceBvh Bvh(Surface, Positions);
		Positions = {3, 4, 5, 3, 4, 5, 3, 4, 5};
		Bvh.Refit(Positions);

		double TriangleTests = 0;
		const std::optional<FSurfaceClosestPoint> CollapsedHit = Bvh.FindNearestPoint({3, 4, 7}, Positions, TriangleTests);
		Require(CollapsedHit.has_value() && CollapsedHit->ClosestPoint.Position == FVector3D{3, 4, 5} && CollapsedHit->ClosestPoint.DistanceSquared == 4 && TriangleTests == 1, "zero-extent BVH leaf discarded its collapsed triangle");
		Positions = {0, 0, 0, 2, 0, 0, 0, 2, 0};
		Bvh.Refit(Positions);

		const std::optional<FSurfaceClosestPoint> RestoredHit = Bvh.FindNearestPoint({0.5, 0.5, 1}, Positions, TriangleTests);
		Require(RestoredHit.has_value() && RestoredHit->ClosestPoint.Position == FVector3D{0.5, 0.5, 0} && RestoredHit->ClosestPoint.DistanceSquared == 1 && TriangleTests == 2, "BVH leaf bounds did not recover after triangle collapse");
	}

	void Setup(FPhysicsWorld& World)
	{
		FSettings Settings = World.GetSettings();
		Settings.Bounds = 1000;
		Settings.Gravity = {};
		Settings.Damping = 0;
		Settings.Substeps = 1;
		Settings.Iterations = 1;
		Settings.EdgeCompliance = 0.02;
		Settings.VolumeCompliance = 0.02;
		Settings.Restitution = 0;
		World.Configure(Settings);
	}

	std::array<double, 13> SettingsValues(const FSettings& Settings)
	{
		return {Settings.Dt, Settings.EdgeCompliance, Settings.VolumeCompliance, Settings.PbdStiffness, Settings.Bounds, Settings.Restitution, Settings.Damping, static_cast<double>(Settings.Substeps), static_cast<double>(Settings.Iterations), static_cast<double>(Settings.bXpbd), Settings.Gravity.X, Settings.Gravity.Y, Settings.Gravity.Z};
	}

	using FBufferReferences = std::array<std::reference_wrapper<const std::vector<double>>, 7>;

	FBufferReferences GetBuffers(const FSoftBody& Body)
	{
		return {std::cref(Body.GetPositions()), std::cref(Body.GetPreviousPositions()), std::cref(Body.GetVelocities()), std::cref(Body.GetPreSolveVelocities()), std::cref(Body.GetInverseMass()), std::cref(Body.GetEdgeLambdas()), std::cref(Body.GetVolumeLambdas())};
	}

	using FStorageIdentity = std::array<std::pair<const double*, std::size_t>, 7>;

	FStorageIdentity GetStorageIdentity(const FSoftBody& Body)
	{
		const FBufferReferences References = GetBuffers(Body);
		FStorageIdentity Identity{};
		std::transform(References.begin(), References.end(), Identity.begin(), [](const auto& Reference)
		{
			return std::make_pair(Reference.get().data(), Reference.get().size());
		});

		return Identity;
	}

	std::vector<std::vector<double>> State(const FBody& Body)
	{
		std::vector<std::vector<double>> Result{Body.GetPositions(), Body.GetPreviousPositions(), Body.GetVelocities(), Body.GetPreSolveVelocities(), Body.GetInverseMass()};

		if (Body.IsSoft())
		{
			const FSoftBody& SoftBody = static_cast<const FSoftBody&>(Body);
			Result.push_back(SoftBody.GetEdgeLambdas());
			Result.push_back(SoftBody.GetVolumeLambdas());
		}

		return Result;
	}

	std::array<double, 8> StatsValues(const FSoftContactStats& Stats)
	{
		return {Stats.BroadPhasePairs, Stats.CandidateTests, Stats.Contacts, Stats.GeneratedContacts, Stats.MaxPenetration, Stats.OverflowTets, Stats.HashCells, Stats.SurfaceTriangleTests};
	}

	bool SameInfo(const FContactBodyInfo& A, const FContactBodyInfo& B)
	{
		return A.SurfaceIndices == B.SurfaceIndices && A.SurfaceVertices == B.SurfaceVertices && A.TetCount == B.TetCount && A.SurfaceTriangleCount == B.SurfaceTriangleCount && A.CellSize == B.CellSize && A.HashCellCount == B.HashCellCount && A.OverflowTets == B.OverflowTets;
	}

	void EmptyVectorsAndReferenceSnapshots()
	{
		FSoftBody Body({0, 0, 0}, {}, {}, {});
		Require(Body.GetEdgeLambdas().empty() && Body.GetVolumeLambdas().empty(), "empty lambdas are not empty");
		Require(Body.GetEdgeLambdas().begin() == Body.GetEdgeLambdas().end(), "empty lambda iterators differ");

		const std::vector<double>& Values = Body.GetPositions();
		const std::reference_wrapper<const std::vector<double>> Observer = std::cref(Values);
		const std::vector<double> Snapshot = Values;
		Body.SetParticlePosition(0, {7, 0, 0});
		Require(Values[0] == 7 && Observer.get()[0] == 7 && Body.GetPositions()[0] == 7, "const vector reference stopped observing setter edits");
		Require(Snapshot[0] == 0, "owning vector snapshot changed after setter edit");
	}

	void StableStorage()
	{
		FPhysicsWorld World;
		Setup(World);

		FSoftBody* Body = World.AddSoft(Tetra(), {0, 1}, {0, 1, 2, 3}, {});
		const FBufferReferences Original = GetBuffers(*Body);
		const FStorageIdentity OriginalStorage = GetStorageIdentity(*Body);
		const auto Verify = [&]
		{
			const FBufferReferences Current = GetBuffers(*Body);

			for (std::size_t I = 0; I < Original.size(); ++I)
			{
				Require(&Original[I].get() == &Current[I].get(), "borrowed vector reference changed");
			}

			Require(GetStorageIdentity(*Body) == OriginalStorage, "borrowed storage address or length changed");
		};
		Require(Original[0].get().size() == 12 && Original[4].get().size() == 4 && Original[5].get().size() == 1 && Original[6].get().size() == 1, "fixture does not exercise all seven nonempty buffers");
		World.Step(0.1);
		Verify();
		Body->MoveTo({5, 6, 7});
		Verify();
		Body->BeginSubstep();
		Verify();
		World.Reset();
		Verify();

		for (int I = 0; I < 128; ++I)
		{
			World.AddBall(0.1, {20.0 + I, 0, 0});
		}

		Require(World.GetBodies().front().get() == Body, "body address changed when world grew");
		Verify();
		Body->SetParticlePosition(0, {3, 0, 0});
		Require(Original[0].get()[0] == 3, "old vector reference stopped observing original body");
	}

	void ZeroStepPreservesProgressedState()
	{
		FPhysicsWorld World;
		Setup(World);

		FSoftBody* Body = World.AddSoft(Tetra(), {0, 1}, {0, 1, 2, 3}, {}, {10, 0, 0});
		Body->SetParticlePosition(1, {11.2, 0, 0});
		World.AddSoft({10.1, 0.2, 0.3}, {}, {}, {});
		World.AddBall(1, {2, 0, 0});

		FObstacle Obstacle;
		Obstacle.Id = 1;
		Obstacle.Size = {2, 2, 2};
		Obstacle.bBumper = true;
		Obstacle.Strength = 4;
		World.SetObstacles({Obstacle});
		Require(World.Step(0.1), "fixture failed to advance");
		Require(World.GetStats().Contacts > 0 && !World.GetHits().empty() && World.GetSimulationTime() > 0, "fixture must contain diagnostics and bumper history");
		Require(Body->GetEdgeLambdas()[0] != 0 && Body->GetVolumeLambdas()[0] != 0, "fixture must contain accumulated edge and volume lambdas");

		std::vector<std::vector<std::vector<double>>> Before;

		for (const std::unique_ptr<FBody>& Item : World.GetBodies())
		{
			Before.push_back(State(*Item));
		}

		const std::array<double, 8> OldStats = StatsValues(World.GetStats());
		const std::vector<FHit> OldHits = World.GetHits();
		const double OldTime = World.GetSimulationTime();
		const auto Verify = [&]
		{
			for (std::size_t I = 0; I < Before.size(); ++I)
			{
				Require(Before[I] == State(*World.GetBodies()[I]), "no-op or rejected step changed body state");
			}

			Require(OldStats == StatsValues(World.GetStats()), "no-op or rejected step cleared diagnostics");
			Require(OldTime == World.GetSimulationTime(), "no-op or rejected step changed time");
			Require(OldHits.size() == World.GetHits().size(), "no-op or rejected step cleared hits");

			for (std::size_t I = 0; I < OldHits.size(); ++I)
			{
				const FHit& A = OldHits[I];
				const FHit& B = World.GetHits()[I];
				Require(A.Body == B.Body && A.Obstacle == B.Obstacle && A.Time == B.Time && A.Strength == B.Strength && A.Vertices == B.Vertices, "no-op or rejected step changed hit contents");
			}
		};
		Require(!World.Step(0), "zero step advanced");
		Verify();

		const std::array<double, 13> OriginalSettings = SettingsValues(World.GetSettings());
		std::vector<FSettings> InvalidSettings(7, World.GetSettings());
		InvalidSettings[0].Substeps = 0;
		InvalidSettings[1].Iterations = 0;
		InvalidSettings[2].Dt = -1;
		InvalidSettings[3].Dt = std::numeric_limits<double>::quiet_NaN();
		InvalidSettings[4].Bounds = 0;
		InvalidSettings[5].Gravity.Y = std::numeric_limits<double>::infinity();
		InvalidSettings[6].EdgeCompliance = -1;

		for (const FSettings& Candidate : InvalidSettings)
		{
			Rejects<EErrorCode::OutOfRange>([&]
			{
				return World.Configure(Candidate);
			}, "configure accepted an invalid candidate");
			Require(SettingsValues(World.GetSettings()) == OriginalSettings, "rejected configure partially changed settings");
			Require(!World.Step(0), "rejected configure corrupted the zero-step contract");
			Verify();
		}

		Rejects<EErrorCode::OutOfRange>([&]
		{
			FOperationResult Status;
			World.Step(std::numeric_limits<double>::denorm_min(), &Status);

			return Status;
		}, "unrepresentable substep scale accepted");
		Verify();

		FSettings ChangedSettings = World.GetSettings();
		ChangedSettings.Dt = 0;
		ChangedSettings.Gravity = {1, -2, 3};
		ChangedSettings.bXpbd = false;
		World.Configure(ChangedSettings);
		Require(SettingsValues(World.GetSettings()) == SettingsValues(ChangedSettings), "valid configure did not publish the complete candidate");
		Verify();
	}

	void OwnerRegistryRejectsStaleAndWrongTypeTokens()
	{
		TOwnerRegistry<int, double> Owners;
		const std::uint32_t First = Owners.RegisterOwner(std::make_unique<int>(7));
		const std::uint32_t OtherType = Owners.RegisterOwner(std::make_unique<double>(2.5));
		Require(First != OtherType && First != 0 && OtherType != 0, "owner kinds must share one nonzero token sequence");
		Rejects<EErrorCode::InvalidArgument>([&]
		{
			FOperationResult Status;
			Owners.Get<double>(First, &Status);

			return Status;
		}, "wrong owner type was accepted");
		Owners.Destroy<double>(First);
		Require(*Owners.Get<int>(First) == 7, "wrong-type destruction deleted the owner");
		Owners.Destroy<int>(First);

		const std::uint32_t Next = Owners.RegisterOwner(std::make_unique<int>(9));
		Require(Next != First, "owner recreation reused a stale token");
		Rejects<EErrorCode::InvalidArgument>([&]
		{
			FOperationResult Status;
			Owners.Get<int>(First, &Status);

			return Status;
		}, "stale owner token selected a replacement");
		Owners.Destroy<int>(First);
		Require(*Owners.Get<int>(Next) == 9 && *Owners.Get<double>(OtherType) == 2.5, "stale destruction affected a live owner");
	}

	void OwnerRegistryExhaustionPreservesOwnersAndLifetime()
	{
		struct FTrackedOwner
		{
			int& LiveCount;

			explicit FTrackedOwner(int& InOutLiveCount)
				: LiveCount(InOutLiveCount)
			{
				++LiveCount;
			}

			~FTrackedOwner()
			{
				--LiveCount;
			}
		};

		int LiveCount = 0;
		{
			// 동일 발급 경로의 마지막 유효 ID와 소진을 작은 상한으로 재현합니다.
			TOwnerRegistry<FTrackedOwner> Owners(2);
			Rejects<EErrorCode::InvalidArgument>([&]
			{
				FOperationResult Status;
				Owners.RegisterOwner(std::unique_ptr<FTrackedOwner>{}, &Status);

				return Status;
			}, "null owner was accepted");

			const std::uint32_t First = Owners.RegisterOwner(std::make_unique<FTrackedOwner>(LiveCount));
			const std::uint32_t Last = Owners.RegisterOwner(std::make_unique<FTrackedOwner>(LiveCount));
			Require(First == 1 && Last == 2 && LiveCount == 2, "last allowed token was not issued exactly once");
			Rejects<EErrorCode::ResourceExhausted>([&]
			{
				FOperationResult Status;
				Owners.RegisterOwner(std::make_unique<FTrackedOwner>(LiveCount), &Status);

				return Status;
			}, "owner token exhaustion wrapped or accepted another owner");
			Require(LiveCount == 2 && Owners.Get<FTrackedOwner>(Last), "failed registration leaked an owner or discarded a live one");
			Owners.Destroy<FTrackedOwner>(First);
			Rejects<EErrorCode::ResourceExhausted>([&]
			{
				FOperationResult Status;
				Owners.RegisterOwner(std::make_unique<FTrackedOwner>(LiveCount), &Status);

				return Status;
			}, "owner deletion allowed an expired token to be reused");
			Require(LiveCount == 1, "registration failure or destruction leaked an owner");
		}

		Require(LiveCount == 0, "registry destruction did not release its remaining owners");
	}

	void BumperScratchPreservesContactOrderAndReuse()
	{
		FSoftBody Body({0, 0, 0, 1, 0, 0, 2, 0, 0}, {}, {}, {}, {}, {1, 1, 0});
		FObstacle First;
		First.Id = 1;
		First.bBumper = true;
		First.Strength = 2;

		FObstacle Second = First;
		Second.Id = 2;
		Second.Strength = 4;

		const std::vector<FObstacle> Obstacles{First, Second};
		const auto Contact = [&](std::uint32_t Vertex, std::uint32_t Obstacle, FVector3D Normal, double Incoming = 0)
		{
			FContact Result;
			Result.BodyA = &Body;
			Result.ParticleIndexA = Vertex;
			Result.Obstacle = Obstacle;
			Result.Normal = Normal;
			Result.IncomingNormalVelocity = Incoming;

			return Result;
		};

		const std::vector<FContact> Contacts{Contact(0, 2, {1, 0, 0}, -2), Contact(0, 1, {-1, 0, 0}), Contact(0, 2, {0, 1, 0}, -100), Contact(1, 2, {0, 1, 0}), Contact(2, 2, {0, 0, 1})};
		FBumperSystem Bumpers;
		const auto VerifyFire = [&]
		{
			const std::vector<FHit>& Hits = Bumpers.GetHits();
			Require(Hits.size() == 2 && Hits[0].Obstacle == 2 && Hits[0].Vertices == 2 && Hits[1].Obstacle == 1 && Hits[1].Vertices == 1, "bumper groups lost first-appearance order or unique vertex counts");
			Require(ReadParticleVector(Body.GetVelocities(), 0) == FVector3D{-2, 0, 0} && ReadParticleVector(Body.GetVelocities(), 1) == FVector3D{0, 4, 0} && ReadParticleVector(Body.GetVelocities(), 2) == FVector3D{}, "bumper response lost first contact normal, group order, or pinned filtering");
		};
		Bumpers.Apply(Contacts, Obstacles, 0, 0.01, 0.5);
		VerifyFire();
		Bumpers.BeginFrame();
		Bumpers.Apply(Contacts, Obstacles, 0.01, 0.01, 0.5);
		Require(Bumpers.GetHits().empty(), "persistent contact fired again");
		Bumpers.Reset();
		Body.Reset();
		Bumpers.Apply(Contacts, Obstacles, 0, 0.01, 0.5);
		VerifyFire();
		Bumpers.BeginFrame();
		Bumpers.Apply({}, {}, 0.3, 0.01, 0.5);
		Require(Bumpers.GetHits().empty(), "empty contact batch replayed stale groups");

		const std::vector<double> OldVelocity = Body.GetVelocities();
		FBall Replacement(1, {});
		FContact ReplacementContact = Contact(0, 1, {0, 0, 1});
		ReplacementContact.BodyA = &Replacement;
		Bumpers.Apply({ReplacementContact}, {First}, 0.4, 0.01, 0.5);
		Require(Bumpers.GetHits().size() == 1 && Bumpers.GetHits()[0].Body == &Replacement && Bumpers.GetHits()[0].Vertices == 1 && ReadParticleVector(Replacement.GetVelocities(), 0) == FVector3D{0, 0, 2}, "smaller replacement body reused stale groups or vertex marks");
		Require(Body.GetVelocities() == OldVelocity, "scratch reuse modified a previous body");
	}

	void BumperObstacleIndexPreservesFirstMatchAndRebuilds()
	{
		FBall Body(1, {});
		FContact Contact;
		Contact.BodyA = &Body;
		Contact.Obstacle = 40;
		Contact.Normal = {1, 0, 0};

		FObstacle First;
		First.Id = 40;
		First.bBumper = true;
		First.Strength = 2;

		FObstacle Duplicate = First;
		Duplicate.Strength = 9;

		FObstacle Lower = First;
		Lower.Id = 3;

		FObstacle Higher = First;
		Higher.Id = 200;

		std::vector<FObstacle> Obstacles{Higher, First, Lower, Duplicate};
		FBumperSystem Bumpers;
		Bumpers.Apply({Contact}, Obstacles, 0, 0.01, 0);
		Require(Bumpers.GetHits().size() == 1 && Bumpers.GetHits()[0].Strength == 2 && ReadParticleVector(Body.GetVelocities(), 0) == FVector3D{2, 0, 0}, "indexed lookup must select the first duplicate ID in input order");

		// 벡터의 순서를 바꾸거나 저장소를 교체해도 이전 숫자 인덱스를 재사용하면 안 됩니다.
		Obstacles = {Duplicate, Higher, Lower, First};
		Bumpers.BeginFrame();
		Body.Reset();
		Bumpers.Apply({Contact}, Obstacles, 0.3, 0.01, 0);
		Require(Bumpers.GetHits().size() == 1 && Bumpers.GetHits()[0].Strength == 9 && ReadParticleVector(Body.GetVelocities(), 0) == FVector3D{9, 0, 0}, "obstacle index was not rebuilt after replacement and reordering");

		Bumpers.BeginFrame();
		Body.Reset();
		Bumpers.Apply({Contact}, {Lower, Higher}, 0.31, 0.01, 0);
		Require(Bumpers.GetHits().empty() && ReadParticleVector(Body.GetVelocities(), 0) == FVector3D{}, "missing ID matched a neighboring indexed obstacle");

		// 0.2초 안에 같은 ID가 복원되어도 제거 시점에 쿨다운 상태를 정리해야 합니다.
		Bumpers.Apply({Contact}, {First}, 0.32, 0.01, 0);
		Require(Bumpers.GetHits().size() == 1 && Bumpers.GetHits()[0].Strength == 2, "removed obstacle retained stale cooldown state");

		Bumpers.BeginFrame();
		Body.Reset();
		Bumpers.Apply({}, {Lower, Higher}, 0.33, 0.01, 0);
		Bumpers.Apply({Contact}, {First}, 0.34, 0.01, 0);
		Require(Bumpers.GetHits().size() == 1 && Bumpers.GetHits()[0].Strength == 2, "empty contact batch skipped pruning its existing cooldown states");

		Bumpers.Reset();
		Body.Reset();
		First.bBumper = false;
		Bumpers.Apply({Contact}, {First, Duplicate}, 0, 0.01, 0);
		Require(Bumpers.GetHits().empty() && ReadParticleVector(Body.GetVelocities(), 0) == FVector3D{}, "disabled first duplicate incorrectly fell through to an active duplicate");
		Bumpers.Apply({Contact}, {}, 0.3, 0.01, 0);
		Require(Bumpers.GetHits().empty(), "empty obstacle index replayed an earlier match");
	}

	void SurfaceTopologyPreservesExteriorTraversalAndOwnership()
	{
		const FIndices Tets{0, 1, 2, 3, 0, 2, 1, 4};
		const FIndices Expected{1, 2, 3, 0, 2, 3, 0, 1, 3, 2, 1, 4, 0, 1, 4, 0, 2, 4};
		FSurfaceTopology Automatic = BuildSurfaceTopology(Tets, {});
		Require(Automatic.Indices == Expected && Automatic.Vertices == FIndices{1, 2, 3, 0, 4}, "exterior faces or unique vertices lost first-appearance order");

		FIndices Supplied = Expected;
		std::reverse(Supplied.begin(), Supplied.end());

		const FSurfaceTopology Explicit = BuildSurfaceTopology(Tets, Supplied);
		Require(Explicit.Indices == Expected && Explicit.Vertices == Automatic.Vertices, "supplied surface ordering changed canonical tetrahedron boundary traversal");
		Automatic.Indices[0] = 99;
		Supplied.clear();
		Require(Explicit.Indices == Expected && Tets == FIndices{0, 1, 2, 3, 0, 2, 1, 4}, "topology result unexpectedly aliases an input or another result");

		const FSurfaceTopology Empty = BuildSurfaceTopology({}, {});
		Require(Empty.Indices.empty() && Empty.Vertices.empty(), "empty topology retained previous exterior faces");
	}

	void SurfaceTopologyRejectsInvalidBoundariesBeforeCachePublication()
	{
		const FIndices Tet{0, 1, 2, 3};
		const FIndices Surface{1, 2, 3, 0, 2, 3, 0, 1, 3, 0, 1, 2};

		for (const FIndices& Invalid : std::vector<FIndices>{{0, 1, 2}, {0, 1, 2, 2}, {0, 1, 2, 3, 0, 1, 2, 4, 0, 1, 2, 5}})
		{
			Rejects<EErrorCode::InvalidArgument>([&]
			{
				FOperationResult Status;
				BuildSurfaceTopology(Invalid, {}, &Status);

				return Status;
			}, "invalid tuple, repeated vertex, or non-manifold face was accepted");
		}

		for (const FIndices& Invalid : std::vector<FIndices>{{0, 1}, {1, 2, 3}, {1, 2, 3, 0, 2, 3, 0, 1, 3, 1, 2, 3}, {1, 2, 3, 0, 2, 3, 0, 1, 3, 0, 1, 4}})
		{
			Rejects<EErrorCode::InvalidArgument>([&]
			{
				FOperationResult Status;
				BuildSurfaceTopology(Tet, Invalid, &Status);

				return Status;
			}, "malformed, missing, duplicate, or non-boundary surface was accepted");
		}

		FSoftBody Valid(Tetra(), {}, Tet, Surface);
		FSoftBody Invalid(Tetra(), {}, Tet, {1, 2, 3});
		FSoftContactSolver Solver;
		Solver.Prepare({&Valid});

		const FContactBodyInfo Before = Solver.GetBodyInfo(&Valid).value();
		Rejects<EErrorCode::InvalidArgument>([&]
		{
			return Solver.Prepare({&Invalid});
		}, "incomplete exterior surface reached the contact cache");
		Require(!Solver.GetBodyInfo(&Invalid).has_value() && Solver.GetBodyInfo(&Valid)->SurfaceIndices == Before.SurfaceIndices && Solver.Query(&Valid, {0.1, 0.2, 0.3}) == FIndices{0}, "failed topology construction published a partial cache or damaged a prepared body");
	}

	void ContactSessionOwnsBatchLifetimeAndPreservesFailedPreparation()
	{
		static_assert(!std::is_copy_constructible_v<FContactSession>);
		static_assert(!std::is_move_constructible_v<FContactSession>);
		static_assert(std::is_same_v<decltype(std::declval<const FContactSession&>().GetContact(0)), const FContact&>);

		FSoftBody Source({0.1, 0.2, 0.3}, {}, {}, {});
		FSoftBody Target(Tetra(), {}, {0, 1, 2, 3}, {});
		FContactSession Session(FContactOptions{});
		Session.Prepare({&Source, &Target});

		const FGeneratedContactRange First = Session.Generate(&Source, &Target);
		Require(First.Begin == 0 && First.End > First.Begin && First.Generation == 1, "initial prepared session did not produce a valid contact range");

		const FContact Contact = Session.GetContact(First.Begin);
		Require(Contact.BodyA == &Source && Contact.BodyB == &Target, "session changed source/target contact identity");

		const FGeneratedContactRange Second = Session.Generate(&Source, &Target);
		Require(Second.Begin == First.End && Second.End > Second.Begin && Second.Generation == First.Generation, "generate discarded earlier contacts or advanced the preparation generation");

		const std::vector<double> SourceBefore = Source.GetPositions();
		const std::vector<double> TargetBefore = Target.GetPositions();
		Rejects<EErrorCode::InvalidArgument>([&]
		{
			return Session.Prepare({nullptr});
		}, "session preparation accepted a null body");
		Require(Source.GetPositions() == SourceBefore && Target.GetPositions() == TargetBefore && Session.GetContact(First.Begin).Normal == Contact.Normal, "failed preparation changed positions or the existing batch");
		Require(Session.Project(static_cast<unsigned>(First.Begin), First.Generation), "failed preparation invalidated an earlier valid contact");

		Source.Reset();
		Target.Reset();
		Session.Prepare({&Source, &Target});

		const FGeneratedContactRange Fresh = Session.Generate(&Source, &Target);
		Require(Fresh.Begin == 0 && Fresh.Generation == First.Generation + 1, "successful preparation did not clear contacts and advance generation");

		const std::vector<double> BeforeRejectedProjection = Source.GetPositions();
		const std::vector<double> BeforeRejectedTarget = Target.GetPositions();
		Rejects<EErrorCode::InvalidArgument>([&]
		{
			FOperationResult Status;
			Session.Project(static_cast<unsigned>(Fresh.Begin), First.Generation, &Status);

			return Status;
		}, "stale generation projected a contact from the replacement batch");
		Rejects<EErrorCode::InvalidArgument>([&]
		{
			FOperationResult Status;
			Session.Project(static_cast<unsigned>(Fresh.End), Fresh.Generation, &Status);

			return Status;
		}, "out-of-range contact index reached projection");
		Require(Source.GetPositions() == BeforeRejectedProjection && Target.GetPositions() == BeforeRejectedTarget, "rejected contact token partially changed particle positions");
		Session.Prepare({});
		Rejects<EErrorCode::InvalidArgument>([&]
		{
			FOperationResult Status;
			Session.Project(static_cast<unsigned>(Fresh.Begin), Fresh.Generation, &Status);

			return Status;
		}, "empty successful preparation retained the previous contact batch");
		Require(StatsValues(Session.GetStats()) == StatsValues(FSoftContactStats{}), "empty session preparation retained previous diagnostics");
	}

	void BoxObstacleGeometryValidationIsAtomic()
	{
		FPhysicsWorld World;
		FObstacle Box;
		Box.Id = 7;
		Box.Position = {1, 2, 3};
		Box.Size = {2, 4, 6};
		World.SetObstacles({Box});

		const std::array<double, 4> InvalidDimensions{0, -1, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()};

		for (const EAxis Axis : AllAxes)
		{
			for (const double InvalidDimension : InvalidDimensions)
			{
				FObstacle InvalidBox = Box;
				InvalidBox.Size[Axis] = InvalidDimension;

				bool bRejected = !World.SetObstacles({InvalidBox}).IsSuccess();
				Require(bRejected && World.GetObstacles().size() == 1 && World.GetObstacles()[0].Id == Box.Id && World.GetObstacles()[0].Size == Box.Size && World.GetObstacles()[0].Position == Box.Position, "invalid box dimensions changed registered geometry");

				if (std::isfinite(InvalidDimension))
				{
					continue;
				}

				InvalidBox = Box;
				InvalidBox.Position[Axis] = InvalidDimension;
				bRejected = !World.SetObstacles({InvalidBox}).IsSuccess();
				Require(bRejected && World.GetObstacles()[0].Position == Box.Position && World.GetObstacles()[0].Size == Box.Size, "nonfinite box position changed registered geometry");
			}
		}
	}

	void StaticContactsPreserveBoundaryObstacleOrderAndPinnedParticles()
	{
		const std::vector<double> Positions{2, -2, 2, 3, 3, 3};
		FSoftBody Body(Positions, {}, {}, {}, {}, {1, 0});
		FSoftBody SilentBody(Positions, {}, {}, {}, {}, {1, 0});
		FSettings Settings;
		Settings.Bounds = 1;

		FObstacle FirstBox;
		FirstBox.Id = 9;
		FirstBox.Position = {1, -1, 1};
		FirstBox.Size = {1, 1, 1};

		FObstacle Box;
		Box.Id = 4;
		Box.Position = {1.5, -1, 1};

		const std::vector<FObstacle> Obstacles{FirstBox, Box};
		FCollisionSolver Solver;
		Solver.SolveStatic(Body, Obstacles, Settings);
		Require(ReadParticleVector(Body.GetPositions(), 0) == FVector3D{2, -1, 1} && ReadParticleVector(Body.GetPositions(), 1) == FVector3D{3, 3, 3}, "static projection changed obstacle order, box face tie, or a pinned particle");

		const std::vector<FContact>& Contacts = Solver.GetContacts();
		Require(Contacts.size() == 5, "boundary/box contacts were skipped or duplicated");

		const std::array<FVector3D, 5> Normals{FVector3D{-1, 0, 0}, FVector3D{0, 1, 0}, FVector3D{0, 0, -1}, FVector3D{1, 0, 0}, FVector3D{1, 0, 0}};
		const std::array<std::uint32_t, 5> ObstacleIds{0, 0, 0, 9, 4};

		for (std::size_t Index = 0; Index < Contacts.size(); ++Index)
		{
			Require(Contacts[Index].BodyA == &Body && Contacts[Index].ParticleIndexA == 0 && Contacts[Index].BodyB == nullptr && Contacts[Index].Normal == Normals[Index] && Contacts[Index].Obstacle == ObstacleIds[Index], "static contact identity or boundary X/Y/Z then obstacle order changed");
		}

		Solver.BeginSubstep();
		Solver.SolveStatic(SilentBody, Obstacles, Settings, false);
		Require(SilentBody.GetPositions() == Body.GetPositions() && Solver.GetContacts().empty(), "silent settle projection changed geometry or recorded contacts");
	}

	void StaticContactsPreserveExteriorBoxAndTangency()
	{
		FSettings Settings;
		Settings.Bounds = 100;

		FObstacle Box;
		Box.Id = 1;
		Box.Size = {2, 2, 2};

		FBall Body(0.5, {1.25, 0, 0});
		FCollisionSolver Solver;
		Solver.SolveStatic(Body, {Box}, Settings);
		Require(ReadParticleVector(Body.GetPositions(), 0) == FVector3D{1.5, 0, 0} && Solver.GetContacts().size() == 1 && Solver.GetContacts()[0].Normal == FVector3D{1, 0, 0}, "exterior box projection lost the closest-point normal");

		FObstacle LargerBox;
		LargerBox.Id = 2;
		LargerBox.Size = {3, 3, 3};
		Body.SetParticlePosition(0, {2, 0, 0});
		Solver.BeginSubstep();
		Solver.SolveStatic(Body, {LargerBox}, Settings);
		Require(ReadParticleVector(Body.GetPositions(), 0) == FVector3D{2, 0, 0} && Solver.GetContacts().size() == 1 && Solver.GetContacts()[0].Obstacle == 2, "box tangency must record a contact without displacement");
		Body.SetParticlePosition(0, {3, 0, 0});
		Solver.BeginSubstep();
		Solver.SolveStatic(Body, {LargerBox, Box}, Settings);
		Require(ReadParticleVector(Body.GetPositions(), 0) == FVector3D{3, 0, 0} && Solver.GetContacts().empty(), "separated particle retained a contact or changed position");
	}

	void SoftOnlyContactsRefreshEditedBounds()
	{
		FPhysicsWorld World;
		FSoftBody* Source = World.AddSoft({20, 0, 0}, {}, {}, {});
		World.AddSoft(Tetra(), {}, {0, 1, 2, 3}, {});
		Source->SetParticlePosition(0, {0.1, 0.2, 0.3});

		FCollisionSolver Collision;
		FSoftContactSolver Solver;
		FSoftContactStats Stats;
		Collision.SolveDynamic(World.GetBodies(), Solver, Stats);
		Require(Stats.GeneratedContacts > 0 && !Collision.GetContacts().empty(), "soft-only broad phase consumed stale bounds after particle editing");
	}

	void ConfigureAtomicityAndSnapshot()
	{
		FSoftBody Body(Tetra(), {}, {0, 1, 2, 3}, {});
		FSoftContactSolver Solver;
		Solver.Prepare({&Body});
		Require(Solver.Query(&Body, {0.1, 0.2, 0.3}) == FIndices{0}, "fixture containment failed");

		FContactBodyInfo Snapshot = Solver.GetBodyInfo(&Body).value();
		const FContactBodyInfo OriginalInfo = Snapshot;
		const std::array<double, 8> OriginalStats = StatsValues(Solver.GetStats());
		const FContactOptions OriginalOptions = Solver.GetOptions();
		std::vector<FContactOptions> Invalid(6, OriginalOptions);
		Invalid[0].CellSize = -1;
		Invalid[1].CellSize = std::numeric_limits<double>::quiet_NaN();
		Invalid[2].Epsilon = 0;
		Invalid[3].Margin = -1;
		Invalid[4].MaxCells = 0;
		Invalid[5].Margin = std::numeric_limits<double>::infinity();

		for (const FContactOptions& Options : Invalid)
		{
			Rejects<EErrorCode::OutOfRange>([&]
			{
				return Solver.Configure(Options);
			}, "invalid configure succeeded");

			const FContactOptions& Current = Solver.GetOptions();
			Require(Current.CellSize == OriginalOptions.CellSize && Current.Epsilon == OriginalOptions.Epsilon && Current.Margin == OriginalOptions.Margin && Current.MaxCells == OriginalOptions.MaxCells, "invalid configure changed options");
			Require(StatsValues(Solver.GetStats()) == OriginalStats, "invalid configure cleared stats");
			Require(SameInfo(Solver.GetBodyInfo(&Body).value(), OriginalInfo), "invalid configure changed cache");
		}

		Snapshot.SurfaceIndices[0] = 99;
		Snapshot.SurfaceVertices[0] = 99;
		Require(SameInfo(Solver.GetBodyInfo(&Body).value(), OriginalInfo), "diagnostic arrays alias cache");
		Require(Solver.Query(&Body, {0.1, 0.2, 0.3}) == FIndices{0}, "invalid configure lost usable cache");

		FContactOptions Changed = OriginalOptions;
		Changed.CellSize = 0.05;
		Changed.MaxCells = 1;
		Solver.Configure(Changed);
		Require(!Solver.GetBodyInfo(&Body), "successful configure retained old registration");
		Require(StatsValues(Solver.GetStats()) == StatsValues(FSoftContactStats{}), "successful configure retained stats");
		Rejects<EErrorCode::OutOfRange>([&]
		{
			FOperationResult Status;
			Solver.Query(&Body, {0.1, 0.2, 0.3}, false, &Status);

			return Status;
		}, "query accepted invalidated cache");
		Require(Snapshot.SurfaceIndices[0] == 99 && Snapshot.SurfaceVertices[0] == 99 && Snapshot.CellSize == OriginalInfo.CellSize, "configure modified previously returned snapshot");
		Rejects<EErrorCode::InvalidArgument>([&]
		{
			return Solver.Prepare({&Body, nullptr});
		}, "null prepare was accepted");
		Solver.Prepare({&Body});
		Require(Solver.Query(&Body, {0.1, 0.2, 0.3}) == FIndices{0}, "prepare did not recover after rejection");
		Require(Solver.GetBodyInfo(&Body)->OverflowTets == 1, "new options were not used during recovery");
		Require(Snapshot.CellSize == OriginalInfo.CellSize, "reprepare modified old snapshot scalars");
	}

	void PreparePublishesCompleteCacheAndBatchDiagnostics()
	{
		FSoftBody First(Tetra(), {}, {0, 1, 2, 3}, {});
		FSoftBody Second(Tetra(), {}, {0, 1, 2, 3}, {}, {10, 0, 0});
		FSoftContactSolver Solver;
		Solver.Prepare({&First});
		Require(Solver.Query(&First, {0.1, 0.2, 0.3}) == FIndices{0}, "initial cache is unusable");

		const std::array<double, 8> BeforeStats = StatsValues(Solver.GetStats());
		const FContactBodyInfo BeforeInfo = Solver.GetBodyInfo(&First).value();
		Rejects<EErrorCode::InvalidArgument>([&]
		{
			return Solver.Prepare({&Second, nullptr});
		}, "invalid batch partially prepared a new body");
		Require(!Solver.GetBodyInfo(&Second), "rejected batch published an earlier body cache");
		Require(StatsValues(Solver.GetStats()) == BeforeStats, "rejected batch replaced prior diagnostic values");
		Require(SameInfo(Solver.GetBodyInfo(&First).value(), BeforeInfo), "rejected batch modified prior cache metadata");
		Require(Solver.Query(&First, {0.1, 0.2, 0.3}) == FIndices{0}, "rejected batch invalidated the prior prepared body");
		Solver.Prepare({&First, &Second});

		const FContactBodyInfo FirstInfo = Solver.GetBodyInfo(&First).value();
		const FContactBodyInfo SecondInfo = Solver.GetBodyInfo(&Second).value();
		Require(Solver.GetStats().HashCells == static_cast<double>(FirstInfo.HashCellCount + SecondInfo.HashCellCount) && Solver.GetStats().OverflowTets == static_cast<double>(FirstInfo.OverflowTets + SecondInfo.OverflowTets), "successful batch diagnostics do not describe both prepared entries");
		Second.MoveTo({20, 1, 0});
		Solver.Prepare({&Second});

		const FVector3D Interior = ReadParticleVector(Second.GetPositions(), 0) + FVector3D{0.1, 0.2, 0.3};
		Require(Solver.Query(&Second, Interior) == FIndices{0}, "warm cache did not refit translated tetrahedron geometry");
		Require(Solver.Query(&Second, {10.1, 0.2, 0.3}).empty(), "warm cache retained the previous translated geometry");
		Require(SecondInfo.SurfaceIndices == Solver.GetBodyInfo(&Second)->SurfaceIndices, "refit unexpectedly rebuilt or changed immutable surface topology");
		Solver.Prepare({});
		Require(StatsValues(Solver.GetStats()) == StatsValues(FSoftContactStats{}), "empty preparation retained diagnostics from a previous batch");
	}

	void ScratchBatchesDoNotLeak()
	{
		FPhysicsWorld First;
		FPhysicsWorld Second;
		FSoftBody* Source = First.AddSoft({0.1, 0.2, 0.3}, {}, {}, {});
		FSoftBody* Target = First.AddSoft(Tetra(), {}, {0, 1, 2, 3}, {});
		First.AddBall(1, {20, 0, 0});

		FSoftBody* NextSource = Second.AddSoft({100.1, 0.2, 0.3}, {}, {}, {});
		FSoftBody* NextTarget = Second.AddSoft(Tetra(), {}, {0, 1, 2, 3}, {}, {100, 0, 0});
		Second.AddBall(1, {120, 0, 0});
		Second.AddBall(1, {121, 0, 0});

		FSoftContactSolver SoftBody;
		FCollisionSolver Collision;

		for (int Repeat = 0; Repeat < 2; ++Repeat)
		{
			Source->Reset();
			Target->Reset();
			Collision.BeginSubstep();

			FSoftContactStats Stats;
			Collision.SolveDynamic(First.GetBodies(), SoftBody, Stats);
			Require(!Collision.GetContacts().empty() && Stats.GeneratedContacts > 0, "warm collision was lost");
		}

		Source->MoveTo({-20, 0, 0});

		const std::vector<std::vector<double>> SeparatedSource = State(*Source);
		const std::vector<std::vector<double>> SeparatedTarget = State(*Target);
		Collision.BeginSubstep();

		FSoftContactStats SeparatedStats;
		Collision.SolveDynamic(First.GetBodies(), SoftBody, SeparatedStats);
		Require(Collision.GetContacts().empty() && SeparatedStats.GeneratedContacts == 0, "noncontact batch replayed old contacts");
		Require(State(*Source) == SeparatedSource && State(*Target) == SeparatedTarget, "noncontact batch changed positions through stale scratch");

		const std::vector<std::unique_ptr<FBody>> Empty;
		Collision.BeginSubstep();

		FSoftContactStats EmptyStats;
		Collision.SolveDynamic(Empty, SoftBody, EmptyStats);
		Require(Collision.GetContacts().empty() && StatsValues(EmptyStats) == StatsValues(FSoftContactStats{}), "empty batch retained old participants");

		std::vector<std::vector<std::vector<double>>> OldState;

		for (const std::unique_ptr<FBody>& Item : First.GetBodies())
		{
			OldState.push_back(State(*Item));
		}

		Collision.BeginSubstep();

		FSoftContactStats NextStats;
		Collision.SolveDynamic(Second.GetBodies(), SoftBody, NextStats);
		Require(NextStats.GeneratedContacts > 0, "replacement batch lost new soft contacts");

		bool bHasNewSoft = false;

		for (const FContact& Contact : Collision.GetContacts())
		{
			const auto Belongs = [&](FBody* Body)
			{
				return std::any_of(Second.GetBodies().begin(), Second.GetBodies().end(), [&](const auto& Item)
				{
					return Item.get() == Body;
				});
			};
			Require(Belongs(Contact.BodyA) && Belongs(Contact.BodyB), "replacement batch retained old body pointer");
			bHasNewSoft = bHasNewSoft || (Contact.bSoft && Contact.BodyA == NextSource && Contact.BodyB == NextTarget);
		}

		Require(bHasNewSoft, "replacement batch did not project its new soft pair");

		for (std::size_t I = 0; I < OldState.size(); ++I)
		{
			Require(OldState[I] == State(*First.GetBodies()[I]), "replacement batch modified a previous body");
		}
	}

	void SettlePreservesUnmovedParticles()
	{
		FPhysicsWorld World;
		Setup(World);

		FSoftBody* Body = World.AddSoft({0, 0, 0, 5, 0, 0}, {}, {}, {}, {}, {}, {9, 9, 9});
		FTimeIntegrator::Predict(*Body, 0.1, World.GetSettings());
		Body->SetParticlePosition(0, {0, 0, 0});
		Body->SetParticlePosition(1, {5, 0, 0});

		for (std::uint32_t I = 0; I < Body->GetVertexCount(); ++I)
		{
			Body->SetParticleVelocity(I, {1, 2, 3});
			Require(ReadParticleVector(Body->GetPreSolveVelocities(), I) == FVector3D{9, 9, 9} && ReadParticleVector(Body->GetVelocities(), I) == FVector3D{1, 2, 3}, "settle fixture must retain distinct current and predicted velocities");
		}

		FObstacle Obstacle;
		Obstacle.Id = 1;
		Obstacle.Size = {2, 2, 2};
		World.SetObstacles({Obstacle});
		World.Settle();
		Require(Body->GetPositions()[0] == 1 && Body->GetPositions()[3] == 5, "settle fixture displacement changed");

		for (EAxis Axis : AllAxes)
		{
			Require(Body->GetVelocities()[ToIndex(Axis)] == 0 && Body->GetPreSolveVelocities()[ToIndex(Axis)] == 0, "displaced particle retained velocity (possible alias snapshot)");
			Require(Body->GetVelocities()[3 + ToIndex(Axis)] == ToIndex(Axis) + 1 && Body->GetPreSolveVelocities()[3 + ToIndex(Axis)] == 9, "settle cleared velocity of an unmoved particle in the same body");
		}

		Require(Body->GetPositions() == Body->GetPreviousPositions(), "settle did not synchronize previous state");
	}

	void AsymmetricSoftBoundsRefit()
	{
		FSoftBody Body({-4, 1, 2, 2, -3, 6, 0, 5, -2}, {}, {}, {});
		Require(Body.GetBoundsMin() == FVector3D{-4, -3, -2} && Body.GetBoundsMax() == FVector3D{2, 5, 6}, "asymmetric soft bounds have incorrect extrema");
		Require(Body.GetCenter() == FVector3D{-1, 1, 2}, "soft center is not the bounding-box midpoint");
		Require(std::abs(Body.GetRadius() - std::sqrt(41.0)) <= 1E-12, "soft radius does not enclose the farthest vertex from its center");

		const FVector3D* CenterAddress = &Body.GetCenter();
		const FVector3D* MinAddress = &Body.GetBoundsMin();
		const FVector3D* MaxAddress = &Body.GetBoundsMax();

		// 더 작은 형상으로 이전 최솟값·최댓값·반지름이 누적되는 오류를 검출합니다.
		const std::array<FVector3D, 3> Smaller{FVector3D{-1, 0, 1}, FVector3D{1, 2, 3}, FVector3D{0, 1, 1}};

		for (std::size_t I = 0; I < Smaller.size(); ++I)
		{
			Body.SetParticlePosition(I, Smaller[I]);
		}

		const std::vector<std::vector<double>> Before = State(Body);
		Body.UpdateBounds();
		Require(Body.GetBoundsMin() == FVector3D{-1, 0, 1} && Body.GetBoundsMax() == FVector3D{1, 2, 3}, "soft refit retained extrema from a previous shape");
		Require(Body.GetCenter() == FVector3D{0, 1, 2} && std::abs(Body.GetRadius() - std::sqrt(3.0)) <= 1E-12, "soft refit retained a previous center or radius");
		Require(State(Body) == Before, "bounds publication changed particle state");
		Require(&Body.GetCenter() == CenterAddress && &Body.GetBoundsMin() == MinAddress && &Body.GetBoundsMax() == MaxAddress, "bounds publication invalidated borrowed result references");
	}

	void BodyInitialVelocityAndBallBounds()
	{
		const FVector3D InitialPosition{-3, 4, 7};
		const FVector3D InitialVelocity{1.25, -2, 3.5};
		FBall Ball(2.5, InitialPosition, InitialVelocity, 0.5);
		const auto VerifyBounds = [&](const FVector3D& ExpectedCenter)
		{
			Require(Ball.GetCenter() == ExpectedCenter && ReadParticleVector(Ball.GetPositions(), 0) == ExpectedCenter, "ball center no longer follows its position");
			Require(Ball.GetRadius() == 2.5 && Ball.GetBoundsMin() == FVector3D{} && Ball.GetBoundsMax() == FVector3D{}, "ball radius or legacy zero bounds changed");
		};
		VerifyBounds(InitialPosition);
		Require(ReadParticleVector(Ball.GetVelocities(), 0) == InitialVelocity && ReadParticleVector(Ball.GetPreSolveVelocities(), 0) == InitialVelocity, "ball did not copy nonzero initial velocity into predicted velocity");
		Ball.MoveTo({8, -1, 2});
		VerifyBounds({8, -1, 2});
		Require(ReadParticleVector(Ball.GetVelocities(), 0) == InitialVelocity && ReadParticleVector(Ball.GetPreSolveVelocities(), 0) == InitialVelocity, "moving ball changed its velocity state");

		const FVector3D EditedPosition{-8, 6, 5};
		Ball.SetParticlePosition(0, EditedPosition);
		Ball.UpdateBounds();
		VerifyBounds(EditedPosition);
		Ball.BeginSubstep();
		VerifyBounds(EditedPosition);
		Ball.Reset();
		VerifyBounds(InitialPosition);
		Require(ReadParticleVector(Ball.GetVelocities(), 0) == FVector3D{} && ReadParticleVector(Ball.GetPreSolveVelocities(), 0) == FVector3D{}, "ball reset restored initial velocity instead of clearing it");

		FSoftBody SoftBody({0, 0, 0, 1, 2, 3}, {}, {}, {}, {}, {}, InitialVelocity);

		for (std::uint32_t I = 0; I < SoftBody.GetVertexCount(); ++I)
		{
			Require(ReadParticleVector(SoftBody.GetVelocities(), I) == InitialVelocity && ReadParticleVector(SoftBody.GetPreSolveVelocities(), I) == FVector3D{}, "soft initial predicted velocity was incorrectly unified with ball initialization");
		}

		SoftBody.Reset();

		for (std::uint32_t I = 0; I < SoftBody.GetVertexCount(); ++I)
		{
			Require(ReadParticleVector(SoftBody.GetVelocities(), I) == FVector3D{} && ReadParticleVector(SoftBody.GetPreSolveVelocities(), I) == FVector3D{}, "soft reset restored initial velocity instead of clearing it");
		}
	}

	void ParticleSettersValidateAndPreserveUnrelatedState()
	{
		FPhysicsWorld World;
		Setup(World);

		FSoftBody* Body = World.AddSoft(Tetra(), {0, 1}, {0, 1, 2, 3}, {}, {}, {}, {1, 2, 3});
		Body->SetParticlePosition(1, {2, 0, 0});
		World.Step(0.1);
		Body->SetDragTarget(FVector3D{7, 8, 9});
		Require(Body->GetEdgeLambdas()[0] != 0 && Body->GetVolumeLambdas()[0] != 0, "setter fixture must contain accumulated multipliers");

		const std::vector<std::vector<double>> Before = State(*Body);
		const std::vector<double> Rest = Body->GetRestPositions();
		const FStorageIdentity OriginalStorage = GetStorageIdentity(*Body);
		const std::array<FVector3D, 3> CachedBounds{Body->GetCenter(), Body->GetBoundsMin(), Body->GetBoundsMax()};
		const double CachedRadius = Body->GetRadius();
		const std::optional<FVector3D> OriginalDrag = Body->GetDragTarget();
		const auto VerifyMetadata = [&]
		{
			Require(Body->GetRestPositions() == Rest && Body->GetDragTarget() == OriginalDrag, "particle setter changed rest state or drag target");
			Require(std::array<FVector3D, 3>{Body->GetCenter(), Body->GetBoundsMin(), Body->GetBoundsMax()} == CachedBounds && Body->GetRadius() == CachedRadius, "particle setter unexpectedly refreshed cached bounds");
			Require(GetStorageIdentity(*Body) == OriginalStorage, "particle setter resized or replaced storage");
		};

		const auto VerifyRejection = [&]
		{
			Require(State(*Body) == Before, "rejected setter partially modified particle state");
			VerifyMetadata();
		};

		const double Nan = std::numeric_limits<double>::quiet_NaN();
		const double Infinity = std::numeric_limits<double>::infinity();

		for (const std::size_t Index : {Body->GetVertexCount(), std::numeric_limits<std::size_t>::max()})
		{
			// 값과 인덱스가 모두 잘못되어도 인덱스 오류를 먼저 보고해야 합니다.
			Rejects<EErrorCode::OutOfRange>([&]
			{
				return Body->SetParticlePosition(Index, {Nan, Infinity, -Infinity});
			}, "position setter did not validate index before values");
			VerifyRejection();
			Rejects<EErrorCode::OutOfRange>([&]
			{
				return Body->SetParticleVelocity(Index, {Nan, Infinity, -Infinity});
			}, "velocity setter did not validate index before values");
			VerifyRejection();
			Rejects<EErrorCode::OutOfRange>([&]
			{
				return Body->SetParticleInverseMass(Index, Nan);
			}, "inverse-mass setter did not validate index before value");
			VerifyRejection();
		}

		for (const double Invalid : {Nan, Infinity, -Infinity})
		{
			for (EAxis Axis : AllAxes)
			{
				FVector3D Value{91, 92, 93};
				Value[Axis] = Invalid;
				Rejects<EErrorCode::InvalidArgument>([&]
				{
					return Body->SetParticlePosition(1, Value);
				}, "position setter accepted a nonfinite component");
				VerifyRejection();
				Rejects<EErrorCode::InvalidArgument>([&]
				{
					return Body->SetParticleVelocity(1, Value);
				}, "velocity setter accepted a nonfinite component");
				VerifyRejection();
			}
		}

		for (const double Invalid : {-1.0, Nan, Infinity, -Infinity})
		{
			Rejects<EErrorCode::InvalidArgument>([&]
			{
				return Body->SetParticleInverseMass(1, Invalid);
			}, "inverse-mass setter accepted an invalid value");
			VerifyRejection();
		}

		std::vector<std::vector<double>> Expected = Before;
		Body->SetParticlePosition(1, {20, 30, 40});
		Expected[0][3] = 20;
		Expected[0][4] = 30;
		Expected[0][5] = 40;
		Require(State(*Body) == Expected, "position setter changed a different particle or another buffer");
		VerifyMetadata();
		Body->SetParticleVelocity(1, {-4, 5, -6});
		Expected[2][3] = -4;
		Expected[2][4] = 5;
		Expected[2][5] = -6;
		Require(State(*Body) == Expected, "velocity setter changed predicted velocity or another buffer");
		VerifyMetadata();

		for (const double ValidMass : {0.0, 2.5})
		{
			Body->SetParticleInverseMass(1, ValidMass);
			Expected[4][1] = ValidMass;
			Require(State(*Body) == Expected, "inverse-mass setter changed velocity or another particle");
			VerifyMetadata();
		}

		Body->UpdateBounds();
		Require(Body->GetBoundsMax() == FVector3D{20, 30, 40}, "explicit bounds refresh missed setter position");
		Require(State(*Body) == Expected && GetStorageIdentity(*Body) == OriginalStorage && Body->GetRestPositions() == Rest && Body->GetDragTarget() == OriginalDrag, "bounds refresh changed unrelated state after setter edits");
	}

	void ClosestTriangleCoversVoronoiRegions()
	{
		struct FCase
		{
			FVector3D Point;
			std::array<double, 3> Weights;
			FVector3D Position;
			double DistanceSquared;
		};

		const FVector3D A{0, 0, 0};
		const FVector3D B{2, 0, 0};
		const FVector3D C{0, 2, 0};
		const std::array<FCase, 8> Cases{{
			{{-1, -1, 1}, {1, 0, 0}, {0, 0, 0}, 3},
			{{3, -1, 1}, {0, 1, 0}, {2, 0, 0}, 3},
			{{-1, 3, 1}, {0, 0, 1}, {0, 2, 0}, 3},
			{{1, -1, 1}, {0.5, 0.5, 0}, {1, 0, 0}, 2},
			{{-1, 1, 1}, {0.5, 0, 0.5}, {0, 1, 0}, 2},
			{{2, 2, 1}, {0, 0.5, 0.5}, {1, 1, 0}, 3},
			{{0.5, 0.5, 1}, {0.5, 0.25, 0.25}, {0.5, 0.5, 0}, 1},
			{{1, 0, 0}, {0.5, 0.5, 0}, {1, 0, 0}, 0},
		}};

		for (const FCase& Case : Cases)
		{
			const FTriangleClosestPoint Hit = FindClosestPointOnTriangle(Case.Point, A, B, C);
			Require(Hit.Weights == Case.Weights, "wrong triangle Voronoi weights");
			Require(Hit.Position == Case.Position, "wrong triangle closest point");
			Require(Hit.DistanceSquared == Case.DistanceSquared, "wrong squared distance");
		}
	}

	void CollapsedEdgesUseRestDirectionAndKeepZeroRestEdges()
	{
		for (const bool bXpbd : {false, true})
		{
			FSettings Settings;
			Settings.bXpbd = bXpbd;
			Settings.EdgeCompliance = 0;
			Settings.PbdStiffness = 1;

			FSoftBody Body({0, 0, 0, 2, 0, 0}, {0, 1}, {}, {}, {}, {0, 2});
			Body.SetParticlePosition(1, {});
			FConstraintSolver::SolveEdges(Body, 0.1, Settings);
			Require(ReadParticleVector(Body.GetPositions(), 0) == FVector3D{} && ReadParticleVector(Body.GetPositions(), 1) == FVector3D{2, 0, 0}, "collapsed edge must recover its rest direction without moving the pinned end");
			Require(Body.GetEdgeLambdas()[0] == 1, "collapsed edge must accumulate the inverse-mass-weighted correction");

			FSoftBody ZeroLength({1, 2, 3, 1, 2, 3}, {0, 1}, {}, {});
			const std::vector<double> Before = ZeroLength.GetPositions();
			FConstraintSolver::SolveEdges(ZeroLength, 0.1, Settings);
			Require(ZeroLength.GetPositions() == Before && ZeroLength.GetEdgeLambdas()[0] == 0, "zero rest-length edge must be skipped without a NaN or multiplier change");
		}
	}

	void PredictionPreservesPinnedParticlesAndDragSnapshot()
	{
		FSettings Settings;
		Settings.Gravity = {100, 200, 300};
		Settings.Damping = 10;

		FSoftBody Body({0, 0, 0, 2, 0, 0}, {}, {}, {}, {}, {0, 2}, {9, 8, 7});
		Body.SetDragTarget(FVector3D{3, 2, -2});

		const std::vector<double> Before = Body.GetPositions();
		FTimeIntegrator::Predict(Body, 0.5, Settings);
		Require(Body.GetPreviousPositions() == Before, "prediction must snapshot positions before drag integration");
		Require(ReadParticleVector(Body.GetPositions(), 0) == FVector3D{} && ReadParticleVector(Body.GetVelocities(), 0) == FVector3D{}, "pinned particle must stay fixed and lose its initial velocity");
		Require(ReadParticleVector(Body.GetVelocities(), 1) == FVector3D{0.8, 0.8, -0.8} && ReadParticleVector(Body.GetPositions(), 1) == FVector3D{2.4, 0.4, -0.4}, "soft drag must replace gravity/damping with the center-to-target velocity");
		Require(Body.GetPreSolveVelocities() == Body.GetVelocities(), "predicted velocity must be recorded after integrating every particle");

		FBall Ball(1, {1, 2, 3});
		Ball.SetDragTarget(FVector3D{5, 6, 7});
		FTimeIntegrator::Predict(Ball, 0.5, Settings);
		Require(ReadParticleVector(Ball.GetVelocities(), 0) == FVector3D{1.2, 1.2, 1.2} && ReadParticleVector(Ball.GetPositions(), 0) == FVector3D{1.6, 2.6, 3.6}, "ball drag must retain its distinct gain and axis update order");
	}

	void ClosestTrianglePreservesDegenerateTieOrder()
	{
		const FTriangleClosestPoint Collinear = FindClosestPointOnTriangle({0.5, 1, 0}, {0, 0, 0}, {1, 0, 0}, {2, 0, 0});
		Require(Collinear.Weights == std::array<double, 3>{0.5, 0.5, 0}, "equal-distance degenerate segments must retain the first AB segment");
		Require(Collinear.Position == FVector3D{0.5, 0, 0} && Collinear.DistanceSquared == 1, "collinear triangle must project onto its boundary segments");

		const FTriangleClosestPoint Repeated = FindClosestPointOnTriangle({1, 1, 0}, {0, 0, 0}, {0, 0, 0}, {2, 0, 0});
		Require(Repeated.Weights == std::array<double, 3>{0, 0.5, 0.5}, "zero length AB segment lost BC projection");

		const FTriangleClosestPoint Collapsed = FindClosestPointOnTriangle({1, 2, 3}, {}, {}, {});
		Require(Collapsed.Weights == std::array<double, 3>{1, 0, 0} && Collapsed.Position == FVector3D{} && Collapsed.DistanceSquared == 14, "collapsed triangle must choose the first vertex");
	}

	void ClosestTriangleIsScaleAndWindingIndependent()
	{
		for (double Scale : {1E-9, 1.0, 1E9})
		{
			const FVector3D Offset{3 * Scale, -2 * Scale, Scale};
			const FVector3D Query = Offset + FVector3D{0.5 * Scale, 0.5 * Scale, Scale};
			const FVector3D A = Offset;
			const FVector3D B = Offset + FVector3D{2 * Scale, 0, 0};
			const FVector3D C = Offset + FVector3D{0, 2 * Scale, 0};
			const FTriangleClosestPoint Forward = FindClosestPointOnTriangle(Query, A, B, C);
			const FTriangleClosestPoint Reverse = FindClosestPointOnTriangle(Query, A, C, B);
			Require(std::abs(Forward.DistanceSquared - Scale * Scale) <= Scale * Scale * 1E-12, "closest-point distance does not scale quadratically");
			Require((Forward.Position - Reverse.Position).Length() <= Scale * 1E-12, "triangle winding changed the geometric closest point");
			Require(std::abs(Forward.Weights[0] + Forward.Weights[1] + Forward.Weights[2] - 1) < 1E-12, "triangle barycentric weights must sum to one");
		}
	}

	void VectorValuesPreserveAxesAndNumericalContracts()
	{
		FVector3D Vector{};

		for (EAxis Axis : AllAxes)
		{
			Vector[Axis] = ToIndex(Axis) + 1;
		}

		Require(Vector.X == 1 && Vector.Y == 2 && Vector.Z == 3,
			"typed axes must address independent XYZ components");

		const FVector3D Snapshot = Vector;
		Vector[EAxis::Y] = 9;
		Require(Snapshot == FVector3D{1, 2, 3}, "vector copies must own independent values");
		Require(FVector3D{1, 0, 0}.Cross({0, 1, 0}) == FVector3D{0, 0, 1},
			"cross product must retain the right handed orientation");
		Require(FVector3D{1E16, -1E16, 1}.Dot({1, 1, 1}) == 1,
			"dot product must retain XYZ accumulation order");
		Require(FVector3D{3, 4, 0}.Length() == 5 && std::isinf(FVector3D{1E308, 0, 0}.Length()),
			"length must preserve PBD square root scaling including overflow");
		Require(!FVector3D{0, std::numeric_limits<double>::quiet_NaN(), 0}.IsFinite() &&
				!FVector3D{0, 0, std::numeric_limits<double>::infinity()}.IsFinite(),
			"finite validation must check every named component");
	}

	void ParticleVectorsPreservePackedStorageAndSignedVolume()
	{
		std::vector<double> Values{1, 2, 3, 4, 5, 6};
		const double* Storage = Values.data();
		const FVector3D Snapshot = ReadParticleVector(Values, 1);

		AddParticleVector(Values, 1, {-1, 2, -3});
		Require(Values.data() == Storage && Values == std::vector<double>{1, 2, 3, 3, 7, 3},
			"vector mutation must retain packed storage and unrelated particles");
		Require(Snapshot == FVector3D{4, 5, 6}, "packed vector reads must return owned values");

		const std::vector<double> Vertices{0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
		Require(CalculateSignedTetrahedronVolume(Vertices) == 1.0 / 6 &&
				CalculateSignedTetrahedronVolume(Vertices, 0, 2, 1, 3) == -1.0 / 6,
			"named vector operations must preserve signed tetrahedron volume");
	}
} // namespace

int main()
{
	const std::vector<std::pair<std::string, std::function<void()>>> Tests{
		{"vector values preserve typed axes and numerical contracts", VectorValuesPreserveAxesAndNumericalContracts},
		{"particle vectors preserve packed storage and signed volume", ParticleVectorsPreservePackedStorageAndSignedVolume},
		{"collapsed and zero rest-length edges preserve projection behavior", CollapsedEdgesUseRestDirectionAndKeepZeroRestEdges},
		{"drag prediction preserves pinned particles and the velocity snapshot", PredictionPreservesPinnedParticlesAndDragSnapshot},
		{"closest triangle covers seven Voronoi regions and an edge boundary", ClosestTriangleCoversVoronoiRegions},
		{"closest triangle preserves degenerate segment tie order", ClosestTrianglePreservesDegenerateTieOrder},
		{"closest triangle preserves scale and winding invariants", ClosestTriangleIsScaleAndWindingIndependent},
		{"tetrahedron query preserves dense sparse overflow order and rebuild", TetrahedronQueryPreservesCandidateOrderAndRebuild},
		{"tetrahedron query switches storage and recovers degenerate geometry", TetrahedronQueryRebuildSwitchesStorageAndRecoversGeometry},
		{"surface BVH preserves tie order and independent values", SurfaceBvhPreservesTieOrderAndIndependentValues},
		{"surface BVH prunes and refits reordered geometry", SurfaceBvhPrunesAndRefitsReorderedGeometry},
		{"surface BVH refits collapsed leaf", SurfaceBvhRefitsCollapsedLeaf},
		{"empty vectors and observing reference versus owning snapshot", EmptyVectorsAndReferenceSnapshots},
		{"seven storage buffers survive step move reset and world growth", StableStorage},
		{"zero and rejected timestep preserve progressed state", ZeroStepPreservesProgressedState},
		{"owner registry rejects stale and wrong-type tokens", OwnerRegistryRejectsStaleAndWrongTypeTokens},
		{"owner registry handles exhaustion without token reuse or leaks", OwnerRegistryExhaustionPreservesOwnersAndLifetime},
		{"bumper scratch preserves contact order and reuse", BumperScratchPreservesContactOrderAndReuse},
		{"bumper obstacle index preserves first match and rebuilds after edits", BumperObstacleIndexPreservesFirstMatchAndRebuilds},
		{"surface topology preserves exterior traversal and owns its result", SurfaceTopologyPreservesExteriorTraversalAndOwnership},
		{"surface topology rejects invalid boundaries before cache publication", SurfaceTopologyRejectsInvalidBoundariesBeforeCachePublication},
		{"contact session owns batch lifetime and preserves failed preparation", ContactSessionOwnsBatchLifetimeAndPreservesFailedPreparation},
		{"static contacts preserve boundary/obstacle order and pinned particles", StaticContactsPreserveBoundaryObstacleOrderAndPinnedParticles},
		{"box obstacle geometry validation preserves registered state", BoxObstacleGeometryValidationIsAtomic},
		{"static contacts preserve exterior box projection and tangency", StaticContactsPreserveExteriorBoxAndTangency},
		{"soft-only contacts refresh edited bounds", SoftOnlyContactsRefreshEditedBounds},
		{"configure atomicity independent diagnostics and prepare recovery", ConfigureAtomicityAndSnapshot},
		{"prepare validates before publication and refits geometry with batch diagnostics", PreparePublishesCompleteCacheAndBatchDiagnostics},
		{"warm scratch batches reject stale contacts and participants", ScratchBatchesDoNotLeak},
		{"settle clears only moved particles in one body", SettlePreservesUnmovedParticles},
		{"asymmetric soft bounds refit without stale extrema", AsymmetricSoftBoundsRefit},
		{"body initial velocity and legacy ball bounds survive move and reset", BodyInitialVelocityAndBallBounds},
		{"particle setters validate atomically and change only their target", ParticleSettersValidateAndPreserveUnrelatedState},
	};

	for (const std::pair<std::string, std::function<void()>>& Test : Tests)
	{
		Test.second();
		std::cout << "PASS " << Test.first << '\n';
	}

	std::cout << Tests.size() << '/' << Tests.size() << " refactoring checks passed\n";

	return 0;
}
