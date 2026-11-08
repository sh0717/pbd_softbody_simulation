#include "simulation/PhysicsWorld.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>

namespace
{

	void Require(bool bValue, const char* Reason)
	{
		if (!bValue)
		{
			std::cerr << "FAIL: " << Reason << '\n';
			std::exit(EXIT_FAILURE);
		}
	}

	void Near(double Actual, double Expected, double Tolerance = 1E-10)
	{
		Require(std::abs(Actual - Expected) <= Tolerance, "numeric mismatch");
	}

	std::vector<double> Tetra()
	{
		return {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
	}

	void Setup(FPhysicsWorld& World)
	{
		FSettings S = World.GetSettings();
		S.Bounds = 100;
		S.Gravity = {};
		S.Damping = 0;
		S.Dt = 0.1;
		S.Substeps = 1;
		S.Iterations = 1;
		S.EdgeCompliance = 0;
		S.VolumeCompliance = 0;
		S.Restitution = 0;
		World.Configure(S);
	}
} // namespace

int main()
{

	int Passed = 0;
	const std::vector<std::pair<std::string, std::function<void()>>> Tests{
		{"signed volume and orientation",
			[]
	{
		Near(CalculateSignedTetrahedronVolume(Tetra()), 1.0 / 6);
		Near(CalculateSignedTetrahedronVolume(Tetra(), 0, 2, 1, 3), -1.0 / 6);
	}},
		{"pinned tetra repairs inversion",
			[]
	{
		FPhysicsWorld W;
		Setup(W);

		FSoftBody* B = W.AddSoft(Tetra(), {}, {0, 1, 2, 3}, {}, {}, {0, 0, 0, 1});
		B->SetParticlePosition(3, {0, 0, -0.3});
		FConstraintSolver::SolveVolumes(*B, 0.1, W.GetSettings());
		Near(B->GetPositions()[11], 1);

		for (int I = 0; I < 9; ++I)
		{
			Near(B->GetPositions()[I], B->GetRestPositions()[I]);
		}
	}},
		{"six-volume constraint matches analytic position and multiplier",
			[]
	{
		for (bool bXpbd : {false, true})
		{
			for (double StepSeconds : {0.1, 0.2})
			{
				for (bool bReverseWinding : {false, true})
				{
					FPhysicsWorld World;
					Setup(World);

					FSettings Settings = World.GetSettings();
					Settings.bXpbd = bXpbd;
					Settings.PbdStiffness = 0.4;
					Settings.VolumeCompliance = 0.001;
					World.Configure(Settings);

					const FIndices Tetrahedra = bReverseWinding ? FIndices{0, 2, 1, 3} : FIndices{0, 1, 2, 3};
					FSoftBody* Body = World.AddSoft(Tetra(), {}, Tetrahedra, {}, {}, {0, 0, 0, 1});
					Body->SetParticlePosition(3, {0, 0, 2});

					const std::vector<double> FixedPositions(Body->GetPositions().begin(), Body->GetPositions().begin() + 9);
					FConstraintSolver::SolveVolumes(*Body, StepSeconds, Settings);

					// 고정된 밑면에서 C = 부호*(z-1), 움직이는 기울기는 부호 하나이므로 해석해를 직접 구합니다.
					const double Sign = bReverseWinding ? -1 : 1;
					const double ComplianceRatio = bXpbd ? 36 * (0.001 / (StepSeconds * StepSeconds)) : 0;
					const double CorrectionScale = (bXpbd ? 1 : 0.4) / (1 + ComplianceRatio);
					Near(Body->GetPositions()[11], 2 - CorrectionScale);
					Near(Body->GetVolumeLambdas()[0], -Sign * CorrectionScale);
					Near(Body->GetRestVolumes()[0], Sign / 6);
					Require(std::equal(FixedPositions.begin(), FixedPositions.end(), Body->GetPositions().begin()), "six-volume projection moved pinned particles");

					if (bXpbd)
					{
						for (int Iteration = 0; Iteration < 8; ++Iteration)
						{
							FConstraintSolver::SolveVolumes(*Body, StepSeconds, Settings);
						}

						Near(Body->GetPositions()[11], 2 - CorrectionScale);
						Near(Body->GetVolumeLambdas()[0], -Sign * CorrectionScale);
					}

					Body->BeginSubstep();
					Near(Body->GetVolumeLambdas()[0], 0);
				}
			}
		}
	}},
		{"six-volume projection distributes correction across unequal masses",
			[]
	{
		FPhysicsWorld World;
		Setup(World);

		FSoftBody* Body = World.AddSoft(Tetra(), {}, {0, 1, 2, 3}, {}, {}, {1, 2, 3, 4});
		Body->SetParticlePosition(3, {0, 0, 2});
		FConstraintSolver::SolveVolumes(*Body, 0.1, World.GetSettings());

		// C=1, 네 기울기 제곱의 역질량 가중합은 33이며 각 축 보정은 해석적으로 정해집니다.
		const std::vector<double> Expected{2.0 / 33, 2.0 / 33, 1.0 / 33, 1 - 4.0 / 33, 0, 0, 0, 1 - 6.0 / 33, 0, 0, 0, 2 - 4.0 / 33};

		for (std::size_t ComponentIndex = 0; ComponentIndex < Expected.size(); ++ComponentIndex)
		{
			Near(Body->GetPositions()[ComponentIndex], Expected[ComponentIndex]);
		}

		Near(Body->GetVolumeLambdas()[0], -1.0 / 33);
	}},
		{"six-volume degeneracy threshold preserves the physical cutoff",
			[]
	{
		for (double Scale : {2E-75, 3E-75})
		{
			FSoftBody Body({0, 0, 0, Scale, 0, 0, 0, Scale, 0, 0, 0, Scale}, {}, {0, 1, 2, 3}, {}, {}, {0, 0, 0, 1});
			Body.SetParticlePosition(3, {0, 0, 2 * Scale});

			FSettings Settings;
			Settings.bXpbd = false;
			FConstraintSolver::SolveVolumes(Body, 0.1, Settings);

			// 길이 자체가 아닌 비율을 비교하여 극소 좌표에서도 퇴화 판정 경계를 검출합니다.
			Near(Body.GetPositions()[11] / Scale, Scale == 2E-75 ? 2 : 1);
		}
	}},
		{"six-volume multiplier records the applied correction limit",
			[]
	{
		FSoftBody Body(Tetra(), {}, {0, 1, 2, 3}, {});
		const double Scale = 1E-4;

		for (std::uint32_t VertexIndex = 0; VertexIndex < 4; ++VertexIndex)
		{
			Body.SetParticlePosition(VertexIndex, ReadParticleVector(Body.GetRestPositions(), VertexIndex) * Scale);
		}

		const std::vector<double> Before = Body.GetPositions();
		FSettings Settings;
		Settings.bXpbd = false;
		FConstraintSolver::SolveVolumes(Body, 0.1, Settings);

		double MaximumDisplacement = 0;

		for (std::uint32_t VertexIndex = 0; VertexIndex < 4; ++VertexIndex)
		{
			MaximumDisplacement = std::max(MaximumDisplacement, (ReadParticleVector(Body.GetPositions(), VertexIndex) - ReadParticleVector(Before, VertexIndex)).Length());
		}

		// 기준 지름 sqrt(2)가 보정 상한이며, 원점 입자의 기울기 길이는 sqrt(3)*Scale²입니다.
		Near(MaximumDisplacement, std::sqrt(2.0));
		Near(Body.GetVolumeLambdas()[0] * Scale * Scale, std::sqrt(2.0 / 3));
	}},
		{"XPBD compliance and accumulated lambda",
			[]
	{
		FPhysicsWorld W;
		Setup(W);

		FSettings Settings = W.GetSettings();
		Settings.EdgeCompliance = 0.02;
		W.Configure(Settings);

		FSoftBody* B = W.AddSoft({0, 0, 0, 1, 0, 0}, {0, 1}, {}, {}, {}, {0, 2});
		B->SetParticlePosition(1, {2, 0, 0});
		FConstraintSolver::SolveEdges(*B, 0.1, W.GetSettings());
		Near(B->GetPositions()[3], 1.5);
		FConstraintSolver::SolveEdges(*B, 0.1, W.GetSettings());
		Near(B->GetPositions()[3], 1.5);
	}},
		{"compliance overflow rejects before mutation",
			[]
	{
		for (bool bEdge : {true, false})
		{
			FPhysicsWorld W;
			Setup(W);

			FSettings S = W.GetSettings();
			S.EdgeCompliance = bEdge ? 1E308 : 0;
			S.VolumeCompliance = bEdge ? 0 : 1E308;
			W.Configure(S);

			FSoftBody* B = W.AddSoft(Tetra(), {0, 1}, {0, 1, 2, 3}, {});
			const std::vector<double> Before = B->GetPositions();
			FOperationResult Status;
			Require(!W.Step(0.1, &Status) && Status.Code == EErrorCode::OutOfRange, "overflow must be rejected before prediction");
			Require(B->GetPositions() == Before, "failed step changed positions");
			Near(W.GetSimulationTime(), 0);
			Require(S.ValidateSubstep(0.1).Code == EErrorCode::OutOfRange, "projection boundary must reject overflow");
		}
	}},
		{"shared substeps integrate gravity",
			[]
	{
		FPhysicsWorld W;
		Setup(W);

		FSettings Settings = W.GetSettings();
		Settings.Gravity = {0, -12, 0};
		Settings.Substeps = 3;
		W.Configure(Settings);

		FBall* A = W.AddBall(0.2, {-4, 0, 0});
		FSoftBody* B = W.AddSoft({4, 0, 0}, {}, {}, {});
		W.Step(0.1);
		Near(A->GetPositions()[1], -0.08);
		Near(B->GetPositions()[1], -0.08);
		Near(W.GetSimulationTime(), 0.1);
	}},
		{"stationary overlap has no launch energy",
			[]
	{
		FPhysicsWorld W;
		Setup(W);

		FBall* A = W.AddBall(1, {-0.5, 0, 0});
		FBall* B = W.AddBall(1, {0.5, 0, 0}, {}, 3);
		W.Step(0.1);
		Near(A->GetPositions()[0], -0.75);
		Near(B->GetPositions()[0], 1.25);
		Near(A->GetVelocities()[0], 0);
		Near(B->GetVelocities()[0], 0);
	}},
		{"normal restitution and momentum",
			[]
	{
		FPhysicsWorld W;
		Setup(W);

		FSettings Settings = W.GetSettings();
		Settings.Restitution = 0.5;
		W.Configure(Settings);

		FBall* A = W.AddBall(1, {-1.1, 0, 0}, {2, 0, 0});
		FBall* B = W.AddBall(1, {1.1, 0, 0}, {-2, 0, 0}, 3);
		W.Step(0.1);
		Near(A->GetVelocities()[0] + B->GetVelocities()[0] / 3, 4.0 / 3);
		Near(B->GetVelocities()[0] - A->GetVelocities()[0], 2);
	}},
		{"local contact moves only contacted triangle",
			[]
	{
		FPhysicsWorld W;
		Setup(W);

		FSoftBody* A = W.AddSoft({0.1, 0.2, 0.3}, {}, {}, {});
		FSoftBody* B = W.AddSoft(Tetra(), {}, {0, 1, 2, 3}, {});
		FSoftContactSolver Solver;
		Solver.Prepare({A, B});

		std::vector<FContact> Contacts;
		Solver.Generate(A, B, Contacts);
		Require(Contacts.size() == 1, "expected local contact");
		Near(Contacts[0].Depth, 0.1);
		Solver.Project(Contacts[0]);
		Near(B->GetPositions()[3], 1);
		Near(B->GetPositions()[4], 0);
		Near(B->GetPositions()[5], 0);
	}},
		{"sparse coordinates and bounded overflow",
			[]
	{
		FPhysicsWorld W;
		Setup(W);

		FSoftBody* B = W.AddSoft(Tetra(), {}, {0, 1, 2, 3}, {}, {1000000, 0, 0});
		FSoftContactSolver Solver;
		FContactOptions Options;
		Options.CellSize = 0.001;
		Options.MaxCells = 2;
		Solver.Configure(Options);
		Solver.Prepare({B});
		Require(Solver.GetBodyInfo(B)->OverflowTets == 1, "missing overflow fallback");
		Require(Solver.Query(B, {1000000.1, 0.2, 0.3}) == FIndices{0}, "lost sparse containment");
	}},
		{"powered bumper and rearming on reset",
			[]
	{
		FPhysicsWorld W;
		Setup(W);

		FBall* B = W.AddBall(1, {2, 0, 0});
		FObstacle O;
		O.Id = 1;
		O.Size = {2, 2, 2};
		O.bBumper = true;
		O.Strength = 4;
		W.SetObstacles({O});
		W.Step(0.1);
		Near(B->GetVelocities()[0], 4);
		Require(W.GetHits().size() == 1, "bumper did not fire");
		W.Reset();
		Near(W.GetSimulationTime(), 0);
		W.Step(0.1);
		Require(W.GetHits().size() == 1, "reset did not rearm");
	}},
		{"zero timestep preserves state",
			[]
	{
		FPhysicsWorld W;
		Setup(W);

		FBall* B = W.AddBall(1, {0, 0, 0}, {1, 2, 3});
		const std::vector<double> P(B->GetPositions().begin(), B->GetPositions().end());
		const std::vector<double> V(B->GetVelocities().begin(), B->GetVelocities().end());
		Require(!W.Step(0), "zero step advanced");
		Require(std::equal(P.begin(), P.end(), B->GetPositions().begin()) && std::equal(V.begin(), V.end(), B->GetVelocities().begin()), "zero step changed state");
	}},
		{"paused editing clears only displaced velocities",
			[]
	{
		FPhysicsWorld W;
		Setup(W);

		FBall* A = W.AddBall(1, {0, 0, 0}, {1, 2, 3});
		FBall* B = W.AddBall(1, {10, 0, 0}, {3, 2, 1});
		FObstacle O;
		O.Id = 1;
		O.Size = {2, 2, 2};
		W.SetObstacles({O});
		W.Settle();
		Near(A->GetPositions()[0], 2);
		Near(A->GetVelocities()[0], 0);
		Near(B->GetVelocities()[0], 3);
	}},
		{"world owns polymorphic bodies and restores rest state",
			[]
	{
		FPhysicsWorld W;
		Setup(W);

		FSoftBody* SoftBody = W.AddSoft(Tetra(), {0, 1}, {0, 1, 2, 3}, {}, {2, 3, 4});
		FBall* Ball = W.AddBall(1, {-4, 0, 0});
		Require(dynamic_cast<FSoftBody*>(W.GetBodies()[0].get()) == SoftBody, "wrong soft body type");
		Require(dynamic_cast<FBall*>(W.GetBodies()[1].get()) == Ball, "wrong ball type");

		const std::vector<double> Rest(SoftBody->GetPositions().begin(), SoftBody->GetPositions().end());
		SoftBody->MoveTo({5, 5, 5});
		SoftBody->SetDragTarget(FVector3D{1, 2, 3});
		W.Reset();
		Require(std::equal(Rest.begin(), Rest.end(), SoftBody->GetPositions().begin()) && !SoftBody->GetDragTarget(), "reset failed");
	}},
		{"invalid settings and topology are rejected",
			[]
	{
		FPhysicsWorld W;
		Setup(W);

		FSettings InvalidSettings = W.GetSettings();
		InvalidSettings.Substeps = 0;
		Require(W.Configure(InvalidSettings).Code == EErrorCode::OutOfRange, "invalid substeps accepted");

		FSoftBody B(Tetra(), {0, 99}, {}, {});
		Require(B.GetInitializationResult().Code == EErrorCode::InvalidArgument, "invalid mesh accepted");
	}},
	};

	for (const std::pair<std::string, std::function<void()>>& Test : Tests)
	{
		Test.second();
		++Passed;
		std::cout << "PASS " << Test.first << '\n';
	}

	std::cout << Passed << " native checks passed\n";

	return 0;
}
