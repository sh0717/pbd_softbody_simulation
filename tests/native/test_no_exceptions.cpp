#include "../../native/bindings/ContactSession.h"
#include "../../native/bindings/OwnerRegistry.h"
#include "simulation/PhysicsWorld.h"
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <type_traits>

#if defined(_WIN32)
#include <process.h>
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#error This regression target must compile with C++ exceptions disabled.
#endif

namespace
{
	static_assert(std::is_void_v<decltype(FConstraintSolver::SolveEdges(std::declval<FSoftBody&>(), 0.1, std::declval<const FSettings&>()))>);
	static_assert(std::is_void_v<decltype(FConstraintSolver::SolveVolumes(std::declval<FSoftBody&>(), 0.1, std::declval<const FSettings&>()))>);
	static_assert(std::is_void_v<decltype(std::declval<FSoftContactSolver&>().Generate(nullptr, nullptr, std::declval<std::vector<FContact>&>()))>);

	void Require(bool bCondition, const char* Message)
	{
		if (!bCondition)
		{
			std::cerr << Message << '\n';
			std::exit(EXIT_FAILURE);
		}
	}

	void RequireContractAbort(const char* Executable, const char* Probe)
	{
#if defined(_WIN32)
		(void)Executable;

		// 한글 작업 경로를 보존하고 CRT 시작 함수의 전역 경로 초기화 방식에 의존하지 않습니다.
		wchar_t ExecutablePath[32768]{};
		const DWORD PathLength = GetModuleFileNameW(nullptr, ExecutablePath, 32768);
		Require(PathLength > 0 && PathLength < 32768, "could not resolve contract probe executable");

		const std::wstring Argument(Probe, Probe + std::strlen(Probe));
		Require(_wspawnl(_P_WAIT, ExecutablePath, ExecutablePath, Argument.c_str(), static_cast<wchar_t*>(nullptr)) == 3, "contract violation did not abort the child");
#else
		const pid_t Child = fork();
		Require(Child >= 0, "could not create contract probe child");

		if (Child == 0)
		{
			execl(Executable, Executable, Probe, static_cast<char*>(nullptr));
			std::_Exit(EXIT_FAILURE);
		}

		int ExitStatus = 0;
		Require(waitpid(Child, &ExitStatus, 0) == Child && WIFSIGNALED(ExitStatus) && WTERMSIG(ExitStatus) == SIGABRT, "contract violation did not abort the child");
#endif
	}

	void RunContractProbe(const char* Probe)
	{
#if defined(_WIN32)
		// 의도적인 종료 검사에서 CRT 오류 대화상자를 띄우지 않습니다.
		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
		FSoftBody Body({0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1}, {0, 1}, {0, 1, 2, 3}, {});
		FSettings Settings;

		if (std::strcmp(Probe, "invalid-edge-scale") == 0)
		{
			Settings.EdgeCompliance = 1E308;
			FConstraintSolver::SolveEdges(Body, 0.1, Settings);
		}
		else if (std::strcmp(Probe, "invalid-volume-scale") == 0)
		{
			Settings.VolumeCompliance = 1E308;
			FConstraintSolver::SolveVolumes(Body, 0.1, Settings);
		}
		else if (std::strcmp(Probe, "zero-substep") == 0)
		{
			FConstraintSolver::SolveEdges(Body, 0, Settings);
		}
		else if (std::strcmp(Probe, "unprepared-contacts") == 0)
		{
			FSoftContactSolver Solver;
			std::vector<FContact> Contacts;
			Solver.Generate(&Body, &Body, Contacts);
		}
	}
} // namespace

int main(int ArgumentCount, char** Arguments)
{
	if (ArgumentCount == 2)
	{
		RunContractProbe(Arguments[1]);

		return 0;
	}

	for (const char* Probe : {"invalid-edge-scale", "invalid-volume-scale", "zero-substep", "unprepared-contacts"})
	{
		RequireContractAbort(Arguments[0], Probe);
	}

	FSoftBody Valid({0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1}, {}, {0, 1, 2, 3}, {});
	FSoftBody NewBody({0, 0, 0}, {}, {}, {});
	FSoftContactSolver Solver;
	Require(Solver.Prepare({&Valid}).IsSuccess(), "initial preparation failed");

	const std::vector<double> Before = Valid.GetPositions();
	const double CellsBefore = Solver.GetStats().HashCells;
	const FOperationResult NullStatus = Solver.Prepare({&NewBody, nullptr});
	Require(NullStatus.Code == EErrorCode::InvalidArgument, "null preparation did not return an argument error");
	Require(!Solver.GetBodyInfo(&NewBody) && Solver.GetStats().HashCells == CellsBefore, "null batch published a partial cache or diagnostics");
	Require(Valid.GetPositions() == Before && Solver.Query(&Valid, {0.1, 0.2, 0.3}) == FIndices{0}, "rejected preparation damaged an existing body");

	FOperationResult Status;
	Require(Solver.Query(&NewBody, {}, false, &Status).empty() && Status.Code == EErrorCode::OutOfRange, "unprepared query was not reported");

	FContactSession Session({});
	Require(Session.Prepare({&Valid}).IsSuccess(), "initial contact session preparation failed");

	const FGeneratedContactRange RejectedRange = Session.Generate(&Valid, &NewBody, &Status);
	Require(Status.Code == EErrorCode::OutOfRange && RejectedRange.Begin == RejectedRange.End, "unprepared generation changed output");

	FPhysicsWorld World;
	Require(!World.AddBall(-1, {}, {}, 1, &Status) && Status.Code == EErrorCode::InvalidArgument && World.GetBodies().empty(), "invalid construction published a body");
	Require(!World.Step(-1, &Status) && Status.Code == EErrorCode::OutOfRange && World.GetSimulationTime() == 0, "invalid timestep changed world state");
	Require(!World.Step(0, &Status) && Status.IsSuccess(), "zero timestep retained a stale failure status");

	TOwnerRegistry<int> Owners(1);
	const std::uint32_t Handle = Owners.RegisterOwner(std::make_unique<int>(7), &Status);
	Require(Handle == 1 && Status.IsSuccess(), "valid owner registration failed");
	Require(Owners.RegisterOwner(std::make_unique<int>(9), &Status) == 0 && Status.Code == EErrorCode::ResourceExhausted, "owner exhaustion was not reported");
	Require(*Owners.Get<int>(Handle, &Status) == 7 && Status.IsSuccess(), "failed registration changed the existing owner");
	std::cout << "No-exception compilation, returned errors and state preservation passed.\n";

	return 0;
}
