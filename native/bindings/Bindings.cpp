// 브라우저·Node용 어댑터입니다. 솔버 자체는 Emscripten에 의존하지 않습니다.
#include "ContactSession.h"
#include "OwnerRegistry.h"
#include "simulation/PhysicsWorld.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
	using emscripten::val;

	using FNativeHandle = std::uintptr_t;

	TOwnerRegistry<FPhysicsWorld, FContactSession>& GetOwners()
	{
		static TOwnerRegistry<FPhysicsWorld, FContactSession> Owners;
		return Owners;
	}

	template <typename T>
	FNativeHandle ToHandle(T* Object)
	{
		return reinterpret_cast<FNativeHandle>(Object);
	}

	template <typename T>
	FNativeHandle RegisterOwned(std::unique_ptr<T> Object)
	{
		return GetOwners().RegisterOwner(std::move(Object));
	}

	template <typename T>
	T* ResolveHandle(FNativeHandle NativeHandle)
	{
		if constexpr (std::is_same_v<T, FBody>)
		{
			// 몸체만 기존 주소 기반 관찰 ABI를 유지합니다. JS가 소유 월드의 생존을 검증하며
			// 몸체의 원시 주소 API는 소유권 토큰이나 임의 정수를 검증하는 보안 경계가 아닙니다.
			return reinterpret_cast<T*>(NativeHandle);
		}
		else
		{
			return GetOwners().Get<T>(NativeHandle);
		}
	}

	FSoftBody* SoftBody(FNativeHandle BodyHandle)
	{
		FSoftBody* Body = dynamic_cast<FSoftBody*>(ResolveHandle<FBody>(BodyHandle));
		if (!Body)
		{
			throw std::invalid_argument("Expected a SoftBody");
		}
		return Body;
	}

	bool HasProperty(const val& Object, const char* Name)
	{
		return !Object[Name].isUndefined() && !Object[Name].isNull();
	}

	double ReadNumber(const val& Object, const char* Name, double Fallback)
	{
		return HasProperty(Object, Name) ? Object[Name].as<double>() : Fallback;
	}

	int ReadPositiveInteger(const val& Object, const char* Name, int Fallback)
	{
		const double Value = ReadNumber(Object, Name, Fallback);
		if (!std::isfinite(Value) || Value < 1 || Value > 2147483647 || std::floor(Value) != Value)
		{
			throw std::range_error(std::string("Invalid ") + Name);
		}
		return static_cast<int>(Value);
	}

	FVector3D ReadVector(const val& Input, FVector3D Fallback = {})
	{
		if (Input.isUndefined() || Input.isNull())
		{
			return Fallback;
		}
		if (Input["length"].as<unsigned>() != 3)
		{
			throw std::invalid_argument("Expected a finite vec3");
		}
		for (EAxis Axis : AllAxes)
		{
			Fallback[Axis] = Input[ToIndex(Axis)].as<double>();
			if (!std::isfinite(Fallback[Axis]))
			{
				throw std::invalid_argument("Expected a finite vec3");
			}
		}
		return Fallback;
	}

	val ToArray(const FVector3D& Vector)
	{
		val Result = val::array();
		for (EAxis Axis : AllAxes)
		{
			Result.set(ToIndex(Axis), Vector[Axis]);
		}
		return Result;
	}

	template <typename T>
	val ToArray(const T& Values)
	{
		val Result = val::array();
		for (std::size_t ElementIndex = 0; ElementIndex < Values.size(); ++ElementIndex)
		{
			Result.set(ElementIndex, Values[ElementIndex]);
		}
		return Result;
	}

	// XYZ 필드의 실제 바이트 배치를 검증한 뒤 Wasm 메모리 뷰로만 노출합니다.
	// C++에서는 &X를 배열처럼 순회하지 않으며 JS 뷰의 수명은 원본 몸체와 메모리 성장에 종속됩니다.
	static_assert(std::is_standard_layout_v<FVector3D> && sizeof(FVector3D) == sizeof(double) * 3);
	static_assert(offsetof(FVector3D, X) == 0 && offsetof(FVector3D, Y) == sizeof(double) && offsetof(FVector3D, Z) == sizeof(double) * 2);

	val BorrowMemoryView(const FVector3D& Vector)
	{
		return val(emscripten::typed_memory_view(3, &Vector.X));
	}

	template <typename T>
	val BorrowMemoryView(const T& Values)
	{
		return val(emscripten::typed_memory_view(Values.size(), Values.data()));
	}

	std::vector<double> ReadDoubleArray(const val& Values)
	{
		if (Values.isUndefined() || Values.isNull())
		{
			return {};
		}
		return emscripten::convertJSArrayToNumberVector<double>(Values);
	}

	FIndices Indices(const val& Values)
	{
		FIndices Result;
		for (double IndexValue : ReadDoubleArray(Values))
		{
			if (!std::isfinite(IndexValue) || IndexValue < 0 || IndexValue > 4294967295.0 || std::floor(IndexValue) != IndexValue)
			{
				throw std::invalid_argument("Invalid mesh indices");
			}
			Result.push_back(static_cast<std::uint32_t>(IndexValue));
		}
		return Result;
	}

	val ReadMeshValues(const val& Data, const char* PreferredKey, const char* CompatibilityKey)
	{
		return HasProperty(Data, PreferredKey) ? Data[PreferredKey] : Data[CompatibilityKey];
	}

	std::vector<double> ReadInverseMasses(const val& Options, std::size_t VertexCount)
	{
		if (!HasProperty(Options, "inverseMass"))
		{
			return std::vector<double>(VertexCount, 1);
		}
		const val Input = Options["inverseMass"];
		if (Input.typeOf().as<std::string>() == "number")
		{
			return std::vector<double>(VertexCount, Input.as<double>());
		}
		std::vector<double> Result = ReadDoubleArray(Input);
		if (Result.size() != VertexCount)
		{
			throw std::invalid_argument("Invalid inverseMass length");
		}
		return Result;
	}

	FSettings GetSettings(const val& Options)
	{
		FSettings Settings;
		Settings.Dt = ReadNumber(Options, "dt", Settings.Dt);
		Settings.Substeps = ReadPositiveInteger(Options, "substeps", Settings.Substeps);
		Settings.Iterations = ReadPositiveInteger(Options, "solverIterations", Settings.Iterations);
		const std::string Mode = HasProperty(Options, "mode") ? Options["mode"].as<std::string>() : "xpbd";
		if (Mode != "pbd" && Mode != "xpbd")
		{
			throw std::range_error("Unknown solver mode");
		}
		Settings.bXpbd = Mode == "xpbd";
		Settings.EdgeCompliance = ReadNumber(Options, "edgeCompliance", Settings.EdgeCompliance);
		Settings.VolumeCompliance = ReadNumber(Options, "volumeCompliance", Settings.VolumeCompliance);
		Settings.PbdStiffness = ReadNumber(Options, "pbdStiffness", Settings.PbdStiffness);
		Settings.Bounds = ReadNumber(Options, "bounds", Settings.Bounds);
		Settings.Restitution = ReadNumber(Options, "restitution", Settings.Restitution);
		Settings.Damping = ReadNumber(Options, "damping", Settings.Damping);
		Settings.Gravity = ReadVector(Options["gravity"]);
		Settings.Validate(Settings.Dt);
		return Settings;
	}

	FContactOptions GetContactOptions(const val& Input)
	{
		FContactOptions Options;
		Options.CellSize = ReadNumber(Input, "cellSize", 0);
		Options.MaxCells = ReadPositiveInteger(Input, "maxCellsPerTet", 512);
		Options.Epsilon = ReadNumber(Input, "relativeEpsilon", 1E-10);
		Options.Margin = ReadNumber(Input, "contactMargin", 0);
		if (HasProperty(Input, "cellSize") && Options.CellSize <= 0)
		{
			throw std::range_error("Invalid spatial hash cell size");
		}
		return Options;
	}

	val GetStats(const FStats& Stats)
	{
		val Result = val::object();
		Result.set("broadPhasePairs", Stats.BroadPhasePairs);
		Result.set("candidateTests", Stats.CandidateTests);
		Result.set("contacts", Stats.Contacts);
		Result.set("generatedContacts", Stats.GeneratedContacts);
		Result.set("maxPenetration", Stats.MaxPenetration);
		Result.set("overflowTets", Stats.OverflowTets);
		Result.set("hashCells", Stats.HashCells);
		Result.set("surfaceTriangleTests", Stats.SurfaceTriangleTests);
		return Result;
	}

} // namespace

// C++ 예외는 바인딩을 통해 전달하며 physics.js가 변환하고 해제합니다.
EMSCRIPTEN_BINDINGS(pbd_core)
{
	using emscripten::val;

	emscripten::function("createWorld", +[](val Options) -> FNativeHandle
	{
		std::unique_ptr<FPhysicsWorld> World = std::make_unique<FPhysicsWorld>();
		World->Configure(GetSettings(Options));
		if (HasProperty(Options, "softContactOptions"))
		{
			World->GetSoftContacts().Configure(GetContactOptions(Options["softContactOptions"]));
		}
		return RegisterOwned(std::move(World));
	});
	emscripten::function("destroyWorld", +[](FNativeHandle WorldHandle)
	{
		GetOwners().Destroy<FPhysicsWorld>(WorldHandle);
	});
	emscripten::function("configure", +[](FNativeHandle WorldHandle, val Options)
	{
		ResolveHandle<FPhysicsWorld>(WorldHandle)->Configure(GetSettings(Options));
	});
	emscripten::function("addSoft", +[](FNativeHandle WorldHandle, val Data, val Offset, val Options) -> FNativeHandle
	{
		std::vector<double> Positions = ReadDoubleArray(ReadMeshValues(Data, "verts", "positions"));
		std::vector<double> InverseMasses = ReadInverseMasses(Options, Positions.size() / 3);
		return ToHandle(ResolveHandle<FPhysicsWorld>(WorldHandle)->AddSoft(std::move(Positions), Indices(ReadMeshValues(Data, "tetEdgeIds", "edgeIndices")), Indices(ReadMeshValues(Data, "tetIds", "tetIndices")), Indices(ReadMeshValues(Data, "tetSurfaceTriIds", "surfaceIndices")), ReadVector(Offset), std::move(InverseMasses), ReadVector(Options["velocity"])));
	});
	emscripten::function("addBall", +[](FNativeHandle WorldHandle, double Radius, val Position, val Velocity, val Options) -> FNativeHandle
	{
		return ToHandle(ResolveHandle<FPhysicsWorld>(WorldHandle)->AddBall(Radius, ReadVector(Position), ReadVector(Velocity), ReadInverseMasses(Options, 1)[0]));
	});
	emscripten::function("bodyArray", +[](FNativeHandle BodyHandle, unsigned Field) -> val
	{
		FBody& BodyState = *ResolveHandle<FBody>(BodyHandle);
		FSoftBody* SoftBodyPtr = dynamic_cast<FSoftBody*>(&BodyState);
		if (Field >= 6 && Field <= 13 && !SoftBodyPtr)
		{
			return val::undefined();
		}
		switch (Field)
		{
			case 0:
				return BorrowMemoryView(BodyState.GetPositions());
			case 1:
				return BorrowMemoryView(BodyState.GetRestPositions());
			case 2:
				return BorrowMemoryView(BodyState.GetPreviousPositions());
			case 3:
				return BorrowMemoryView(BodyState.GetVelocities());
			case 4:
				return BorrowMemoryView(BodyState.GetPreSolveVelocities());
			case 5:
				return BorrowMemoryView(BodyState.GetInverseMass());
			case 6:
				return BorrowMemoryView(SoftBodyPtr->GetEdgeIndices());
			case 7:
				return BorrowMemoryView(SoftBodyPtr->GetTetIndices());
			case 8:
				return BorrowMemoryView(SoftBodyPtr->GetSurfaceIndices());
			case 9:
				return BorrowMemoryView(SoftBodyPtr->GetRestLengths());
			case 10:
				return BorrowMemoryView(SoftBodyPtr->GetRestVolumes());
			case 11:
				return BorrowMemoryView(SoftBodyPtr->GetEdgeLambdas());
			case 12:
				return BorrowMemoryView(SoftBodyPtr->GetVolumeLambdas());
			case 13:
				return BorrowMemoryView(SoftBodyPtr->GetTetLimits());
			case 14:
				return BodyState.IsSoft() ? BorrowMemoryView(BodyState.GetCenter()) : BorrowMemoryView(BodyState.GetPositions());
			case 15:
				return BorrowMemoryView(BodyState.GetBoundsMin());
			case 16:
				return BorrowMemoryView(BodyState.GetBoundsMax());
			default:
				return val::undefined();
		}
	});
	emscripten::function("bodyRadius", +[](FNativeHandle BodyHandle) -> double
	{
		return ResolveHandle<FBody>(BodyHandle)->GetRadius();
	});
	emscripten::function("moveBody", +[](FNativeHandle BodyHandle, val Target)
	{
		ResolveHandle<FBody>(BodyHandle)->MoveTo(ReadVector(Target));
	});
	emscripten::function("bodyDrag", +[](FNativeHandle BodyHandle, val Target)
	{
		ResolveHandle<FBody>(BodyHandle)->SetDragTarget(Target.isNull() ? std::nullopt : std::optional<FVector3D>(ReadVector(Target)));
	});
	emscripten::function("bodyCommand", +[](FNativeHandle BodyHandle, unsigned Command, double SubstepSeconds, val Options)
	{
		FBody* Body = ResolveHandle<FBody>(BodyHandle);
		if (Command == 0)
		{
			Body->UpdateBounds();
		}
		else if (Command == 1)
		{
			SoftBody(BodyHandle)->ResetLambdas();
		}
		else if (Command == 2)
		{
			Body->Reset();
		}
		else
		{
			FSettings Settings = GetSettings(Options);
			if (!(SubstepSeconds > 0) || !std::isfinite(1 / (SubstepSeconds * SubstepSeconds)))
			{
				throw std::range_error("Invalid constraint timestep");
			}
			if (Command == 3)
			{
				FConstraintSolver::SolveEdges(*SoftBody(BodyHandle), SubstepSeconds, Settings);
			}
			else
			{
				FConstraintSolver::SolveVolumes(*SoftBody(BodyHandle), SubstepSeconds, Settings);
			}
		}
	});
	emscripten::function("setObstacles", +[](FNativeHandle WorldHandle, val Values)
	{
		std::vector<FObstacle> Obstacles;
		for (unsigned ObstacleIndex = 0; ObstacleIndex < Values["length"].as<unsigned>(); ++ObstacleIndex)
		{
			const val InputObstacle = Values[ObstacleIndex];
			FObstacle Obstacle;
			Obstacle.Id = InputObstacle["nativeId"].as<unsigned>();
			const std::string Shape = InputObstacle["shape"].as<std::string>();
			if (Shape != "box")
			{
				throw std::invalid_argument("Only box obstacles are supported");
			}
			if (HasProperty(InputObstacle, "radius"))
			{
				throw std::invalid_argument("Box obstacles do not support radius");
			}
			Obstacle.Position = ReadVector(InputObstacle["position"]);
			Obstacle.Size = ReadVector(InputObstacle["size"], {1, 1, 1});
			Obstacle.bBumper = HasProperty(InputObstacle, "bumper") && InputObstacle["bumper"].as<bool>();
			Obstacle.Strength = ReadNumber(InputObstacle, "bumperStrength", 0);
			if (Obstacle.Size.X <= 0 || Obstacle.Size.Y <= 0 || Obstacle.Size.Z <= 0)
			{
				throw std::invalid_argument("Invalid obstacle dimensions");
			}
			Obstacles.push_back(Obstacle);
		}
		ResolveHandle<FPhysicsWorld>(WorldHandle)->SetObstacles(std::move(Obstacles));
	});
	emscripten::function("step", +[](FNativeHandle WorldHandle, double Dt) -> bool
	{
		return ResolveHandle<FPhysicsWorld>(WorldHandle)->Step(Dt);
	});
	emscripten::function("settle", +[](FNativeHandle WorldHandle)
	{
		ResolveHandle<FPhysicsWorld>(WorldHandle)->Settle();
	});
	emscripten::function("reset", +[](FNativeHandle WorldHandle)
	{
		ResolveHandle<FPhysicsWorld>(WorldHandle)->Reset();
	});
	emscripten::function("worldStats", +[](FNativeHandle WorldHandle) -> val
	{
		return GetStats(ResolveHandle<FPhysicsWorld>(WorldHandle)->GetStats());
	});
	emscripten::function("worldTime", +[](FNativeHandle WorldHandle) -> double
	{
		return ResolveHandle<FPhysicsWorld>(WorldHandle)->GetSimulationTime();
	});
	emscripten::function("worldHits", +[](FNativeHandle WorldHandle) -> val
	{
		val Result = val::array();
		unsigned HitIndex = 0;
		for (const FHit& Hit : ResolveHandle<FPhysicsWorld>(WorldHandle)->GetHits())
		{
			val SerializedHit = val::object();
			SerializedHit.set("body", ToHandle(Hit.Body));
			SerializedHit.set("obstacle", Hit.Obstacle);
			SerializedHit.set("time", Hit.Time);
			SerializedHit.set("strength", Hit.Strength);
			SerializedHit.set("vertices", Hit.Vertices);
			Result.set(HitIndex++, SerializedHit);
		}
		return Result;
	});
	emscripten::function("tetraVolume", +[](val Values, unsigned VertexA, unsigned VertexB, unsigned VertexC, unsigned VertexD) -> double
	{
		const std::vector<double> Positions = ReadDoubleArray(Values);
		if (std::size_t(std::max({VertexA, VertexB, VertexC, VertexD})) * 3 + 2 >= Positions.size())
		{
			throw std::invalid_argument("Invalid tetrahedron indices");
		}
		return CalculateSignedTetrahedronVolume(Positions, VertexA, VertexB, VertexC, VertexD);
	});
	emscripten::function("createSolver", +[](val Options) -> FNativeHandle
	{
		std::unique_ptr<FContactSession> Session = std::make_unique<FContactSession>(GetContactOptions(Options));
		return RegisterOwned(std::move(Session));
	});
	emscripten::function("destroySolver", +[](FNativeHandle SolverHandle)
	{
		GetOwners().Destroy<FContactSession>(SolverHandle);
	});
	emscripten::function("prepareSolver", +[](FNativeHandle Solver, val Handles)
	{
		FContactSession& Session = *ResolveHandle<FContactSession>(Solver);
		std::vector<FSoftBody*> Bodies;
		for (unsigned BodyIndex = 0; BodyIndex < Handles["length"].as<unsigned>(); ++BodyIndex)
		{
			Bodies.push_back(SoftBody(Handles[BodyIndex].as<FNativeHandle>()));
		}
		Session.Prepare(Bodies);
	});
	emscripten::function("generateContacts", +[](FNativeHandle Solver, FNativeHandle BodyHandleA, FNativeHandle BodyHandleB) -> val
	{
		FContactSession& Session = *ResolveHandle<FContactSession>(Solver);
		const FGeneratedContactRange Range = Session.Generate(SoftBody(BodyHandleA), SoftBody(BodyHandleB));
		val Result = val::array();
		for (std::size_t ContactIndex = Range.Begin; ContactIndex < Range.End; ++ContactIndex)
		{
			const FContact& Contact = Session.GetContact(ContactIndex);
			val SerializedContact = val::object();
			SerializedContact.set("a", ToHandle(Contact.BodyA));
			SerializedContact.set("b", ToHandle(Contact.BodyB));
			SerializedContact.set("ai", Contact.ParticleIndexA);
			SerializedContact.set("indices", ToArray(Contact.Indices));
			SerializedContact.set("weights", ToArray(Contact.Weights));
			SerializedContact.set("nx", Contact.Normal.X);
			SerializedContact.set("ny", Contact.Normal.Y);
			SerializedContact.set("nz", Contact.Normal.Z);
			SerializedContact.set("depth", Contact.Depth);
			SerializedContact.set("incoming", Contact.IncomingNormalVelocity);
			SerializedContact.set("margin", Contact.Margin);
			SerializedContact.set("_index", ContactIndex);
			SerializedContact.set("_generation", Range.Generation);
			SerializedContact.set("kind", "soft");
			Result.set(ContactIndex - Range.Begin, SerializedContact);
		}
		return Result;
	});
	emscripten::function("projectContacts", +[](FNativeHandle Solver, val InputContacts) -> val
	{
		FContactSession& Session = *ResolveHandle<FContactSession>(Solver);
		val SolvedIndices = val::array();
		unsigned SolvedIndex = 0;
		for (unsigned InputIndex = 0; InputIndex < InputContacts["length"].as<unsigned>(); ++InputIndex)
		{
			const val SerializedContact = InputContacts[InputIndex];
			const std::uint32_t ContactIndex = SerializedContact["_index"].as<unsigned>();
			if (Session.Project(ContactIndex, SerializedContact["_generation"].as<unsigned>()))
			{
				SolvedIndices.set(SolvedIndex++, InputIndex);
			}
		}
		return SolvedIndices;
	});
	emscripten::function("queryPoint", +[](FNativeHandle SolverHandle, FNativeHandle BodyHandle, val QueryPosition) -> val
	{
		return ToArray(ResolveHandle<FContactSession>(SolverHandle)->Query(SoftBody(BodyHandle), ReadVector(QueryPosition)));
	});
	emscripten::function("solverStats", +[](FNativeHandle SolverHandle) -> val
	{
		return GetStats(ResolveHandle<FContactSession>(SolverHandle)->GetStats());
	});
	emscripten::function("bodyInfo", +[](FNativeHandle SolverHandle, FNativeHandle BodyHandle) -> val
	{
		const std::optional<FContactBodyInfo> Info = ResolveHandle<FContactSession>(SolverHandle)->GetBodyInfo(SoftBody(BodyHandle));
		if (!Info)
		{
			return val::null();
		}
		const FContactBodyInfo& BodyInfo = *Info;
		val SerializedInfo = val::object();
		SerializedInfo.set("surfaceIndices", ToArray(BodyInfo.SurfaceIndices));
		SerializedInfo.set("surfaceVertices", ToArray(BodyInfo.SurfaceVertices));
		SerializedInfo.set("tetCount", BodyInfo.TetCount);
		SerializedInfo.set("surfaceTriangleCount", BodyInfo.SurfaceTriangleCount);
		SerializedInfo.set("cellSize", BodyInfo.CellSize);
		SerializedInfo.set("hashCellCount", BodyInfo.HashCellCount);
		SerializedInfo.set("overflowTets", BodyInfo.OverflowTets);
		return SerializedInfo;
	});
	emscripten::function("closestPointTriangle", +[](val QueryPosition, val VertexA, val VertexB, val VertexC) -> val
	{
		const FTriangleClosestPoint Hit = FindClosestPointOnTriangle(ReadVector(QueryPosition), ReadVector(VertexA), ReadVector(VertexB), ReadVector(VertexC));
		val Out = val::object();
		Out.set("weights", ToArray(Hit.Weights));
		Out.set("point", ToArray(Hit.Position));
		Out.set("distanceSquared", Hit.DistanceSquared);
		return Out;
	});
}
