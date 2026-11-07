#pragma once
#include "collision/SoftContactSolver.h"
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <vector>

struct FGeneratedContactRange
{
	// 이번 Generate가 Pending에 추가한 접촉의 시작 인덱스입니다.
	std::size_t Begin = 0;

	// 생성 범위의 끝 인덱스입니다. [Begin, End)에서 End 자체는 읽지 않습니다.
	std::size_t End = 0;

	// 범위를 발급한 준비 세대입니다. 다음 Prepare 성공 후에는 이전 범위가 만료됩니다.
	unsigned Generation = 0;
};

// 호스트의 독립 솔버 상태를 소유합니다. 몸체는 비소유 관찰자이며 세션보다 오래
// 살아 있어야 합니다. 소유 객체의 유지·교체와 호출 직렬화는 기존 JS 어댑터가 담당합니다.
class FContactSession
{
public:
	explicit FContactSession(const FContactOptions& Options)
	{
		Core.Configure(Options);
	}

	FContactSession(const FContactSession&) = delete;

	FContactSession& operator=(const FContactSession&) = delete;

	FContactSession(FContactSession&&) = delete;

	FContactSession& operator=(FContactSession&&) = delete;

	/**
	 * 준비에 성공한 경우에만 기존 배치를 무효화합니다. Core의 공간 캐시 갱신 실패 보장은
	 * FSoftContactSolver::Prepare를 따르며 Pending/Generation은 실패 시 유지합니다.
	 */
	void Prepare(const std::vector<FSoftBody*>& Bodies)
	{
		Core.Prepare(Bodies);
		Pending.clear();
		++Generation;
	}

	/**
	 * 양방향 접촉을 세션에 추가하고 이번에 생성한 반열린 범위와 세대를 반환합니다.
	 * @note 위치를 변경하지 않으며 반환 범위는 준비 성공 시 만료됩니다.
	 */
	FGeneratedContactRange Generate(FSoftBody* BodyA, FSoftBody* BodyB)
	{
		const std::size_t Begin = Pending.size();
		Core.Generate(BodyA, BodyB, Pending);
		return {Begin, Pending.size(), Generation};
	}

	/**
	 * 결과 직렬화를 위한 읽기 전용 대여입니다. 다음 Generate/Prepare 또는 파괴 시
	 * 무효화될 수 있습니다. 성공한 Generate가 반환한 범위만 읽습니다.
	 */
	const FContact& GetContact(std::size_t Index) const
	{
		return Pending.at(Index);
	}

	/**
	 * 세대와 인덱스를 모두 확인한 뒤 위치를 변경합니다. 과거와 같이 각 접촉을
	 * 순서대로 투영하므로 여러 접촉 중 후속 오류가 앞선 투영을 롤백하지는 않습니다.
	 */
	bool Project(unsigned Index, unsigned ContactGeneration)
	{
		if (ContactGeneration != Generation || Index >= Pending.size())
		{
			throw std::invalid_argument("Expired contact batch");
		}
		return Core.Project(Pending[Index]);
	}

	/** 준비된 몸체에서 점을 포함하는 사면체 인덱스를 독립 배열로 반환합니다. */
	FIndices Query(FSoftBody* Body, FVector3D Point)
	{
		return Core.Query(Body, Point);
	}

	const FStats& GetStats() const noexcept
	{
		return Core.GetStats();
	}

	std::optional<FContactBodyInfo> GetBodyInfo(const FSoftBody* Body) const
	{
		return Core.GetBodyInfo(Body);
	}

private:
	// 이 호스트 세션만 사용하는 독립적인 연체 접촉 솔버와 공간 캐시입니다.
	FSoftContactSolver Core;

	// 준비 이후 생성한 접촉을 소유합니다. Generate는 뒤에 추가하고 Prepare 성공 시 전체를 비웁니다.
	std::vector<FContact> Pending;

	// 현재 Pending의 준비 세대입니다. Project는 호출자가 보낸 세대와 대조해 오래된 접촉을 거부합니다.
	unsigned Generation = 0;
};
