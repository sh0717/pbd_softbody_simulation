#pragma once
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <variant>

// 하나의 등록소에서 모든 소유 객체 종류가 같은 토큰 공간을 사용합니다.
// 토큰은 주소와 독립적이며 해제 후 재사용하지 않습니다. uint32 범위는 Wasm32와
// JavaScript Number 양쪽에서 정확합니다. 범위 소진 시 새 소유 객체를 등록하지 않습니다.
// 호출은 직렬화하며 반환 포인터는 해당 토큰의 Destroy 또는 등록소 파괴까지 유효합니다.
template <typename... Types>
class TOwnerRegistry
{
public:
	using FHandle = std::uint32_t;

	/** 작은 상한은 ID 소진 정책을 동일 코드 경로에서 검사하는 데 사용할 수 있습니다. */
	explicit TOwnerRegistry(FHandle InMaximumHandle = std::numeric_limits<FHandle>::max())
		: MaximumHandle(InMaximumHandle)
	{
	}

	TOwnerRegistry(const TOwnerRegistry&) = delete;

	TOwnerRegistry& operator=(const TOwnerRegistry&) = delete;

	TOwnerRegistry(TOwnerRegistry&&) = delete;

	TOwnerRegistry& operator=(TOwnerRegistry&&) = delete;

	// 검증/할당 실패 시 전달받은 소유 객체는 RAII로 해제하고 기존 등록은 보존합니다.
	template <typename T>
	FHandle RegisterOwner(std::unique_ptr<T> Object)
	{
		if (!Object)
		{
			throw std::invalid_argument("Cannot register a null native owner");
		}
		if (NextHandle > MaximumHandle)
		{
			throw std::overflow_error("Native owner handle range exhausted");
		}
		const FHandle Handle = static_cast<FHandle>(NextHandle);
		Objects.emplace(Handle, std::move(Object));
		++NextHandle;
		return Handle;
	}

	template <typename T>
	/**
	 * 토큰의 소유 객체 종류와 생존을 확인한 뒤 비소유 포인터를 반환합니다.
	 * @note 만료되었거나 다른 타입의 토큰이면 예외로 거부합니다.
	 */
	T* Get(FHandle Handle) const
	{
		const auto Found = Objects.find(Handle);
		if (Found != Objects.end())
		{
			if (const std::unique_ptr<T>* Object = std::get_if<std::unique_ptr<T>>(&Found->second))
			{
				return Object->get();
			}
		}
		throw std::invalid_argument("Expired native owner");
	}

	// 만료/다른 종류의 토큰은 무시하므로 중복 해제가 새 소유 객체를 파괴하지 않습니다.
	template <typename T>
	void Destroy(FHandle Handle)
	{
		const auto Found = Objects.find(Handle);
		if (Found != Objects.end() && std::get_if<std::unique_ptr<T>>(&Found->second))
		{
			Objects.erase(Found);
		}
	}

private:
	using FOwner = std::variant<std::unique_ptr<Types>...>;

	// 발급한 토큰에서 타입별 unique_ptr 소유 객체를 찾는 등록소입니다.
	std::unordered_map<FHandle, FOwner> Objects;

	// 이 등록소에서 허용하는 마지막 토큰입니다. 작은 값으로 소진 경로를 검사할 수도 있습니다.
	const FHandle MaximumHandle;

	// uint32 최대 토큰 발급 뒤에도 다음 값을 표현하여 정수 순환을 거부합니다.
	std::uint64_t NextHandle = 1;
};
