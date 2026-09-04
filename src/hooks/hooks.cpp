#include "Hooks/hooks.h"
#include "hooks/countSafety.h"

#include "merchantCache/merchantCache.h"
#include "utilities/utilities.h"
#include "RE/offset.h"

#include <limits>

namespace {
	struct RuleLogContext
	{
		std::string_view configPath;
		std::string_view friendlyName;
		RE::TESObjectREFR* container;
		RE::TESForm* source;
	};

	std::uint32_t GetFormID(const RE::TESForm* a_form)
	{
		return a_form ? a_form->GetFormID() : 0;
	}

	template <class T>
	void ResolveLeveledList(
		RE::TESLeveledList* a_levItem,
		RE::BSScrapArray<RE::CALCED_OBJECT>* a_result,
		T a_count,
		const RuleLogContext& a_context,
		RE::TESForm* a_target)
	{
		const auto normalized = Hooks::CountSafety::NormalizeLeveledListCount(a_count);
		if (normalized.action == Hooks::CountSafety::LeveledListCountAction::kSkip) {
			if (a_count == 0) {
				logger::debug(
					"Config <{}>/[{}] skipped zero leveled-list count "
					"(container 0x{:08X}, base 0x{:08X}, source 0x{:08X}, target 0x{:08X})",
					a_context.configPath,
					a_context.friendlyName,
					GetFormID(a_context.container),
					GetFormID(a_context.container ? a_context.container->GetBaseObject() : nullptr),
					GetFormID(a_context.source),
					GetFormID(a_target));
			} else {
				logger::warn(
					"Config <{}>/[{}] skipped nonpositive leveled-list count {} "
					"(container 0x{:08X}, base 0x{:08X}, source 0x{:08X}, target 0x{:08X})",
					a_context.configPath,
					a_context.friendlyName,
					a_count,
					GetFormID(a_context.container),
					GetFormID(a_context.container ? a_context.container->GetBaseObject() : nullptr),
					GetFormID(a_context.source),
					GetFormID(a_target));
			}
			return;
		}
		if (normalized.action == Hooks::CountSafety::LeveledListCountAction::kClamp) {
			logger::warn(
				"Config <{}>/[{}] leveled-list count {} exceeds the engine limit; clamping to {} "
				"(container 0x{:08X}, base 0x{:08X}, source 0x{:08X}, target 0x{:08X})",
				a_context.configPath,
				a_context.friendlyName,
				a_count,
				normalized.value,
				GetFormID(a_context.container),
				GetFormID(a_context.container ? a_context.container->GetBaseObject() : nullptr),
				GetFormID(a_context.source),
				GetFormID(a_target));
		}

		RE::BSScrapArray<RE::CALCED_OBJECT> temp{};
		a_levItem->CalculateCurrentFormList(RE::PlayerCharacter::GetSingleton()->GetLevel(), normalized.value, temp, 0, true);

		for (auto& it : temp) {
			auto* form = it.form;
			auto* leveledForm = form->As<RE::TESLeveledList>();
			if (leveledForm) {
				ResolveLeveledList(leveledForm, a_result, it.count, a_context, form);
			}
			else {
				a_result->push_back(it);
			}
		}
	}

	template <class T>
	void AddLeveledListToContainer(
		RE::TESLeveledList* a_list,
		RE::TESObjectREFR* a_container,
		T a_count,
		const RuleLogContext& a_context,
		RE::TESForm* a_target)
	{
		RE::BSScrapArray<RE::CALCED_OBJECT> result{};
		ResolveLeveledList(a_list, &result, a_count, a_context, a_target);
		if (result.size() < 1) return;

		for (auto& obj : result) {
			auto* thingToAdd = static_cast<RE::TESBoundObject*>(obj.form);
			if (!thingToAdd) continue;
			a_container->AddObjectToContainer(thingToAdd, nullptr, obj.count, nullptr);
		}
	}

	void AddObjectCountToContainer(RE::TESBoundObject* a_object, RE::TESObjectREFR* a_container, std::uint32_t a_count)
	{
		constexpr auto maxEngineCount = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
		while (a_count > maxEngineCount) {
			a_container->AddObjectToContainer(a_object, nullptr, std::numeric_limits<std::int32_t>::max(), nullptr);
			a_count -= maxEngineCount;
		}
		if (a_count > 0) {
			a_container->AddObjectToContainer(a_object, nullptr, static_cast<std::int32_t>(a_count), nullptr);
		}
	}
}
namespace Hooks {
	void Install()
	{
		ContainerManager::Install();
	}

	void ContainerManager::Install()
	{
		auto& trampoline = REL::GetTrampoline();
		REL::Relocation<std::uintptr_t> initializeTarget{ RE::Offset::TESObjectREFR::Initialize, 0x78C };
		REL::Relocation<std::uintptr_t> resetTarget{ RE::Offset::TESObjectREFR::Reset, 0x12B };

		_initialize = trampoline.write_call<5>(initializeTarget.address(), Initialize);
		_reset = trampoline.write_call<5>(resetTarget.address(), Reset);
	}

	RE::BGSLocation* ContainerManager::GetNearestMarkerLocation(RE::TESObjectREFR* a_container)
	{
		const auto containerWorld = a_container->GetWorldspace();
		if (containerWorld && this->worldspaceMarkers.contains(containerWorld)) {
			const auto& vec = worldspaceMarkers[containerWorld];
			float lastDistance = maxLookupDistance;
			RE::BGSLocation* lastLocation = nullptr;
			for (const auto ref : vec) {
				if (const auto currentDistance = ref->GetDistance(a_container) < lastDistance) {
					lastLocation = ref->GetCurrentLocation();
					lastDistance = currentDistance;
				}
			}
			return lastLocation;
		}
		return nullptr;
	}

	void ContainerManager::RegisterDistance(float a_newDistance)
	{
		if (a_newDistance < 10000.0f) {
			a_newDistance = 10000.0f;
		}
		else if (a_newDistance > 150000.0f) {
			a_newDistance = 150000.0f;
		}

		maxLookupDistance = a_newDistance;
	}

	void ContainerManager::RegisterRule(
		Json::Value& raw,
		std::vector<size_t> a_conditions,
		bool a_safe,
		bool a_vendors,
		bool a_onlyVendors,
		bool a_random,
		const std::string& a_configPath,
		const std::string& a_friendlyName)
	{
		//json is valid here, checked in Settings::JSON::Read()
		const auto& add = raw["add"];
		const auto& remove = raw["remove"];
		const auto& removeKeywordsField = raw["removeByKeywords"];
		const auto& count = raw["count"];

		if (add && removeKeywordsField) {
			ReplaceKeywordRule newRule{};
			newRule.conditions = a_conditions;
			newRule.configPath = a_configPath;
			newRule.friendlyName = a_friendlyName;
			newRule.allowSafeBypass = a_safe;
			newRule.allowVendors = a_vendors;
			newRule.onlyVendors = a_onlyVendors;
			newRule.randomAdd = a_random;
			for (const auto& entry : add) {
				const auto obj = Utilities::Forms::GetFormFromString<RE::TESBoundObject>(entry.asString());
				newRule.newForms.push_back(obj);
			}
			for (const auto& entry : removeKeywordsField) {
				const auto keyword = Utilities::Forms::GetFormFromString<RE::BGSKeyword>(entry.asString());
				newRule.keywordsToRemove.push_back(keyword);
			}
			replaceKeywords.push_back(std::move(newRule));
		}
		else if (add && remove) {
			ReplaceRule newRule{};
			newRule.conditions = a_conditions;
			newRule.configPath = a_configPath;
			newRule.friendlyName = a_friendlyName;
			newRule.allowSafeBypass = a_safe;
			newRule.allowVendors = a_vendors;
			newRule.onlyVendors = a_onlyVendors;
			newRule.randomAdd = a_random;
			for (const auto& entry : add) {
				const auto obj = Utilities::Forms::GetFormFromString<RE::TESBoundObject>(entry.asString());
				newRule.newForms.push_back(obj);
			}
			newRule.oldForm = Utilities::Forms::GetFormFromString<RE::TESBoundObject>(remove.asString());
			replaces.push_back(std::move(newRule));
		}
		else if (removeKeywordsField) {
			RemoveKeywordRule newRule{};
			newRule.conditions = a_conditions;
			newRule.configPath = a_configPath;
			newRule.friendlyName = a_friendlyName;
			newRule.allowSafeBypass = a_safe;
			newRule.allowVendors = a_vendors;
			newRule.onlyVendors = a_onlyVendors;
			newRule.randomAdd = a_random;
			for (const auto& entry : removeKeywordsField) {
				const auto keyword = Utilities::Forms::GetFormFromString<RE::BGSKeyword>(entry.asString());
				newRule.keywordsToRemove.push_back(keyword);
			}
			removeKeywords.push_back(std::move(newRule));
		}
		else if (remove) {
			RemoveRule newRule{};
			newRule.conditions = a_conditions;
			newRule.configPath = a_configPath;
			newRule.friendlyName = a_friendlyName;
			newRule.allowSafeBypass = a_safe;
			newRule.allowVendors = a_vendors;
			newRule.onlyVendors = a_onlyVendors;
			newRule.randomAdd = a_random;
			newRule.form = Utilities::Forms::GetFormFromString<RE::TESBoundObject>(remove.asString());

			if (count) {
				newRule.ruleCount = count.asUInt();
			}
			else {
				newRule.ruleCount = 0;
			}
			removes.push_back(std::move(newRule));
		}
		else if (add) {
			AddRule newRule{};
			newRule.conditions = a_conditions;
			newRule.configPath = a_configPath;
			newRule.friendlyName = a_friendlyName;
			newRule.allowSafeBypass = a_safe;
			newRule.allowVendors = a_vendors;
			newRule.onlyVendors = a_onlyVendors;
			newRule.randomAdd = a_random;
			for (const auto& entry : add) {
				const auto obj = Utilities::Forms::GetFormFromString<RE::TESBoundObject>(entry.asString());
				newRule.newForms.push_back(obj);
			}

			if (count) {
				newRule.ruleCount = count.asUInt();
			}
			else {
				newRule.ruleCount = 1;
			}
			adds.push_back(std::move(newRule));
		}
	}

	void ContainerManager::WarmCache()
	{
		auto& worldspaceArray = RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESWorldSpace>();
		for (auto* worldspace : worldspaceArray) {
			auto* persistentCell = worldspace->persistentCell;
			if (!persistentCell) continue;
			std::vector<RE::TESObjectREFR*> references{};

			persistentCell->ForEachReference([&](RE::TESObjectREFR* a_marker) {
				auto* markerLoc = a_marker->GetCurrentLocation();
				if (!markerLoc) return RE::BSContainer::ForEachResult::kContinue;
				if (!a_marker->extraList.GetByType(RE::ExtraDataType::kMapMarker)) return RE::BSContainer::ForEachResult::kContinue;
				references.push_back(a_marker);
				return RE::BSContainer::ForEachResult::kContinue;
				});

			this->worldspaceMarkers[worldspace] = references;
		}
	}

	void ContainerManager::PrettyPrint()
	{
		logger::info("=================================================");
		logger::info("Finished reading settings. Information to follow:");
		logger::info("=================================================");
		if (!adds.empty()) {
			logger::info("New add rules:");
			for (auto& rule : adds) {
				rule.Print();
				logger::info("=================================");
			}
		}
		if (!removes.empty()) {
			logger::info("New remove rules:");
			for (auto& rule : removes) {
				rule.Print();
				logger::info("=================================");
			}
		}
		if (!replaces.empty()) {
			logger::info("New replace rules:");
			for (auto& rule : replaces) {
				rule.Print();
				logger::info("=================================");
			}
		}
		if (!replaceKeywords.empty()) {
			logger::info("New replace by keyword rules:");
			for (auto& rule : replaceKeywords) {
				rule.Print();
				logger::info("=================================");
			}
		}
	}

	void ContainerManager::Initialize(RE::TESObjectREFR* a_container, bool a3)
	{
		_initialize(a_container, a3);
		if (a_container && a_container->GetBaseObject() && a_container->GetBaseObject()->As<RE::TESObjectCONT>()) {
			ContainerManager::GetSingleton()->ProcessContainer(a_container);
		}
	}

	void ContainerManager::Reset(RE::TESObjectREFR* a_container, bool a3)
	{
		_reset(a_container, a3);
		if (a_container && a_container->GetBaseObject() && a_container->GetBaseObject()->As<RE::TESObjectCONT>()) {
			ContainerManager::GetSingleton()->ProcessContainer(a_container);
		}
	}

	void ContainerManager::ProcessContainer(RE::TESObjectREFR* a_container)
	{
#ifdef DEBUG
		const auto then = std::chrono::high_resolution_clock::now();
#endif
		for (auto& add : adds) {
			add.Apply(a_container);
		}
		for (auto& remove : removes) {
			remove.Apply(a_container);
		}
		for (auto& remove : removeKeywords) {
			remove.Apply(a_container);
		}
		for (auto& replace : replaces) {
			replace.Apply(a_container);
		}
		for (auto& replace : replaceKeywords) {
			replace.Apply(a_container);
		}
#ifdef DEBUG
		const auto now = std::chrono::high_resolution_clock::now();
		const auto timespan = now - then;
		const auto durationSpan = timespan.count();
		if (durationSpan > 10000) {
			logger::debug("Processed {} in {}ns", Utilities::EDID::GetEditorID(a_container->GetBaseObject()), durationSpan);
		}
#endif
	}

	void ContainerManager::AddRule::Apply(RE::TESObjectREFR* a_container)
	{		
		if (!PreCheck(a_container)) {
			return;
		}

		const auto count = ruleCount;
		const auto logContext = RuleLogContext{ configPath, friendlyName, a_container, nullptr };
		if (randomAdd) {
			size_t upper = newForms.size() - 1;
			for (auto i = (size_t)0; i < count; ++i) {
				const auto index = clib_util::RNG().generate((size_t)0, upper);
				const auto obj = newForms.at(index);
				if (const auto leveledList = obj->As<RE::TESLeveledList>()) {
					AddLeveledListToContainer(leveledList, a_container, 1, logContext, obj);
				}
				else {
					a_container->AddObjectToContainer(newForms.at(index), nullptr, 1, nullptr);
				}
			}
		}
		else {
			for (const auto baseObj : newForms) {
				if (const auto leveledList = baseObj->As<RE::TESLeveledList>()) {
					AddLeveledListToContainer(leveledList, a_container, count, logContext, baseObj);
				}
				else {
					AddObjectCountToContainer(baseObj, a_container, count);
				}
			}
		}
	}

	void ContainerManager::AddRule::Print()
	{
		PrintContext();
		const auto singleton = ContainerManager::GetSingleton();
		if (!conditions.empty()) {
			logger::info("Conditions:");
			for (const auto condition : conditions) {
				singleton->storedConditions.at(condition)->Print();
			}
		}
		logger::info("----------------------------");
		logger::info("Count: {}", ruleCount);
		logger::info("Forms:");
		for (const auto& form : newForms) {
			logger::info("  ->{}", form->GetName());
		}
	}

	void ContainerManager::RemoveRule::Apply(RE::TESObjectREFR* a_container)
	{
		auto inventory = a_container->GetInventory();
		if (!inventory.contains(form)) {
			return;
		}
		if (!PreCheck(a_container)) {
			return;
		}

		const auto availableCount = inventory.at(form).first;
		if (!CountSafety::IsPositiveInventoryCount(availableCount)) {
			LogSkippedNonPositiveCount(a_container, form, availableCount);
			return;
		}

		const auto availableCountUnsigned = static_cast<std::uint32_t>(availableCount);
		const auto countToRemove = ruleCount == 0 || ruleCount > availableCountUnsigned ?
			availableCount :
			static_cast<std::int32_t>(ruleCount);
		a_container->RemoveItem(form, countToRemove, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
	}

	void ContainerManager::RemoveRule::Print()
	{
		PrintContext();
		const auto singleton = ContainerManager::GetSingleton();
		if (!conditions.empty()) {
			logger::info("Conditions:");
			for (const auto condition : conditions) {
				singleton->storedConditions.at(condition)->Print();
			}
		}
		logger::info("----------------------------");
		logger::info("Count: {}", ruleCount == 0 ? "All" : std::to_string(ruleCount));
		logger::info("Form: {}", form->GetName());
	}

	void ContainerManager::ReplaceRule::Apply(RE::TESObjectREFR* a_container)
	{
		auto inventory = a_container->GetInventory();
		if (!inventory.contains(oldForm)) {
			return;
		}
		if (!PreCheck(a_container)) {
			return;
		}
		const auto count = inventory.at(oldForm).first;
		if (!CountSafety::IsPositiveInventoryCount(count)) {
			LogSkippedNonPositiveCount(a_container, oldForm, count);
			return;
		}

		const auto logContext = RuleLogContext{ configPath, friendlyName, a_container, oldForm };
		a_container->RemoveItem(oldForm, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		if (randomAdd) {
			size_t upper = newForms.size() - 1;
			const auto positiveCount = static_cast<std::size_t>(count);
			for (auto i = (size_t)0; i < positiveCount; ++i) {
				const auto index = clib_util::RNG().generate((size_t)0, upper);
				const auto obj = newForms.at(index);
				if (const auto leveledList = obj->As<RE::TESLeveledList>()) {
					AddLeveledListToContainer(leveledList, a_container, 1, logContext, obj);
				}
				else {
					a_container->AddObjectToContainer(newForms.at(index), nullptr, 1, nullptr);
				}
			}
		}
		else {
			for (const auto baseObj : newForms) {
				if (const auto leveledList = baseObj->As<RE::TESLeveledList>()) {
					AddLeveledListToContainer(leveledList, a_container, count, logContext, baseObj);
				}
				else {
					a_container->AddObjectToContainer(baseObj, nullptr, count, nullptr);
				}
			}
		}
	}

	void ContainerManager::ReplaceRule::Print()
	{
		PrintContext();
		const auto singleton = ContainerManager::GetSingleton();
		if (!conditions.empty()) {
			logger::info("Conditions:");
			for (const auto condition : conditions) {
				singleton->storedConditions.at(condition)->Print();
			}
		}
		logger::info("----------------------------");
		logger::info("Form to remove: {}", oldForm->GetName());
		logger::info("Replaced by:");
		for (const auto& form : newForms) {
			logger::info("  ->{}", form->GetName());
		}
	}

	void ContainerManager::Rule::LogSkippedNonPositiveCount(
		RE::TESObjectREFR* a_container,
		RE::TESForm* a_source,
		std::int32_t a_count) const
	{
		const auto containerID = GetFormID(a_container);
		const auto baseID = GetFormID(a_container ? a_container->GetBaseObject() : nullptr);
		const auto sourceID = GetFormID(a_source);
		if (a_count < 0) {
			logger::warn(
				"Config <{}>/[{}] skipped nonpositive inventory count {} "
				"(container 0x{:08X}, base 0x{:08X}, source 0x{:08X})",
				configPath,
				friendlyName,
				a_count,
				containerID,
				baseID,
				sourceID);
		} else {
			logger::debug(
				"Config <{}>/[{}] skipped zero inventory count "
				"(container 0x{:08X}, base 0x{:08X}, source 0x{:08X})",
				configPath,
				friendlyName,
				containerID,
				baseID,
				sourceID);
		}
	}

	void ContainerManager::Rule::PrintContext() const
	{
		logger::info("Config <{}>/[{}]", configPath, friendlyName);
	}

	bool ContainerManager::Rule::PreCheck(RE::TESObjectREFR* a_container)
	{
		const auto owner = a_container->GetFactionOwner();
		auto isMerchant = owner ? owner->IsVendor() : false;
		if (!isMerchant) {
			isMerchant = MerchantCache::MerchantCache::GetSingleton()->IsMerchantContainer(a_container);
		}
		if (isMerchant && !allowVendors) {
			return false;
		}
		if (!isMerchant && onlyVendors) {
			return false;
		}

		const auto containerBase = a_container->GetBaseObject()->As<RE::TESObjectCONT>();
		bool isSafeContainer = false;
		if (containerBase && !(containerBase->data.flags & RE::CONT_DATA::Flag::kRespawn)) {
			isSafeContainer = true;
		}

		bool hasParentCell = a_container->parentCell ? true : false;
		auto* parentEncounterZone = hasParentCell ? a_container->parentCell->extraList.GetEncounterZone() : nullptr;
		if (parentEncounterZone && parentEncounterZone->data.flags & RE::ENCOUNTER_ZONE_DATA::Flag::kNeverResets) {
			isSafeContainer = true;
		}

		if (isSafeContainer && !allowSafeBypass) {
			return false;
		}

		for (auto& condition : conditions) {
			if (!ContainerManager::GetSingleton()->storedConditions.at(condition)->IsValid(a_container)) {
				return false;
			}
		}

		return true;
	}

	void ContainerManager::RemoveKeywordRule::Apply(RE::TESObjectREFR* a_container)
	{
		auto inventory = a_container->GetInventory();
		if (inventory.empty()) {
			return;
		}

		//This is a awkward one. Results are mixed on whether it is faster to iterate through the inventory
		//first or the rules. When I get around to the rule matching algorithm, rules should be faster on
		//average.
		std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> removals{};
		std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> skippedCounts{};
		for (const auto& inventoryEntry : inventory) {
			bool shouldSkip = false;
			for (auto it = keywordsToRemove.begin(); !shouldSkip && it != keywordsToRemove.end(); ++it) {
				const auto keywordForm = inventoryEntry.first->As<RE::BGSKeywordForm>();
				if (!keywordForm || !keywordForm->HasKeyword(*it)) {
					shouldSkip = true;
				}
			}
			if (shouldSkip) {
				continue;
			}

			const auto entryCount = inventoryEntry.second.first;
			if (!CountSafety::IsPositiveInventoryCount(entryCount)) {
				skippedCounts.push_back({ inventoryEntry.first, entryCount });
				continue;
			}

			removals.push_back({ inventoryEntry.first, entryCount });
		}
		if (removals.empty() && skippedCounts.empty()) {
			return;
		}
		if (!PreCheck(a_container)) {
			return;
		}
		for (const auto& pair : skippedCounts) {
			LogSkippedNonPositiveCount(a_container, pair.first, pair.second);
		}

		for (const auto& pair : removals) {
			a_container->RemoveItem(pair.first, pair.second, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		}
	}

	void ContainerManager::RemoveKeywordRule::Print()
	{
		PrintContext();
		const auto singleton = ContainerManager::GetSingleton();
		if (!conditions.empty()) {
			logger::info("Conditions:");
			for (const auto condition : conditions) {
				singleton->storedConditions.at(condition)->Print();
			}
		}
		logger::info("----------------------------");
		logger::info("If an item has all of these keywords, it will be removed:");
		for (const auto keyword : keywordsToRemove) {
			logger::info("  ->{}", keyword->GetFormEditorID());
		}
	}

	void ContainerManager::ReplaceKeywordRule::Apply(RE::TESObjectREFR* a_container)
	{
		auto inventory = a_container->GetInventory();
		if (inventory.empty()) {
			return;
		}

		auto count = CountSafety::InventoryCountTotal{ 0, false };
		std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> removals{};
		std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> skippedCounts{};
		for (const auto& inventoryEntry : inventory) {
			bool shouldSkip = false;
			for (auto it = keywordsToRemove.begin(); !shouldSkip && it != keywordsToRemove.end(); ++it) {
				const auto keywordForm = inventoryEntry.first->As<RE::BGSKeywordForm>();
				if (!keywordForm || !keywordForm->HasKeyword(*it)) {
					shouldSkip = true;
				}
			}
			if (shouldSkip) {
				continue;
			}

			const auto entryCount = inventoryEntry.second.first;
			if (!CountSafety::IsPositiveInventoryCount(entryCount)) {
				skippedCounts.push_back({ inventoryEntry.first, entryCount });
				continue;
			}

			removals.push_back({ inventoryEntry.first, entryCount });
			count = CountSafety::AddPositiveInventoryCount(count, entryCount);
		}
		if (removals.empty() && skippedCounts.empty()) {
			return;
		}
		if (!PreCheck(a_container)) {
			return;
		}
		for (const auto& pair : skippedCounts) {
			LogSkippedNonPositiveCount(a_container, pair.first, pair.second);
		}
		if (removals.empty()) {
			return;
		}
		if (count.saturated) {
			logger::warn(
				"Config <{}>/[{}] positive keyword-replacement total exceeded {}; saturating "
				"(container 0x{:08X}, base 0x{:08X})",
				configPath,
				friendlyName,
				std::numeric_limits<std::uint32_t>::max(),
				GetFormID(a_container),
				GetFormID(a_container->GetBaseObject()));
		}

		for (const auto& pair : removals) {
			a_container->RemoveItem(pair.first, pair.second, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		}
		const auto logContext = RuleLogContext{ configPath, friendlyName, a_container, nullptr };
		if (randomAdd) {
			size_t upper = newForms.size() - 1;
			const auto positiveCount = static_cast<std::size_t>(count.value);
			for (auto i = (size_t)0; i < positiveCount; ++i) {
				const auto index = clib_util::RNG().generate((size_t)0, upper);
				const auto obj = newForms.at(index);
				if (const auto leveledList = obj->As<RE::TESLeveledList>()) {
					AddLeveledListToContainer(leveledList, a_container, 1, logContext, obj);
				}
				else {
					a_container->AddObjectToContainer(newForms.at(index), nullptr, 1, nullptr);
				}
			}
		}
		else {
			for (const auto baseObj : newForms) {
				if (const auto leveledList = baseObj->As<RE::TESLeveledList>()) {
					AddLeveledListToContainer(leveledList, a_container, count.value, logContext, baseObj);
				}
				else {
					AddObjectCountToContainer(baseObj, a_container, count.value);
				}
			}
		}
	}

	void ContainerManager::ReplaceKeywordRule::Print()
	{
		PrintContext();
		const auto singleton = ContainerManager::GetSingleton();
		if (!conditions.empty()) {
			logger::info("Conditions:");
			for (const auto condition : conditions) {
				singleton->storedConditions.at(condition)->Print();
			}
		}
		logger::info("----------------------------");
		logger::info("If an item has all of these keywords, it will be removed:");
		for (const auto keyword : keywordsToRemove) {
			logger::info("  ->{}", keyword->GetFormEditorID());
		}
		logger::info("And replaced by:");
		for (const auto form : newForms) {
			logger::info("  ->{}", form->GetName());
		}
	}
}
