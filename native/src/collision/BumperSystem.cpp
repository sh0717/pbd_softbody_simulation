#include "collision/BumperSystem.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{
	constexpr double ContactReentryGraceSeconds = 0.04;
	constexpr double BumperCooldownSeconds = 0.2;
	constexpr double CooldownComparisonToleranceSeconds = 1E-12;
	constexpr double MaximumBumperStrength = 12.0;
} // namespace

void FBumperSystem::Apply(const std::vector<FContact>& Contacts, const std::vector<FObstacle>& Obstacles, double SimulationTime, double SubstepSeconds, double Restitution)
{
	CollectGroups(Contacts, Obstacles);

	for (std::size_t GroupIndex = 0; GroupIndex < ActiveGroups; ++GroupIndex)
	{
		ApplyGroup(Groups[GroupIndex], SimulationTime, SubstepSeconds, Restitution);
	}

	ClearGroups();
	PruneStates(Obstacles);
	ObstacleIndices.clear();
}

void FBumperSystem::ClearGroups() noexcept
{
	for (std::size_t GroupIndex = 0; GroupIndex < ActiveGroups; ++GroupIndex)
	{
		FGroup& Group = Groups[GroupIndex];
		Group.Contacts.clear();
		Group.Body = nullptr;
		Group.Obstacle = nullptr;
	}

	ActiveGroups = 0;
}

void FBumperSystem::CollectGroups(const std::vector<FContact>& Contacts, const std::vector<FObstacle>& Obstacles)
{
	ClearGroups();

	if (Contacts.empty() && States.empty())
	{
		ObstacleIndices.clear();

		return;
	}

	IndexObstacles(Obstacles);

	// 접촉의 최초 출현 순서를 유지하고 그룹별 재사용 용량은 보존합니다.
	for (const FContact& Contact : Contacts)
	{
		const FObstacle* Obstacle = FindObstacle(Obstacles, Contact.Obstacle);

		if (!Obstacle || !Obstacle->bBumper || !std::isfinite(Obstacle->Strength) || Obstacle->Strength <= 0 || !Contact.BodyA->GetInverseMass()[Contact.ParticleIndexA])
		{
			continue;
		}

		auto GroupIterator = std::find_if(Groups.begin(), Groups.begin() + ActiveGroups, [&Contact, Obstacle](const FGroup& ExistingGroup)
		{
			return ExistingGroup.Body == Contact.BodyA && ExistingGroup.Obstacle->Id == Obstacle->Id;
		});

		if (GroupIterator == Groups.begin() + ActiveGroups)
		{
			if (ActiveGroups == Groups.size())
			{
				Groups.emplace_back();
			}

			// emplace_back이 반복자를 무효화할 수 있으므로 다시 취득합니다.
			GroupIterator = Groups.begin() + ActiveGroups++;
			GroupIterator->Body = Contact.BodyA;
			GroupIterator->Obstacle = Obstacle;
		}

		GroupIterator->Contacts.push_back(&Contact);
	}
}

void FBumperSystem::IndexObstacles(const std::vector<FObstacle>& Obstacles)
{
	ObstacleIndices.clear();
	ObstacleIndices.reserve(Obstacles.size());

	for (std::size_t ObstacleIndex = 0; ObstacleIndex < Obstacles.size(); ++ObstacleIndex)
	{
		ObstacleIndices.emplace_back(Obstacles[ObstacleIndex].Id, ObstacleIndex);
	}

	std::sort(ObstacleIndices.begin(), ObstacleIndices.end());
}

const FObstacle* FBumperSystem::FindObstacle(const std::vector<FObstacle>& Obstacles, std::uint32_t ObstacleId) const
{
	const auto Found = std::lower_bound(ObstacleIndices.begin(), ObstacleIndices.end(), ObstacleId, [](const std::pair<std::uint32_t, std::size_t>& Entry, const std::uint32_t RequestedId)
	{
		return Entry.first < RequestedId;
	});

	return Found != ObstacleIndices.end() && Found->first == ObstacleId ? &Obstacles[Found->second] : nullptr;
}

void FBumperSystem::ApplyGroup(const FGroup& Group, double SimulationTime, double SubstepSeconds, double Restitution)
{
	FState& ContactState = States[{Group.Body, Group.Obstacle->Id}];
	const bool bNewContactEpisode = SimulationTime - ContactState.LastTouchTimeSeconds > std::max(SubstepSeconds, ContactState.LastSubstepSeconds) + ContactReentryGraceSeconds;
	ContactState.LastTouchTimeSeconds = SimulationTime;
	ContactState.LastSubstepSeconds = SubstepSeconds;

	if (!bNewContactEpisode || SimulationTime - ContactState.LastFireTimeSeconds < BumperCooldownSeconds - CooldownComparisonToleranceSeconds)
	{
		return;
	}

	if (VertexGenerations.size() < Group.Body->GetVertexCount())
	{
		VertexGenerations.resize(Group.Body->GetVertexCount());
	}

	if (++VertexGeneration == 0)
	{
		std::fill(VertexGenerations.begin(), VertexGenerations.end(), 0);
		++VertexGeneration;
	}

	// 이번 발동에서 추가할 법선 목표속도입니다. 장애물 강도에 허용 상한을 적용합니다.
	const double ClampedStrength = std::min(MaximumBumperStrength, Group.Obstacle->Strength);

	// 여러 접촉이 같은 정점을 가리켜도 한 번만 세는 처리 정점 수입니다. 발동 이벤트에 기록합니다.
	std::size_t AffectedVertexCount = 0;

	for (const FContact* Contact : Group.Contacts)
	{
		if (VertexGenerations[Contact->ParticleIndexA] == VertexGeneration)
		{
			continue;
		}

		VertexGenerations[Contact->ParticleIndexA] = VertexGeneration;
		++AffectedVertexCount;

		const double TargetNormalVelocity = std::max(0.0, -Restitution * Contact->IncomingNormalVelocity) + ClampedStrength;
		const double CurrentNormalVelocity = ReadParticleVector(Group.Body->GetVelocities(), Contact->ParticleIndexA).Dot(Contact->Normal);

		AddParticleVector(Group.Body->Velocities, Contact->ParticleIndexA, Contact->Normal * std::max(0.0, TargetNormalVelocity - CurrentNormalVelocity));
	}

	ContactState.LastFireTimeSeconds = SimulationTime;
	Hits.push_back({Group.Body, Group.Obstacle->Id, SimulationTime, ClampedStrength, AffectedVertexCount});
}

void FBumperSystem::PruneStates(const std::vector<FObstacle>& Obstacles)
{
	for (auto StateIterator = States.begin(); StateIterator != States.end();)
	{
		const std::uint32_t ObstacleId = StateIterator->first.second;

		if (!FindObstacle(Obstacles, ObstacleId))
		{
			StateIterator = States.erase(StateIterator);
		}
		else
		{
			++StateIterator;
		}
	}
}
