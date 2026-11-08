#pragma once
#include "core/BuildPolicy.h"
#include <string>
#include <utility>

enum class EErrorCode
{
	None,
	InvalidArgument,
	OutOfRange,
	ResourceExhausted
};

/** 외부 입력을 받는 작업의 성공 여부와 호출자가 처리할 오류 원인을 소유합니다. */
struct FOperationResult
{
	// None이면 성공이며 나머지는 호출자가 처리할 실패 종류입니다.
	EErrorCode Code = EErrorCode::None;

	// 실패 원인을 소유합니다. 성공 상태에서는 빈 문자열입니다.
	std::string Message;

	bool IsSuccess() const noexcept
	{
		return Code == EErrorCode::None;
	}

	explicit operator bool() const noexcept
	{
		return IsSuccess();
	}
};

/** 선택적 출력에 작업 결과를 전달하고 성공 여부를 반환합니다. */
inline bool PublishOperationResult(FOperationResult Status, FOperationResult* OutResult)
{
	const bool bSuccess = Status.IsSuccess();

	if (OutResult)
	{
		*OutResult = std::move(Status);
	}

	return bSuccess;
}
