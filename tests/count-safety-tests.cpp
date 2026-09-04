#include "hooks/countSafety.h"

#include <cstdint>
#include <limits>

namespace
{
	using Hooks::CountSafety::InventoryCountTotal;
	using Hooks::CountSafety::DirectObjectCountAction;
	using Hooks::CountSafety::LeveledListCountAction;

	constexpr bool HasAction(std::int32_t a_count, LeveledListCountAction a_action, std::int16_t a_value)
	{
		const auto result = Hooks::CountSafety::NormalizeLeveledListCount(a_count);
		return result.action == a_action && result.value == a_value;
	}

	constexpr bool BoundaryCasesPass()
	{
		return HasAction(-16, LeveledListCountAction::kSkip, 0) &&
		       HasAction(-15, LeveledListCountAction::kSkip, 0) &&
		       HasAction(0, LeveledListCountAction::kSkip, 0) &&
		       HasAction(1, LeveledListCountAction::kUse, 1) &&
		       HasAction(32767, LeveledListCountAction::kUse, 32767) &&
		       HasAction(32768, LeveledListCountAction::kClamp, 32767);
	}

	constexpr bool DrakrRegressionPasses()
	{
		// Owned C.O.I.N. integration: MISC DE5012 -> LVLI DE5016. ExtraLeveledItem
		// depletion deltas of -15/-16 must never become unsigned leveled-list counts.
		constexpr std::uint32_t ancientNordDrakr = 0x00DE5012;
		constexpr std::uint32_t randomDrakrList = 0x00DE5016;
		static_assert(ancientNordDrakr != randomDrakrList);

		return !Hooks::CountSafety::IsPositiveInventoryCount(-15) &&
		       !Hooks::CountSafety::IsPositiveInventoryCount(-16) &&
		       Hooks::CountSafety::NormalizeLeveledListCount(-15).action == LeveledListCountAction::kSkip &&
		       Hooks::CountSafety::NormalizeLeveledListCount(-16).action == LeveledListCountAction::kSkip;
	}

	constexpr bool KeywordAggregationPasses()
	{
		auto total = InventoryCountTotal{ 0, false };
		total = Hooks::CountSafety::AddPositiveInventoryCount(total, -16);
		total = Hooks::CountSafety::AddPositiveInventoryCount(total, -15);
		total = Hooks::CountSafety::AddPositiveInventoryCount(total, 0);
		total = Hooks::CountSafety::AddPositiveInventoryCount(total, 1);
		total = Hooks::CountSafety::AddPositiveInventoryCount(total, 32767);
		if (total.value != 32768 || total.overflowed) {
			return false;
		}

		total = Hooks::CountSafety::AddPositiveInventoryCount(
			{ std::numeric_limits<std::uint32_t>::max() - 1ULL, false },
			2);
		if (total.value != static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1 || total.overflowed) {
			return false;
		}

		total = Hooks::CountSafety::AddPositiveInventoryCount(
			{ std::numeric_limits<std::uint64_t>::max() - 1, false },
			2);
		return total.value == std::numeric_limits<std::uint64_t>::max() && total.overflowed;
	}

	constexpr bool OperationalBoundariesPass()
	{
		const auto zeroDirect = Hooks::CountSafety::NormalizeDirectObjectCount(0);
		const auto maxDirect = Hooks::CountSafety::NormalizeDirectObjectCount(std::numeric_limits<std::int32_t>::max());
		const auto tooLargeDirect = Hooks::CountSafety::NormalizeDirectObjectCount(
			static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) + 1);
		const auto uint32MaxDirect = Hooks::CountSafety::NormalizeDirectObjectCount(
			std::numeric_limits<std::uint32_t>::max());

		return zeroDirect.action == DirectObjectCountAction::kSkip &&
		       maxDirect.action == DirectObjectCountAction::kUse &&
		       maxDirect.value == std::numeric_limits<std::int32_t>::max() &&
		       tooLargeDirect.action == DirectObjectCountAction::kSkip &&
		       uint32MaxDirect.action == DirectObjectCountAction::kSkip &&
		       Hooks::CountSafety::IsRandomAddCountWithinLimit(32767) &&
		       !Hooks::CountSafety::IsRandomAddCountWithinLimit(32768) &&
		       !Hooks::CountSafety::IsRandomAddCountWithinLimit(std::numeric_limits<std::uint32_t>::max());
	}

	static_assert(BoundaryCasesPass());
	static_assert(DrakrRegressionPasses());
	static_assert(KeywordAggregationPasses());
	static_assert(OperationalBoundariesPass());
}

int main()
{
	return BoundaryCasesPass() && DrakrRegressionPasses() && KeywordAggregationPasses() && OperationalBoundariesPass() ? 0 : 1;
}
