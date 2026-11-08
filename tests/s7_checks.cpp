#include "config/Settings.h"
#include "core/Core.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	int g_failures = 0;

	void Expect(bool a_condition, const char* a_expression, int a_line)
	{
		if (!a_condition) {
			std::cerr << "FAIL line " << a_line << ": " << a_expression << '\n';
			++g_failures;
		}
	}

	slacs::ChangeSet Add(
		slacs::CoreState& a_state,
		slacs::ActorId a_target,
		std::vector<slacs::Region> a_regions,
		const slacs::Settings& a_settings,
		slacs::GameTime a_now = {},
		slacs::TextureVariant a_variant = slacs::TextureVariant::Normal)
	{
		slacs::AddLayers op{};
		op.source = 100;
		op.tid = 7;
		op.sequence = 1;
		op.selectionSeed = 0x71A2;
		op.targets.push_back(slacs::InteractionTarget{ a_target, std::move(a_regions), a_variant });
		return slacs::Core::apply(a_state, slacs::CoreOp{ std::move(op) }, a_settings, a_now);
	}

	slacs::ChangeSet Advance(
		slacs::CoreState& a_state,
		slacs::ActorId a_actor,
		const slacs::Settings& a_settings,
		slacs::GameTime a_now,
		std::optional<slacs::EnvironmentSample> a_sample = std::nullopt)
	{
		slacs::Advance op{};
		op.actor = a_actor;
		op.now = a_now;
		op.environment = std::move(a_sample);
		return slacs::Core::apply(a_state, slacs::CoreOp{ std::move(op) }, a_settings, a_now);
	}

	slacs::EnvironmentSample Sample(
		slacs::ActorId a_actor,
		slacs::GameTime a_now,
		bool a_interior,
		bool a_raining,
		bool a_snowing,
		bool a_swimming,
		double a_observedRainHours)
	{
		slacs::EnvironmentSample sample{};
		sample.actor = a_actor;
		sample.interior = a_interior;
		sample.raining = a_raining;
		sample.snowing = a_snowing;
		sample.swimming = a_swimming;
		sample.observedRainHours = a_observedRainHours;
		sample.at = a_now;
		return sample;
	}

	std::size_t LayerCount(const slacs::CoreState& a_state, slacs::ActorId a_actor, slacs::Region a_region)
	{
		const auto found = a_state.actors.find(a_actor);
		if (found == a_state.actors.end()) {
			return 0;
		}
		return found->second.regions[slacs::RegionIndex(a_region)].layers.size();
	}

	const slacs::SpellTarget* FindSpell(const slacs::ChangeSet& a_change, slacs::ActorId a_actor)
	{
		const auto found = std::find_if(
			a_change.spells.begin(),
			a_change.spells.end(),
			[a_actor](const slacs::SpellTarget& a_target) { return a_target.actor == a_actor; });
		return found == a_change.spells.end() ? nullptr : &*found;
	}

	void CheckSevenRegionPools()
	{
		using namespace slacs;

		constexpr std::array<std::uint16_t, kRegionCount> kPools{ 12, 12, 12, 4, 12, 4, 4 };
		for (std::size_t index = 0; index < kRegionCount; ++index) {
			CoreState state{};
			const auto region = kRegions[index];
			auto change = Add(state, 200, { region }, Settings{});
			Expect(change.regions.size() == 1, "one region target per added region", __LINE__);
			const auto& layers = state.actors.at(200).regions[index].layers;
			Expect(layers.size() == 1, "one layer stored", __LINE__);
			if (!layers.empty()) {
				Expect(layers.front().textureIndex >= 1 && layers.front().textureIndex <= kPools[index],
					"texture index inside region pool", __LINE__);
			}
		}

		// Every region has its own five-layer logical capacity.
		CoreState full{};
		for (int i = 0; i < 6; ++i) {
			Add(full, 300, { Region::Face, Region::Hands, Region::Feet, Region::BodyAnal }, Settings{});
		}
		Expect(LayerCount(full, 300, Region::Face) == 5, "face cap", __LINE__);
		Expect(LayerCount(full, 300, Region::Hands) == 5, "hands cap", __LINE__);
		Expect(LayerCount(full, 300, Region::Feet) == 5, "feet cap", __LINE__);
		Expect(LayerCount(full, 300, Region::BodyAnal) == 5, "anal cap", __LINE__);

		// Mega uses the same logical pools; the texture index chooses m_ files
		// in the adapter without changing the Core state shape.
		CoreState mega{};
		Add(mega, 400, { Region::BodyVaginal, Region::BodyBreast, Region::BodyLegs }, Settings{}, {}, TextureVariant::Mega);
		Expect(mega.actors.at(400).regions[RegionIndex(Region::BodyVaginal)].layers.front().variant == TextureVariant::Mega,
			"mega variant stored", __LINE__);
	}

	void CheckSwimmingClearsAll()
	{
		using namespace slacs;

		Settings settings{};
		CoreState state{};
		Add(state, 200, { Region::BodyVaginal, Region::BodyAnal, Region::BodyBreast, Region::BodyLegs, Region::Face, Region::Hands, Region::Feet },
			settings, { 0.0, 0.0 });
		for (const auto region : kRegions) {
			Expect(LayerCount(state, 200, region) == 1, "all seven regions populated", __LINE__);
		}
		Expect(state.actors.at(200).timedClearAt.has_value(), "timed clear armed after add", __LINE__);

		const auto change = Advance(state, 200, settings, { 0.01, 10.0 }, Sample(200, { 0.01, 10.0 }, false, false, false, true, 0.0));
		for (const auto region : kRegions) {
			Expect(LayerCount(state, 200, region) == 0, "swimming clears every region", __LINE__);
		}
		Expect(!state.actors.at(200).timedClearAt.has_value(), "swimming clears timed-clear deadline", __LINE__);
		Expect(change.regions.size() == kRegionCount, "swimming reports seven visual targets", __LINE__);

		// Wading is IsSwimming()==false in the sampler; the Core contract is
		// solely the boolean sample value.
		Add(state, 200, { Region::BodyVaginal }, settings, { 0.02, 12.0 });
		Advance(state, 200, settings, { 0.02, 13.0 }, Sample(200, { 0.02, 13.0 }, false, true, false, false, 0.0));
		Expect(LayerCount(state, 200, Region::BodyVaginal) == 1, "not swimming keeps layers", __LINE__);
	}

	void CheckRainBudget()
	{
		using namespace slacs;

		Settings settings{};

		// Rain adds an equal extra budget on top of calendar time: half an
		// hour of observed outdoor rain plus half an hour of calendar time
		// consumes one full decay interval.
		CoreState state{};
		Add(state, 200, { Region::BodyVaginal }, settings, { 0.0, 0.0 });
		auto change = Advance(state, 200, settings, { 0.5, 0.0 }, Sample(200, { 0.5, 0.0 }, false, true, false, false, 0.5));
		Expect(LayerCount(state, 200, Region::BodyVaginal) == 0, "observed rain doubles the decay budget", __LINE__);
		Expect(FindSpell(change, 200) != nullptr && FindSpell(change, 200)->desired.combinationIndex == 0,
			"rain decay refreshes spell target to zero", __LINE__);

		// A sample is consumed once. Replaying the same timestamp must not
		// contribute its observed rain increment a second time.
		CoreState duplicate{};
		Add(duplicate, 200, { Region::BodyVaginal }, settings, { 0.0, 0.0 });
		Add(duplicate, 200, { Region::BodyVaginal }, settings, { 0.0, 0.0 });
		Add(duplicate, 200, { Region::BodyVaginal }, settings, { 0.0, 0.0 });
		const auto once = Sample(200, { 0.2, 0.0 }, false, true, false, false, 0.2);
		Advance(duplicate, 200, settings, { 0.2, 0.0 }, once);
		Advance(duplicate, 200, settings, { 0.2, 0.0 }, once);
		Expect(std::abs(duplicate.actors.at(200).regions[RegionIndex(Region::BodyVaginal)].decayRemainderGameHours - 0.4) < 1e-9,
			"a duplicate rain sample is not consumed twice", __LINE__);

		// Without rain the same half hour only banks a remainder.
		CoreState dry{};
		Add(dry, 200, { Region::BodyVaginal }, settings, { 0.0, 0.0 });
		Advance(dry, 200, settings, { 0.5, 0.0 });
		Expect(LayerCount(dry, 200, Region::BodyVaginal) == 1, "dry half hour keeps layer", __LINE__);
		Expect(std::abs(dry.actors.at(200).regions[RegionIndex(Region::BodyVaginal)].decayRemainderGameHours - 0.5) < 1e-9,
			"dry remainder accumulated", __LINE__);

		// Indoor rain contributes no rain budget; the sample may still carry
		// an observed value from a previous outdoor interval, which the Core
		// contract explicitly ignores.
		CoreState indoor{};
		Add(indoor, 200, { Region::BodyAnal }, settings, { 2.0, 0.0 });
		Advance(indoor, 200, settings, { 2.5, 0.0 }, Sample(200, { 2.5, 0.0 }, true, true, false, false, 0.5));
		Expect(LayerCount(indoor, 200, Region::BodyAnal) == 1, "indoor rain does not accelerate", __LINE__);
		Expect(std::abs(indoor.actors.at(200).regions[RegionIndex(Region::BodyAnal)].decayRemainderGameHours - 0.5) < 1e-9,
			"indoor rain banks only calendar time", __LINE__);

		// Snow is not rain: it banks calendar time only.
		CoreState snow{};
		Add(snow, 200, { Region::BodyBreast }, settings, { 0.0, 0.0 });
		Advance(snow, 200, settings, { 0.5, 0.0 }, Sample(200, { 0.5, 0.0 }, false, false, true, false, 0.5));
		Expect(LayerCount(snow, 200, Region::BodyBreast) == 1, "snow does not accelerate", __LINE__);
		Expect(std::abs(snow.actors.at(200).regions[RegionIndex(Region::BodyBreast)].decayRemainderGameHours - 0.5) < 1e-9,
			"snow banks only calendar time", __LINE__);

		// rainAccelerates=false ignores the same observed hours.
		auto noRain = settings;
		noRain.rainAccelerates = false;
		CoreState disabled{};
		Add(disabled, 200, { Region::BodyLegs }, noRain, { 0.0, 0.0 });
		Advance(disabled, 200, noRain, { 0.5, 0.0 }, Sample(200, { 0.5, 0.0 }, false, true, false, false, 0.5));
		Expect(LayerCount(disabled, 200, Region::BodyLegs) == 1, "rain switch off keeps layer", __LINE__);
		Expect(std::abs(disabled.actors.at(200).regions[RegionIndex(Region::BodyLegs)].decayRemainderGameHours - 0.5) < 1e-9,
			"rain switch off banks only calendar time", __LINE__);

		// naturalDecay=false stops both calendar and rain budgets and clears
		// banked remainders so re-enabling cannot replay elapsed time.
		auto noNatural = settings;
		noNatural.naturalDecay = false;
		CoreState frozen{};
		Add(frozen, 200, { Region::BodyVaginal }, noNatural, { 0.0, 0.0 });
		Advance(frozen, 200, noNatural, { 5.0, 0.0 }, Sample(200, { 5.0, 0.0 }, false, true, false, false, 5.0));
		Expect(LayerCount(frozen, 200, Region::BodyVaginal) == 1, "natural off stops rain decay", __LINE__);
		Expect(frozen.actors.at(200).regions[RegionIndex(Region::BodyVaginal)].decayRemainderGameHours == 0.0,
			"natural off clears remainder", __LINE__);
	}

	void CheckSpellCombinations()
	{
		using namespace slacs;

		Settings settings{};
		CoreState state{};

		// oral 1 -> 1
		Add(state, 200, { Region::Face }, settings, { 0.0, 0.0 });
		auto change = Advance(state, 200, settings, { 0.0, 0.0 });
		auto* spell = FindSpell(change, 200);
		Expect(spell && spell->desired.combinationIndex == 1 && spell->desired.enabled, "oral level 1 -> 1", __LINE__);

		// oral 3 -> 2. Each AddLayers result adds at most one layer per
		// actor and region, so accumulate across separate orgasm results.
		Add(state, 200, { Region::Face }, settings, { 0.0, 0.0 });
		Add(state, 200, { Region::Face }, settings, { 0.0, 0.0 });
		change = Advance(state, 200, settings, { 0.0, 0.0 });
		spell = FindSpell(change, 200);
		Expect(spell && spell->desired.combinationIndex == 2, "oral level 2 -> 2", __LINE__);

		// Add vaginal 1: anal*9 + vaginal*3 + oral = 3 + 2 = 5.
		Add(state, 200, { Region::BodyVaginal }, settings, { 0.0, 0.0 });
		change = Advance(state, 200, settings, { 0.0, 0.0 });
		spell = FindSpell(change, 200);
		Expect(spell && spell->desired.combinationIndex == 5, "oral 2 + vaginal 1 -> 5", __LINE__);

		// Add anal 1: 9 + 3 + 2 = 14.
		Add(state, 200, { Region::BodyAnal }, settings, { 0.0, 0.0 });
		change = Advance(state, 200, settings, { 0.0, 0.0 });
		spell = FindSpell(change, 200);
		Expect(spell && spell->desired.combinationIndex == 14, "anal 1 + vaginal 1 + oral 2 -> 14", __LINE__);

		// Breast, legs, hands and feet never contribute to the 26-spell index.
		Add(state, 200, { Region::BodyBreast, Region::BodyLegs, Region::Hands, Region::Feet }, settings, { 0.0, 0.0 });
		change = Advance(state, 200, settings, { 0.0, 0.0 });
		spell = FindSpell(change, 200);
		Expect(spell && spell->desired.combinationIndex == 14, "non-oral regions do not shift combination", __LINE__);

		// anal 2 + vaginal 2 + oral 2 = 18 + 6 + 2 = 26. Level 2 starts at
		// three layers, so each participating region needs three layers.
		for (int i = 0; i < 2; ++i) {
			Add(state, 200, { Region::BodyAnal }, settings, { 0.0, 0.0 });
			Add(state, 200, { Region::BodyVaginal }, settings, { 0.0, 0.0 });
		}
		change = Advance(state, 200, settings, { 0.0, 0.0 });
		spell = FindSpell(change, 200);
		Expect(spell && spell->desired.combinationIndex == 26, "max combination is 26", __LINE__);

		// Exhaustively verify every one of the 27 combination indices from the
		// 0/1/2 levels. Three layers is level 2 under the default 1/3
		// thresholds.
		constexpr std::array<std::size_t, 3> kLevelLayers{ 0, 1, 3 };
		for (std::size_t oral = 0; oral < 3; ++oral) {
			for (std::size_t vaginal = 0; vaginal < 3; ++vaginal) {
				for (std::size_t anal = 0; anal < 3; ++anal) {
					CoreState fixed{};
					ActorState actor{};
					actor.actor = 200;
					actor.formId = 0x1;
					actor.lastSettledGameHours = 0.0;
					std::uint64_t nextId = 1;
					const auto fill = [&](Region a_region, std::size_t a_count) {
						auto& region = actor.regions[RegionIndex(a_region)];
						for (std::size_t i = 0; i < a_count; ++i) {
							region.layers.push_back(LayerRecord{ nextId++, static_cast<std::uint16_t>(i + 1), TextureVariant::Normal });
						}
					};
					fill(Region::Face, kLevelLayers[oral]);
					fill(Region::BodyVaginal, kLevelLayers[vaginal]);
					fill(Region::BodyAnal, kLevelLayers[anal]);
					fixed.actors.emplace(200, actor);
					const auto fixedChange = Advance(fixed, 200, settings, { 0.0, 0.0 });
					const auto* fixedSpell = FindSpell(fixedChange, 200);
					const auto expected = static_cast<std::uint8_t>(anal * 9 + vaginal * 3 + oral);
					Expect(fixedSpell && fixedSpell->desired.combinationIndex == expected,
						"all 27 combination indices", __LINE__);
				}
			}
		}

		// Disabling effects keeps the index but removes the spell at the adapter.
		auto off = settings;
		off.effectsEnabled = false;
		change = Advance(state, 200, off, { 0.0, 0.0 });
		spell = FindSpell(change, 200);
		Expect(spell && spell->desired.combinationIndex == 26 && !spell->desired.enabled, "effects off disables desired spell", __LINE__);

		// Custom thresholds: 1..2 -> level 0, 3..4 -> level 1, 5 -> level 2.
		auto custom = settings;
		custom.spellLevel1Threshold = 3;
		custom.spellLevel2Threshold = 5;
		CoreState thresholds{};
		Add(thresholds, 200, { Region::Face }, custom, { 0.0, 0.0 });
		Add(thresholds, 200, { Region::Face }, custom, { 0.0, 0.0 });
		change = Advance(thresholds, 200, custom, { 0.0, 0.0 });
		spell = FindSpell(change, 200);
		Expect(spell && spell->desired.combinationIndex == 0, "below custom threshold is level 0", __LINE__);
		Add(thresholds, 200, { Region::Face }, custom, { 0.0, 0.0 });
		change = Advance(thresholds, 200, custom, { 0.0, 0.0 });
		spell = FindSpell(change, 200);
		Expect(spell && spell->desired.combinationIndex == 1, "custom level 1 threshold", __LINE__);
		Add(thresholds, 200, { Region::Face }, custom, { 0.0, 0.0 });
		Add(thresholds, 200, { Region::Face }, custom, { 0.0, 0.0 });
		change = Advance(thresholds, 200, custom, { 0.0, 0.0 });
		spell = FindSpell(change, 200);
		Expect(spell && spell->desired.combinationIndex == 2, "custom level 2 threshold", __LINE__);

		// Clearing all relevant regions forces combination 0.
		ClearRegions op{};
		op.actor = 200;
		op.regions = { Region::Face, Region::BodyVaginal, Region::BodyAnal };
		change = Core::apply(thresholds, CoreOp{ std::move(op) }, custom, { 0.0, 0.0 });
		spell = FindSpell(change, 200);
		Expect(spell && spell->desired.combinationIndex == 0, "clear resets combination", __LINE__);
	}

	void CheckOverlaySlots()
	{
		using namespace slacs;

		Settings settings{};
		const auto vaginal = OverlaySlotsForRegion(settings, Region::BodyVaginal);
		const auto anal = OverlaySlotsForRegion(settings, Region::BodyAnal);
		const auto breast = OverlaySlotsForRegion(settings, Region::BodyBreast);
		const auto legs = OverlaySlotsForRegion(settings, Region::BodyLegs);
		const auto face = OverlaySlotsForRegion(settings, Region::Face);
		const auto hands = OverlaySlotsForRegion(settings, Region::Hands);
		const auto feet = OverlaySlotsForRegion(settings, Region::Feet);

		Expect(vaginal.firstSlot == 7 && vaginal.slotCount == 6, "vaginal 7-12", __LINE__);
		Expect(anal.firstSlot == 13 && anal.slotCount == 6, "anal 13-18", __LINE__);
		Expect(breast.firstSlot == 19 && breast.slotCount == 6, "breast 19-24", __LINE__);
		Expect(legs.firstSlot == 25 && legs.slotCount == 2, "legs 25-26", __LINE__);
		Expect(face.firstSlot == 7 && face.slotCount == 6, "face 7-12", __LINE__);
		Expect(hands.firstSlot == 7 && hands.slotCount == 2, "hands 7-8", __LINE__);
		Expect(feet.firstSlot == 7 && feet.slotCount == 2, "feet 7-8", __LINE__);

		// Visible capacity is independent of the five-layer logical capacity.
		Expect(vaginal.slotCount == 6 && legs.slotCount == 2 && hands.slotCount == 2 && feet.slotCount == 2,
			"fixed visible capacities", __LINE__);

		auto valid = Settings{};
		Expect(Settings::validate(valid).valid, "default slot layout valid", __LINE__);
		valid.firstOwnedSlot = { 8, 8, 8, 8 };
		Expect(Settings::validate(valid).valid, "shifted non-overlapping layout valid", __LINE__);
		auto tooLate = Settings{};
		tooLate.firstOwnedSlot[0] = 232;
		Expect(!Settings::validate(tooLate).valid, "body range past slot 250 rejected", __LINE__);
		auto faceTooLate = Settings{};
		faceTooLate.firstOwnedSlot[1] = 246;
		Expect(!Settings::validate(faceTooLate).valid, "face range past slot 250 rejected", __LINE__);
		auto handsTooLate = Settings{};
		handsTooLate.firstOwnedSlot[2] = 250;
		Expect(!Settings::validate(handsTooLate).valid, "hands range past slot 250 rejected", __LINE__);
		auto feetTooLate = Settings{};
		feetTooLate.firstOwnedSlot[3] = 250;
		Expect(!Settings::validate(feetTooLate).valid, "feet range past slot 250 rejected", __LINE__);
		constexpr std::array<unsigned, 4> lastValidStart{ 107, 121, 125, 125 };
		for (std::size_t domain = 0; domain < lastValidStart.size(); ++domain) {
			for (unsigned start = 0; start <= 255; ++start) {
				auto candidate = Settings{};
				candidate.firstOwnedSlot[domain] = static_cast<std::uint8_t>(start);
				Expect(Settings::validate(candidate).valid == (start <= lastValidStart[domain]),
					"slot validation respects RaceMenu's 127-slot limit for every byte value", __LINE__);
			}
		}

		// The body domain is the only one whose four profiles share a node
		// namespace; adjacency must remain exact after shifting the base.
		auto shifted = Settings{};
		shifted.firstOwnedSlot[0] = 20;
		const auto shiftedVaginal = OverlaySlotsForRegion(shifted, Region::BodyVaginal);
		const auto shiftedAnal = OverlaySlotsForRegion(shifted, Region::BodyAnal);
		const auto shiftedBreast = OverlaySlotsForRegion(shifted, Region::BodyBreast);
		const auto shiftedLegs = OverlaySlotsForRegion(shifted, Region::BodyLegs);
		Expect(shiftedVaginal.firstSlot == 20 && shiftedAnal.firstSlot == 26 &&
			shiftedBreast.firstSlot == 32 && shiftedLegs.firstSlot == 38,
			"body sub-ranges keep +0/+6/+12/+18 offsets", __LINE__);
	}
}

int main()
{
	CheckSevenRegionPools();
	CheckSwimmingClearsAll();
	CheckRainBudget();
	CheckSpellCombinations();
	CheckOverlaySlots();

	if (g_failures == 0) {
		std::cout << "SLACS2 NG S7 checks passed\n";
		return 0;
	}
	return 1;
}
