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
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
	using emscripten::val;

	using FNativeHandle = std::uintptr_t;

	// 직렬 바인딩 호출의 오류 전달 슬롯입니다. 코어의 상태를 JS가 호출 반환 후 소비합니다.
	FOperationResult BindingResult;

	void RecordOperationResult(FOperationResult Status)
	{
		if (BindingResult && !Status)
		{
			BindingResult = std::move(Status);
		}
	}

	template <auto Function>
	struct TCheckedBinding;

	template <typename Result, typename... Arguments, Result (*Function)(Arguments...)>
	struct TCheckedBinding<Function>
	{
		static Result Call(Arguments... Values)
		{
			BindingResult = {};

			return Function(std::forward<Arguments>(Values)...);
		}
	};

	template <auto Function>
	void RegisterFunction(const char* Name)
	{
		emscripten::function(Name, &TCheckedBinding<Function>::Call);
	}

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
		return GetOwners().RegisterOwner(std::move(Object), &BindingResult);
	}

	template <typename T>
	T* ResolveHandle(FNativeHandle NativeHandle)
	{
		if constexpr (std::is_same_v<T, FBody>)
		{
			// 몸체만 기존 주소 기반 관찰 ABI를 유지합니다. JS가 소유 월드의 생존을 검증하며
			// 몸체의 원시 주소 API는 소유권 토큰이나 임의 정수를 검증하는 보안 경계가 아닙니다.
			if (NativeHandle == 0)
			{
				RecordOperationResult({EErrorCode::InvalidArgument, "Null body handle"});
				return nullptr;
			}

			return reinterpret_cast<T*>(NativeHandle);
		}
		else
		{
			FOperationResult Status;
			T* Owner = GetOwners().Get<T>(NativeHandle, &Status);
			RecordOperationResult(std::move(Status));
			return Owner;
		}
	}

	FSoftBody* SoftBody(FNativeHandle BodyHandle)
	{
		FSoftBody* Body = dynamic_cast<FSoftBody*>(ResolveHandle<FBody>(BodyHandle));

		if (!Body)
		{
			RecordOperationResult({EErrorCode::InvalidArgument, "Expected a SoftBody"});
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
			RecordOperationResult({EErrorCode::OutOfRange, std::string("Invalid ") + Name});
			return Fallback;
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
			RecordOperationResult({EErrorCode::InvalidArgument, "Expected a finite vec3"});
			return {};
		}

		for (EAxis Axis : AllAxes)
		{
			Fallback[Axis] = Input[ToIndex(Axis)].as<double>();

			if (!std::isfinite(Fallback[Axis]))
			{
				RecordOperationResult({EErrorCode::InvalidArgument, "Expected a finite vec3"});
				return {};
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
				RecordOperationResult({EErrorCode::InvalidArgument, "Invalid mesh indices"});
				return {};
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
			RecordOperationResult({EErrorCode::InvalidArgument, "Invalid inverseMass length"});
			return {};
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
			RecordOperationResult({EErrorCode::OutOfRange, "Unknown solver mode"});
			return {};
		}

		Settings.bXpbd = Mode == "xpbd";
		Settings.EdgeCompliance = ReadNumber(Options, "edgeCompliance", Settings.EdgeCompliance);
		Settings.VolumeCompliance = ReadNumber(Options, "volumeCompliance", Settings.VolumeCompliance);
		Settings.PbdStiffness = ReadNumber(Options, "pbdStiffness", Settings.PbdStiffness);
		Settings.Bounds = ReadNumber(Options, "bounds", Settings.Bounds);
		Settings.Restitution = ReadNumber(Options, "restitution", Settings.Restitution);
		Settings.Damping = ReadNumber(Options, "damping", Settings.Damping);
		Settings.Gravity = ReadVector(Options["gravity"]);
		RecordOperationResult(Settings.Validate(Settings.Dt));
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
			RecordOperationResult({EErrorCode::OutOfRange, "Invalid spatial hash cell size"});
		}

		RecordOperationResult(Options.Validate());
		return Options;
	}

	val GetStats(const FSoftContactStats& Stats)
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

// 바인딩은 상태를 저장하고 정상 반환합니다. physics.js가 takeError 결과를 JS 오류로 변환합니다.
EMSCRIPTEN_BINDINGS(pbd_core)
{
	using emscripten::val;

	emscripten::function("takeError", +[]() -> val
	{
		if (BindingResult)
		{
			return val::null();
		}

		val Error = val::object();
		Error.set("code", static_cast<int>(BindingResult.Code));
		Error.set("message", BindingResult.Message);
		BindingResult = {};

		return Error;
	});

	RegisterFunction<+[](val Options) -> FNativeHandle
	{
		std::unique_ptr<FPhysicsWorld> World = std::make_unique<FPhysicsWorld>();
		const FSettings Settings = GetSettings(Options);

		if (!BindingResult)
		{
			return 0;
		}

		RecordOperationResult(World->Configure(Settings));

		if (HasProperty(Options, "softContactOptions"))
		{
			const FContactOptions Contacts = GetContactOptions(Options["softContactOptions"]);

			if (!BindingResult)
			{
				return 0;
			}

			RecordOperationResult(World->GetSoftContacts().Configure(Contacts));
		}

		if (!BindingResult)
		{
			return 0;
		}

		return RegisterOwned(std::move(World));
	}>("createWorld");

	RegisterFunction<+[](FNativeHandle WorldHandle)
	{
		GetOwners().Destroy<FPhysicsWorld>(WorldHandle);
	}>("destroyWorld");

	RegisterFunction<+[](FNativeHandle WorldHandle, val Options)
	{
		FPhysicsWorld* World = ResolveHandle<FPhysicsWorld>(WorldHandle);
		const FSettings Settings = GetSettings(Options);

		if (!BindingResult || !World)
		{
			return;
		}

		RecordOperationResult(World->Configure(Settings));
	}>("configure");

	RegisterFunction<+[](FNativeHandle WorldHandle, val Data, val Offset, val Options) -> FNativeHandle
	{
		std::vector<double> Positions = ReadDoubleArray(ReadMeshValues(Data, "verts", "positions"));
		std::vector<double> InverseMasses = ReadInverseMasses(Options, Positions.size() / 3);
		FPhysicsWorld* World = ResolveHandle<FPhysicsWorld>(WorldHandle);
		FIndices Edges = Indices(ReadMeshValues(Data, "tetEdgeIds", "edgeIndices"));
		FIndices Tetrahedra = Indices(ReadMeshValues(Data, "tetIds", "tetIndices"));
		FIndices Surface = Indices(ReadMeshValues(Data, "tetSurfaceTriIds", "surfaceIndices"));
		const FVector3D PositionOffset = ReadVector(Offset);
		const FVector3D Velocity = ReadVector(Options["velocity"]);

		if (!BindingResult || !World)
		{
			return 0;
		}

		return ToHandle(World->AddSoft(std::move(Positions), std::move(Edges), std::move(Tetrahedra), std::move(Surface), PositionOffset, std::move(InverseMasses), Velocity, &BindingResult));
	}>("addSoft");

	RegisterFunction<+[](FNativeHandle WorldHandle, double Radius, val Position, val Velocity, val Options) -> FNativeHandle
	{
		FPhysicsWorld* World = ResolveHandle<FPhysicsWorld>(WorldHandle);
		const FVector3D Center = ReadVector(Position);
		const FVector3D Speed = ReadVector(Velocity);
		const std::vector<double> InverseMasses = ReadInverseMasses(Options, 1);

		if (!BindingResult || !World)
		{
			return 0;
		}

		return ToHandle(World->AddBall(Radius, Center, Speed, InverseMasses[0], &BindingResult));
	}>("addBall");

	RegisterFunction<+[](FNativeHandle BodyHandle, unsigned Field) -> val
	{
		FBody* Body = ResolveHandle<FBody>(BodyHandle);

		if (!Body)
		{
			return val::undefined();
		}

		FBody& BodyState = *Body;
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
	}>("bodyArray");

	RegisterFunction<+[](FNativeHandle BodyHandle) -> double
	{
		FBody* Body = ResolveHandle<FBody>(BodyHandle);
		return Body ? Body->GetRadius() : 0;
	}>("bodyRadius");

	RegisterFunction<+[](FNativeHandle BodyHandle, val Target)
	{
		FBody* Body = ResolveHandle<FBody>(BodyHandle);
		const FVector3D Position = ReadVector(Target);

		if (!BindingResult || !Body)
		{
			return;
		}

		RecordOperationResult(Body->MoveTo(Position));
	}>("moveBody");

	RegisterFunction<+[](FNativeHandle BodyHandle, val Target)
	{
		FBody* Body = ResolveHandle<FBody>(BodyHandle);
		const std::optional<FVector3D> Position = Target.isNull() ? std::nullopt : std::optional<FVector3D>(ReadVector(Target));

		if (!BindingResult || !Body)
		{
			return;
		}

		RecordOperationResult(Body->SetDragTarget(Position));
	}>("bodyDrag");

	RegisterFunction<+[](FNativeHandle BodyHandle, unsigned Command, double SubstepSeconds, val Options)
	{
		FBody* Body = ResolveHandle<FBody>(BodyHandle);

		if (!Body)
		{
			return;
		}

		if (Command == 0)
		{
			Body->UpdateBounds();
		}
		else if (Command == 1)
		{
			FSoftBody* Soft = SoftBody(BodyHandle);

			if (Soft)
			{
				Soft->ResetLambdas();
			}
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
				RecordOperationResult({EErrorCode::OutOfRange, "Invalid constraint timestep"});
				return;
			}

			FSoftBody* Soft = SoftBody(BodyHandle);

			if (!BindingResult || !Soft)
			{
				return;
			}

			RecordOperationResult(Settings.ValidateSubstep(SubstepSeconds));

			if (!BindingResult)
			{
				return;
			}

			if (Command == 3)
			{
				FConstraintSolver::SolveEdges(*Soft, SubstepSeconds, Settings);
			}
			else
			{
				FConstraintSolver::SolveVolumes(*Soft, SubstepSeconds, Settings);
			}
		}
	}>("bodyCommand");

	RegisterFunction<+[](FNativeHandle WorldHandle, val Values)
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
				RecordOperationResult({EErrorCode::InvalidArgument, "Only box obstacles are supported"});
				return;
			}

			if (HasProperty(InputObstacle, "radius"))
			{
				RecordOperationResult({EErrorCode::InvalidArgument, "Box obstacles do not support radius"});
				return;
			}

			Obstacle.Position = ReadVector(InputObstacle["position"]);
			Obstacle.Size = ReadVector(InputObstacle["size"], {1, 1, 1});
			Obstacle.bBumper = HasProperty(InputObstacle, "bumper") && InputObstacle["bumper"].as<bool>();
			Obstacle.Strength = ReadNumber(InputObstacle, "bumperStrength", 0);

			if (Obstacle.Size.X <= 0 || Obstacle.Size.Y <= 0 || Obstacle.Size.Z <= 0)
			{
				RecordOperationResult({EErrorCode::InvalidArgument, "Invalid obstacle dimensions"});
				return;
			}

			Obstacles.push_back(Obstacle);
		}

		FPhysicsWorld* World = ResolveHandle<FPhysicsWorld>(WorldHandle);

		if (!BindingResult || !World)
		{
			return;
		}

		RecordOperationResult(World->SetObstacles(std::move(Obstacles)));
	}>("setObstacles");

	RegisterFunction<+[](FNativeHandle WorldHandle, double Dt) -> bool
	{
		FPhysicsWorld* World = ResolveHandle<FPhysicsWorld>(WorldHandle);
		return World ? World->Step(Dt, &BindingResult) : false;
	}>("step");

	RegisterFunction<+[](FNativeHandle WorldHandle)
	{
		FPhysicsWorld* World = ResolveHandle<FPhysicsWorld>(WorldHandle);

		if (World)
		{
			World->Settle();
		}
	}>("settle");

	RegisterFunction<+[](FNativeHandle WorldHandle)
	{
		FPhysicsWorld* World = ResolveHandle<FPhysicsWorld>(WorldHandle);

		if (World)
		{
			World->Reset();
		}
	}>("reset");

	RegisterFunction<+[](FNativeHandle WorldHandle) -> val
	{
		FPhysicsWorld* World = ResolveHandle<FPhysicsWorld>(WorldHandle);
		return World ? GetStats(World->GetStats()) : val::undefined();
	}>("worldStats");

	RegisterFunction<+[](FNativeHandle WorldHandle) -> double
	{
		FPhysicsWorld* World = ResolveHandle<FPhysicsWorld>(WorldHandle);
		return World ? World->GetSimulationTime() : 0;
	}>("worldTime");

	RegisterFunction<+[](FNativeHandle WorldHandle) -> unsigned int
	{
		FPhysicsWorld* World = ResolveHandle<FPhysicsWorld>(WorldHandle);
		return World ? static_cast<unsigned int>(World->GetConstraintWorkerCount()) : 0;
	}>("worldWorkerCount");

	RegisterFunction<+[](FNativeHandle WorldHandle) -> val
	{
		val Result = val::array();
		unsigned HitIndex = 0;

		FPhysicsWorld* World = ResolveHandle<FPhysicsWorld>(WorldHandle);

		if (!World)
		{
			return val::undefined();
		}

		for (const FHit& Hit : World->GetHits())
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
	}>("worldHits");

	RegisterFunction<+[](val Values, unsigned VertexA, unsigned VertexB, unsigned VertexC, unsigned VertexD) -> double
	{
		const std::vector<double> Positions = ReadDoubleArray(Values);

		if (std::size_t(std::max({VertexA, VertexB, VertexC, VertexD})) * 3 + 2 >= Positions.size())
		{
			RecordOperationResult({EErrorCode::InvalidArgument, "Invalid tetrahedron indices"});
			return 0;
		}

		return CalculateSignedTetrahedronVolume(Positions, VertexA, VertexB, VertexC, VertexD);
	}>("tetraVolume");

	RegisterFunction<+[](val Options) -> FNativeHandle
	{
		std::unique_ptr<FContactSession> Session = std::make_unique<FContactSession>(GetContactOptions(Options));

		if (!BindingResult)
		{
			return 0;
		}

		RecordOperationResult(Session->GetInitializationResult());

		if (!BindingResult)
		{
			return 0;
		}

		return RegisterOwned(std::move(Session));
	}>("createSolver");

	RegisterFunction<+[](FNativeHandle SolverHandle)
	{
		GetOwners().Destroy<FContactSession>(SolverHandle);
	}>("destroySolver");

	RegisterFunction<+[](FNativeHandle Solver, val Handles)
	{
		FContactSession* SessionPointer = ResolveHandle<FContactSession>(Solver);

		if (!SessionPointer)
		{
			return;
		}

		FContactSession& Session = *SessionPointer;
		std::vector<FSoftBody*> Bodies;

		for (unsigned BodyIndex = 0; BodyIndex < Handles["length"].as<unsigned>(); ++BodyIndex)
		{
			Bodies.push_back(SoftBody(Handles[BodyIndex].as<FNativeHandle>()));
		}

		if (!BindingResult)
		{
			return;
		}

		RecordOperationResult(Session.Prepare(Bodies));
	}>("prepareSolver");

	RegisterFunction<+[](FNativeHandle Solver, FNativeHandle BodyHandleA, FNativeHandle BodyHandleB) -> val
	{
		FContactSession* SessionPointer = ResolveHandle<FContactSession>(Solver);

		if (!SessionPointer)
		{
			return val::undefined();
		}

		FContactSession& Session = *SessionPointer;
		FSoftBody* BodyA = SoftBody(BodyHandleA);
		FSoftBody* BodyB = SoftBody(BodyHandleB);

		if (!BindingResult)
		{
			return val::undefined();
		}

		const FGeneratedContactRange Range = Session.Generate(BodyA, BodyB, &BindingResult);

		if (!BindingResult)
		{
			return val::undefined();
		}

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
	}>("generateContacts");

	RegisterFunction<+[](FNativeHandle Solver, val InputContacts) -> val
	{
		FContactSession* SessionPointer = ResolveHandle<FContactSession>(Solver);

		if (!SessionPointer)
		{
			return val::undefined();
		}

		FContactSession& Session = *SessionPointer;
		val SolvedIndices = val::array();
		unsigned SolvedIndex = 0;

		for (unsigned InputIndex = 0; InputIndex < InputContacts["length"].as<unsigned>(); ++InputIndex)
		{
			const val SerializedContact = InputContacts[InputIndex];

			const std::uint32_t ContactIndex = SerializedContact["_index"].as<unsigned>();

			if (Session.Project(ContactIndex, SerializedContact["_generation"].as<unsigned>(), &BindingResult))
			{
				SolvedIndices.set(SolvedIndex++, InputIndex);
			}

			if (!BindingResult)
			{
				return val::undefined();
			}
		}

		return SolvedIndices;
	}>("projectContacts");

	RegisterFunction<+[](FNativeHandle SolverHandle, FNativeHandle BodyHandle, val QueryPosition) -> val
	{
		FContactSession* Session = ResolveHandle<FContactSession>(SolverHandle);
		FSoftBody* Body = SoftBody(BodyHandle);
		const FVector3D Point = ReadVector(QueryPosition);

		if (!BindingResult || !Session)
		{
			return val::undefined();
		}

		return ToArray(Session->Query(Body, Point, &BindingResult));
	}>("queryPoint");

	RegisterFunction<+[](FNativeHandle SolverHandle) -> val
	{
		FContactSession* Session = ResolveHandle<FContactSession>(SolverHandle);
		return Session ? GetStats(Session->GetStats()) : val::undefined();
	}>("solverStats");

	RegisterFunction<+[](FNativeHandle SolverHandle, FNativeHandle BodyHandle) -> val
	{
		FContactSession* Session = ResolveHandle<FContactSession>(SolverHandle);
		FSoftBody* Body = SoftBody(BodyHandle);

		if (!BindingResult || !Session)
		{
			return val::undefined();
		}

		const std::optional<FContactBodyInfo> Info = Session->GetBodyInfo(Body);

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
	}>("bodyInfo");

	RegisterFunction<+[](val QueryPosition, val VertexA, val VertexB, val VertexC) -> val
	{
		const FTriangleClosestPoint Hit = FindClosestPointOnTriangle(ReadVector(QueryPosition), ReadVector(VertexA), ReadVector(VertexB), ReadVector(VertexC));
		val Out = val::object();
		Out.set("weights", ToArray(Hit.Weights));
		Out.set("point", ToArray(Hit.Position));
		Out.set("distanceSquared", Hit.DistanceSquared);

		return Out;
	}>("closestPointTriangle");
}
