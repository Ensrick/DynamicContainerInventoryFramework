#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

namespace Hooks::CountSafety
{
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

	struct InventoryCountTotal
	{
		std::uint32_t value;
		bool saturated;
	};

	[[nodiscard]] constexpr InventoryCountTotal AddPositiveInventoryCount(
		InventoryCountTotal a_total,
		std::int32_t a_count) noexcept
	{
		if (!IsPositiveInventoryCount(a_count) || a_total.saturated) {
			return a_total;
		}

		const auto positiveCount = static_cast<std::uint32_t>(a_count);
		constexpr auto maxCount = std::numeric_limits<std::uint32_t>::max();
		if (positiveCount > maxCount - a_total.value) {
			return { maxCount, true };
		}

		return { a_total.value + positiveCount, false };
	}
}
