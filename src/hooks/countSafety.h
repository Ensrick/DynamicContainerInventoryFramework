#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

namespace Hooks::CountSafety
{
	inline constexpr auto kMaxRandomAddCount = static_cast<std::uint32_t>(std::numeric_limits<std::int16_t>::max());

	enum class LeveledListCountAction : std::uint8_t
	{
		kSkip,
		kUse,
		kClamp
	};

	struct LeveledListCount
	{
		LeveledListCountAction action;
		std::int16_t value;
	};

	template <class T>
	requires std::is_integral_v<T>
	[[nodiscard]] constexpr LeveledListCount NormalizeLeveledListCount(T a_count) noexcept
	{
		if constexpr (std::is_signed_v<T>) {
			if (a_count <= 0) {
				return { LeveledListCountAction::kSkip, 0 };
			}
		} else if (a_count == 0) {
			return { LeveledListCountAction::kSkip, 0 };
		}

		constexpr auto maxCount = static_cast<std::uint64_t>(std::numeric_limits<std::int16_t>::max());
		const auto positiveCount = static_cast<std::uint64_t>(a_count);
		if (positiveCount > maxCount) {
			return { LeveledListCountAction::kClamp, std::numeric_limits<std::int16_t>::max() };
		}

		return { LeveledListCountAction::kUse, static_cast<std::int16_t>(positiveCount) };
	}

	[[nodiscard]] constexpr bool IsPositiveInventoryCount(std::int32_t a_count) noexcept
	{
		return a_count > 0;
	}

	enum class DirectObjectCountAction : std::uint8_t
	{
		kSkip,
		kUse
	};

	struct DirectObjectCount
	{
		DirectObjectCountAction action;
		std::int32_t value;
	};

	template <class T>
	requires std::is_integral_v<T>
	[[nodiscard]] constexpr DirectObjectCount NormalizeDirectObjectCount(T a_count) noexcept
	{
		if constexpr (std::is_signed_v<T>) {
			if (a_count <= 0) {
				return { DirectObjectCountAction::kSkip, 0 };
			}
		} else if (a_count == 0) {
			return { DirectObjectCountAction::kSkip, 0 };
		}

		const auto positiveCount = static_cast<std::uint64_t>(a_count);
		if (positiveCount > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
			return { DirectObjectCountAction::kSkip, 0 };
		}

		return { DirectObjectCountAction::kUse, static_cast<std::int32_t>(positiveCount) };
	}

	template <class T>
	requires std::is_integral_v<T>
	[[nodiscard]] constexpr bool IsRandomAddCountWithinLimit(T a_count) noexcept
	{
		if constexpr (std::is_signed_v<T>) {
			if (a_count < 0) {
				return false;
			}
		}

		return static_cast<std::uint64_t>(a_count) <= kMaxRandomAddCount;
	}

	struct InventoryCountTotal
	{
		std::uint64_t value;
		bool overflowed;
	};

	[[nodiscard]] constexpr InventoryCountTotal AddPositiveInventoryCount(
		InventoryCountTotal a_total,
		std::int32_t a_count) noexcept
	{
		if (!IsPositiveInventoryCount(a_count) || a_total.overflowed) {
			return a_total;
		}

		const auto positiveCount = static_cast<std::uint64_t>(a_count);
		constexpr auto maxCount = std::numeric_limits<std::uint64_t>::max();
		if (positiveCount > maxCount - a_total.value) {
			return { maxCount, true };
		}

		return { a_total.value + positiveCount, false };
	}
}
