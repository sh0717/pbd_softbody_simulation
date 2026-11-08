#include "collision/CollisionSolver.h"
#include "collision/SoftContactSolver.h"
#include "simulation/SoftBody.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace
{
	constexpr int StaticSettlementIterations = 8;

	// 중심이 거의 일치할 때 법선 정규화 대신 +X 방향을 선택하는 길이 기준입니다.
	constexpr double MinimumContactNormalLength = 1E-15;

	// 상자 내부와 외부 모서리 접촉을 구분하는 제곱길이 기준입니다. 기존 리터럴 정밀도를 유지합니다.
	constexpr double MinimumBoxNormalLengthSquared = 1E-30;

	bool AreBoundsSeparated(const FBody& BodyA, const FBody& BodyB)
	{
		for (EAxis Axis : AllAxes)
		{
			// 경계가 닿는 경우는 분리로 보지 않아 후속 접촉 검사에 포함합니다.
			if (BodyA.GetBoundsMax()[Axis] < BodyB.GetBoundsMin()[Axis] || BodyB.GetBoundsMax()[Axis] < BodyA.GetBoundsMin()[Axis])
			{
				return true;
			}
		}
		return false;
	}

	EAxis FindClosestBoxFaceAxis(const FVector3D& DistanceToFace)
	{
		// 동률에서는 기존 삼항식과 같이 X, Y, Z 순서로 면을 선택합니다.
		if (DistanceToFace.X <= DistanceToFace.Y && DistanceToFace.X <= DistanceToFace.Z)
		{
			return EAxis::X;
		}
		if (DistanceToFace.Y <= DistanceToFace.Z)
		{
			return EAxis::Y;
		}
		return EAxis::Z;
	}
} // namespace

void FCollisionSolver::RecordContact(FBody& BodyA, std::uint32_t VertexIndexA, FBody* BodyB, std::uint32_t VertexIndexB, FVector3D ContactNormal, std::uint32_t ObstacleId)
{
	FVector3D OtherVelocity{};
	if (BodyB)
	{
		OtherVelocity = ReadParticleVector(BodyB->GetPreSolveVelocities(), VertexIndexB);
	}
	const FVector3D RelativeVelocity = ReadParticleVector(BodyA.GetPreSolveVelocities(), VertexIndexA) - OtherVelocity;
	FContact Contact;
	Contact.BodyA = &BodyA;
	Contact.ParticleIndexA = VertexIndexA;
	Contact.BodyB = BodyB;
	Contact.ParticleIndexB = VertexIndexB;
	Contact.Normal = ContactNormal;
	Contact.IncomingNormalVelocity = RelativeVelocity.Dot(ContactNormal);
	Contact.Obstacle = ObstacleId;
	Contacts.push_back(Contact);
}

void FCollisionSolver::SolveParticlePairContact(FBody& InOutBodyA, std::uint32_t VertexIndexA, FBody& InOutBodyB, std::uint32_t VertexIndexB, double ContactRadius)
{
	const double InverseMassA = InOutBodyA.GetInverseMass()[VertexIndexA];
	const double InverseMassB = InOutBodyB.GetInverseMass()[VertexIndexB];
	// 두 입자가 움직일 비율의 합입니다. 고정 여부 검사와 위치 보정 분모에 같은 값을 사용합니다.
	const double InverseMassSum = InverseMassA + InverseMassB;
	if (InverseMassSum == 0)
	{
		return;
	}

	FVector3D ContactNormal = ReadParticleVector(InOutBodyA.GetPositions(), VertexIndexA) - ReadParticleVector(InOutBodyB.GetPositions(), VertexIndexB);
	const double DistanceSquared = ContactNormal.Dot(ContactNormal);
	if (DistanceSquared > ContactRadius * ContactRadius)
	{
		//안부딪혔네
		return;
	}

	// 현재 두 입자의 중심 거리입니다. 접촉 법선 정규화와 반지름 합의 침투량 계산에 사용합니다.
	const double CenterDistance = std::sqrt(DistanceSquared);
	if (CenterDistance > MinimumContactNormalLength)
	{
		ContactNormal = ContactNormal * (1 / CenterDistance);
	}
	else
	{
		ContactNormal = {1, 0, 0};
	}

	// 겹친 거리를 역질량 합으로 나눈 공통 보정량입니다. 각 입자 이동량에는 자기 역질량을 다시 곱합니다.
	const double PenetrationCorrection = std::max(0.0, ContactRadius - CenterDistance) / InverseMassSum;
	AddParticleVector(InOutBodyA.Positions, VertexIndexA, ContactNormal * (InverseMassA * PenetrationCorrection));
	AddParticleVector(InOutBodyB.Positions, VertexIndexB, ContactNormal * (-InverseMassB * PenetrationCorrection));
	RecordContact(InOutBodyA, VertexIndexA, &InOutBodyB, VertexIndexB, ContactNormal);
}

void FCollisionSolver::SolveDynamic(const std::vector<std::unique_ptr<FBody>>& Bodies, FSoftContactSolver& InOutSoftSolver, FStats& InOutStats)
{
	//Hard - Ball , Hard - Soft
	CollectBodies(Bodies);
	SolveBallContacts();

	//Soft - Soft
	CollectSoftPairs();
	SolveSoftPairs(InOutSoftSolver, InOutStats);
}

void FCollisionSolver::CollectBodies(const std::vector<std::unique_ptr<FBody>>& Bodies)
{
	{
		SoftBodies.clear();
		Balls.clear();
		SoftPairs.clear();
		SoftContactParticipants.clear();
		SoftContacts.clear();
	}

	for (const std::unique_ptr<FBody>& Item : Bodies)
	{
		if (Item->IsSoft())
		{
			SoftBodies.push_back(static_cast<FSoftBody*>(Item.get()));
		}
		else
		{
			Balls.push_back(Item.get());
		}
	}

	// 구 충돌의 광역 검사만 이 시점의 경계를 사용합니다. 구가 없으면 연체 쌍의 수집 직전 갱신 한 번으로 충분하며 입자 전체를 중복 순회하지 않습니다.
	if (!Balls.empty())
	{
		for (FSoftBody* Body : SoftBodies)
		{
			Body->UpdateBounds();
		}
	}
}

void FCollisionSolver::SolveBallContacts()
{
	for (std::size_t FirstBallIndex = 0; FirstBallIndex < Balls.size(); ++FirstBallIndex)
	{
		for (std::size_t SecondBallIndex = FirstBallIndex + 1; SecondBallIndex < Balls.size(); ++SecondBallIndex)
		{
			//Ball - Ball
			SolveParticlePairContact(*Balls[FirstBallIndex], 0, *Balls[SecondBallIndex], 0, Balls[FirstBallIndex]->GetRadius() + Balls[SecondBallIndex]->GetRadius());
		}
	}

	for (FSoftBody* SoftBody : SoftBodies)
	{
		for (FBody* Ball : Balls)
		{
			const FVector3D CenterOffset = SoftBody->GetCenter() - ReadParticleVector(Ball->GetPositions(), 0);
			const double CombinedRadius = SoftBody->GetRadius() + Ball->GetRadius();
			if (CenterOffset.Dot(CenterOffset) > CombinedRadius * CombinedRadius)
			{
				continue;
			}

			for (std::uint32_t VertexIndex = 0; VertexIndex < SoftBody->GetVertexCount(); ++VertexIndex)
			{
				SolveParticlePairContact(*SoftBody, VertexIndex, *Ball, 0, Ball->GetRadius());
			}
		}
	}
}

void FCollisionSolver::CollectSoftPairs()
{
	for (FSoftBody* SoftBody : SoftBodies)
	{
		SoftBody->UpdateBounds();
	}
	for (std::size_t FirstBodyIndex = 0; FirstBodyIndex < SoftBodies.size(); ++FirstBodyIndex)
	{
		for (std::size_t SecondBodyIndex = FirstBodyIndex + 1; SecondBodyIndex < SoftBodies.size(); ++SecondBodyIndex)
		{
			FSoftBody* BodyA = SoftBodies[FirstBodyIndex];
			FSoftBody* BodyB = SoftBodies[SecondBodyIndex];
			const FVector3D CenterOffset = BodyA->GetCenter() - BodyB->GetCenter();
			const double CombinedRadius = BodyA->GetRadius() + BodyB->GetRadius();
			if (CenterOffset.Dot(CenterOffset) > CombinedRadius * CombinedRadius)
			{
				continue;
			}
			if (AreBoundsSeparated(*BodyA, *BodyB))
			{
				continue;
			}
			SoftPairs.emplace_back(BodyA, BodyB);
			for (FSoftBody* Body : {BodyA, BodyB})
			{
				if (std::find(SoftContactParticipants.begin(), SoftContactParticipants.end(), Body) == SoftContactParticipants.end())
				{
					SoftContactParticipants.push_back(Body);
				}
			}
		}
	}
}

void FCollisionSolver::SolveSoftPairs(FSoftContactSolver& InOutSoftSolver, FStats& InOutStats)
{
	if (SoftPairs.empty())
	{
		return;
	}
	InOutSoftSolver.Prepare(SoftContactParticipants);
	// 모든 몸체 쌍이 같은 기하 스냅샷에서 접촉을 수집한 뒤 투영합니다.
	for (const std::pair<FSoftBody*, FSoftBody*>& Pair : SoftPairs)
	{
		InOutSoftSolver.Generate(Pair.first, Pair.second, SoftContacts);
	}
	for (const FContact& Contact : SoftContacts)
	{
		if (InOutSoftSolver.Project(Contact))
		{
			Contacts.push_back(Contact);
		}
	}
	InOutStats.BroadPhasePairs += static_cast<double>(SoftPairs.size());
	InOutStats.Accumulate(InOutSoftSolver.GetStats());
	for (FSoftBody* Body : SoftContactParticipants)
	{
		Body->UpdateBounds();
	}
}

void FCollisionSolver::SolveStatic(FBody& InOutBody, const std::vector<FObstacle>& Obstacles, const FSettings& Settings, bool bRecordContacts)
{
	const double ParticleRadius = InOutBody.IsSoft() ? 0 : InOutBody.GetRadius();
	// 구체 반지름을 제외한 중심의 축별 허용 반폭입니다. 연체 입자는 반지름 0으로 검사합니다.
	const double Limit = std::max(0.0, Settings.Bounds - ParticleRadius);
	for (std::uint32_t VertexIndex = 0; VertexIndex < InOutBody.GetVertexCount(); ++VertexIndex)
	{
		if (!InOutBody.GetInverseMass()[VertexIndex])
		{
			continue;
		}

		SolveBoundaryContact(InOutBody, VertexIndex, Limit, bRecordContacts);

		for (const FObstacle& Obstacle : Obstacles)
		{
			FVector3D ContactNormal{};
			const bool bProjected = ProjectBoxObstacle(InOutBody, VertexIndex, Obstacle, ParticleRadius, ContactNormal);
			if (bProjected && bRecordContacts)
			{
				RecordContact(InOutBody, VertexIndex, nullptr, 0, ContactNormal, Obstacle.Id);
			}
		}
	}
}

void FCollisionSolver::SolveBoundaryContact(FBody& InOutBody, std::uint32_t VertexIndex, double Limit, bool bRecordContacts)
{
	const std::size_t PositionOffset = std::size_t(VertexIndex) * 3;
	for (EAxis Axis : AllAxes)
	{
		FVector3D Normal{};
		if (InOutBody.GetPositions()[PositionOffset + ToIndex(Axis)] >= Limit)
		{
			InOutBody.Positions[PositionOffset + ToIndex(Axis)] = Limit;
			Normal[Axis] = -1;
		}
		else if (InOutBody.GetPositions()[PositionOffset + ToIndex(Axis)] <= -Limit)
		{
			InOutBody.Positions[PositionOffset + ToIndex(Axis)] = -Limit;
			Normal[Axis] = 1;
		}
		else
		{
			continue;
		}
		if (bRecordContacts)
		{
			RecordContact(InOutBody, VertexIndex, nullptr, 0, Normal);
		}
	}
}

bool FCollisionSolver::ProjectBoxObstacle(FBody& InOutBody, std::uint32_t VertexIndex, const FObstacle& Obstacle, double ParticleRadius, FVector3D& OutNormal)
{
	const std::size_t PositionOffset = std::size_t(VertexIndex) * 3;
	const FVector3D LocalPosition = ReadParticleVector(InOutBody.GetPositions(), VertexIndex) - Obstacle.Position;
	const FVector3D HalfExtent = Obstacle.Size * 0.5;
	FVector3D ClosestPoint{};
	for (EAxis Axis : AllAxes)
	{
		ClosestPoint[Axis] = std::clamp(LocalPosition[Axis], -HalfExtent[Axis], HalfExtent[Axis]);
	}
	OutNormal = LocalPosition - ClosestPoint;
	const double DistanceSquared = OutNormal.Dot(OutNormal);
	if (DistanceSquared > ParticleRadius * ParticleRadius)
	{
		return false;
	}

	if (DistanceSquared > MinimumBoxNormalLengthSquared)
	{
		const double Distance = std::sqrt(DistanceSquared);
		OutNormal = OutNormal * (1 / Distance);
		AddParticleVector(InOutBody.Positions, VertexIndex, OutNormal * (ParticleRadius - Distance));
		return true;
	}

	// 상자 안이나 면 위에서는 가장 가까운 면을 선택하여 입자 중심을 표면 밖으로 옮깁니다.
	const FVector3D DistanceToFace{HalfExtent.X - std::abs(LocalPosition.X), HalfExtent.Y - std::abs(LocalPosition.Y), HalfExtent.Z - std::abs(LocalPosition.Z)};
	const EAxis Axis = FindClosestBoxFaceAxis(DistanceToFace);
	OutNormal = {};
	OutNormal[Axis] = LocalPosition[Axis] >= 0 ? 1 : -1;
	InOutBody.Positions[PositionOffset + ToIndex(Axis)] = Obstacle.Position[Axis] + OutNormal[Axis] * (HalfExtent[Axis] + ParticleRadius);
	return true;
}

void FCollisionSolver::Restitute(const FSettings& Settings)
{
	for (const FContact& Contact : Contacts)
	{
		const double TargetNormalVelocity = Contact.IncomingNormalVelocity < 0 ? -Settings.Restitution * Contact.IncomingNormalVelocity : Contact.IncomingNormalVelocity;
		const double InverseMassA = Contact.BodyA->GetInverseMass()[Contact.ParticleIndexA];
		if (Contact.bSoft)
		{
			RestituteSoftContact(Contact, TargetNormalVelocity, InverseMassA);
		}
		else
		{
			RestituteParticleContact(Contact, TargetNormalVelocity, InverseMassA);
		}
	}
}

void FCollisionSolver::RestituteSoftContact(const FContact& Contact, double TargetNormalVelocity, double InverseMassA)
{
	// A 입자와 B 표면 정점의 무게중심 가중 역질량을 합칠 반발 계산의 분모입니다.
	double EffectiveInverseMass = InverseMassA;
	FVector3D SurfaceVelocity{};
	for (int TriangleCornerIndex = 0; TriangleCornerIndex < 3; ++TriangleCornerIndex)
	{
		const std::uint32_t VertexIndex = Contact.Indices[TriangleCornerIndex];
		const double Weight = Contact.Weights[TriangleCornerIndex];
		EffectiveInverseMass += Contact.BodyB->GetInverseMass()[VertexIndex] * Weight * Weight;
		SurfaceVelocity = SurfaceVelocity + ReadParticleVector(Contact.BodyB->GetVelocities(), VertexIndex) * Weight;
	}
	if (EffectiveInverseMass <= 0)
	{
		return;
	}
	const double CurrentNormalVelocity = (ReadParticleVector(Contact.BodyA->GetVelocities(), Contact.ParticleIndexA) - SurfaceVelocity).Dot(Contact.Normal);
	const double NormalImpulse = (TargetNormalVelocity - CurrentNormalVelocity) / EffectiveInverseMass;
	AddParticleVector(Contact.BodyA->Velocities, Contact.ParticleIndexA, Contact.Normal * (InverseMassA * NormalImpulse));
	for (int TriangleCornerIndex = 0; TriangleCornerIndex < 3; ++TriangleCornerIndex)
	{
		AddParticleVector(Contact.BodyB->Velocities, Contact.Indices[TriangleCornerIndex], Contact.Normal * (-Contact.BodyB->GetInverseMass()[Contact.Indices[TriangleCornerIndex]] * Contact.Weights[TriangleCornerIndex] * NormalImpulse));
	}
}

void FCollisionSolver::RestituteParticleContact(const FContact& Contact, double TargetNormalVelocity, double InverseMassA)
{
	// 고정 환경은 상대 몸체가 없으므로 역질량과 속도를 모두 0으로 취급합니다.
	double InverseMassB = 0;
	if (Contact.BodyB)
	{
		InverseMassB = Contact.BodyB->GetInverseMass()[Contact.ParticleIndexB];
	}
	const double InverseMassSum = InverseMassA + InverseMassB;
	if (InverseMassSum == 0)
	{
		return;
	}
	FVector3D VelocityB{};
	if (Contact.BodyB)
	{
		VelocityB = ReadParticleVector(Contact.BodyB->GetVelocities(), Contact.ParticleIndexB);
	}
	const double CurrentNormalVelocity = (ReadParticleVector(Contact.BodyA->GetVelocities(), Contact.ParticleIndexA) - VelocityB).Dot(Contact.Normal);
	const double NormalImpulse = (TargetNormalVelocity - CurrentNormalVelocity) / InverseMassSum;
	AddParticleVector(Contact.BodyA->Velocities, Contact.ParticleIndexA, Contact.Normal * (InverseMassA * NormalImpulse));
	if (Contact.BodyB)
	{
		AddParticleVector(Contact.BodyB->Velocities, Contact.ParticleIndexB, Contact.Normal * (-InverseMassB * NormalImpulse));
	}
}

void FCollisionSolver::Settle(const std::vector<std::unique_ptr<FBody>>& Bodies, const std::vector<FObstacle>& Obstacles, const FSettings& Settings)
{
	// 정적 겹침 해소 전 몸체별 위치 복사본입니다. 실제로 이동한 입자만 속도를 초기화할 때 비교합니다.
	std::vector<std::vector<double>> PositionsBeforeProjection;
	PositionsBeforeProjection.reserve(Bodies.size());
	for (const std::unique_ptr<FBody>& Body : Bodies)
	{
		PositionsBeforeProjection.push_back(Body->GetPositions());
	}
	for (int Iteration = 0; Iteration < StaticSettlementIterations; ++Iteration)
	{
		for (const std::unique_ptr<FBody>& Body : Bodies)
		{
			SolveStatic(*Body, Obstacles, Settings, false);
		}
	}
	for (std::size_t BodyIndex = 0; BodyIndex < Bodies.size(); ++BodyIndex)
	{
		FBody& Body = *Bodies[BodyIndex];
		const std::vector<double>& Positions = Body.GetPositions();
		const std::vector<double>& OriginalPositions = PositionsBeforeProjection[BodyIndex];
		std::copy(Positions.begin(), Positions.end(), Body.PreviousPositions.begin());
		for (std::size_t VertexOffset = 0; VertexOffset < Positions.size(); VertexOffset += 3)
		{
			// 입자 하나의 XYZ가 모두 같은 경우에만 기존 속도를 유지합니다.
			if (std::equal(Positions.begin() + VertexOffset, Positions.begin() + VertexOffset + 3, OriginalPositions.begin() + VertexOffset))
			{
				continue;
			}
			for (EAxis Axis : AllAxes)
			{
				Body.PreSolveVelocities[VertexOffset + ToIndex(Axis)] = 0;
				Body.Velocities[VertexOffset + ToIndex(Axis)] = 0;
			}
		}
		Body.UpdateBounds();
	}
}
