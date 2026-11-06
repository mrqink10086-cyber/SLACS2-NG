#include "config/Settings.h"
#include "core/Core.h"

#include <algorithm>
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

	const slacs::RegionTarget* FindTarget(const slacs::ChangeSet& a_change, slacs::ActorId a_actor, slacs::Region a_region)
	{
		const auto found = std::find_if(
			a_change.regions.begin(),
			a_change.regions.end(),
			[a_actor, a_region](const slacs::RegionTarget& a_target) {
				return a_target.actor == a_actor && a_target.region == a_region;
			});
		return found == a_change.regions.end() ? nullptr : &*found;
	}

	slacs::ChangeSet Add(slacs::CoreState& a_state, slacs::ActorId a_source, slacs::ActorId a_target, std::vector<slacs::Region> a_regions, std::uint64_t a_seed, const slacs::Settings& a_settings)
	{
		slacs::AddLayers op{};
		op.source = a_source;
		op.tid = 7;
		op.sequence = 1;
		op.selectionSeed = a_seed;
		op.targets.push_back(slacs::InteractionTarget{ a_target, std::move(a_regions), slacs::TextureVariant::Normal });
		return slacs::Core::apply(a_state, slacs::CoreOp{ std::move(op) }, a_settings, slacs::GameTime{});
	}
}

int main()
{
	using namespace slacs;

	const Settings defaults{};
	CoreState state{};

	// Five-layer cap, deterministic ids, one diagnostic per ignored layer.
	for (int i = 0; i < 6; ++i) {
		const auto change = Add(state, 100, 200, { Region::BodyVaginal }, 0x1234, defaults);
		if (i < 5) {
			Expect(change.diagnostics.empty(), "no diagnostic before cap", __LINE__);
		} else {
			Expect(change.diagnostics.size() == 1, "capacity diagnostic at six", __LINE__);
			Expect(change.diagnostics.front().code == CoreDiagnosticCode::AtCapacity, "AtCapacity code", __LINE__);
		}
	}
	Expect(state.actors.at(200).regions[RegionIndex(Region::BodyVaginal)].layers.size() == 5, "five stored layers", __LINE__);
	for (std::size_t i = 0; i < 5; ++i) {
		Expect(state.actors.at(200).regions[RegionIndex(Region::BodyVaginal)].layers[i].id == i + 1, "sequential layer id", __LINE__);
	}

	// Same notification cannot request two layers for the same actor/region.
	{
		CoreState duplicate{};
		AddLayers op{};
		op.source = 100;
		op.selectionSeed = 9;
		op.targets.push_back(InteractionTarget{ 200, { Region::BodyVaginal, Region::BodyVaginal }, TextureVariant::Normal });
		const auto change = Core::apply(duplicate, CoreOp{ std::move(op) }, defaults, GameTime{});
		Expect(duplicate.actors.at(200).regions[RegionIndex(Region::BodyVaginal)].layers.size() == 1, "duplicate region collapsed", __LINE__);
		Expect(change.regions.size() == 1, "one changed region", __LINE__);
	}

	// Self orgasm never adds a layer.
	{
		CoreState self{};
		const auto change = Add(self, 200, 200, { Region::BodyVaginal }, 1, defaults);
		Expect(self.actors.empty(), "self target creates no actor", __LINE__);
		Expect(change.regions.empty(), "self target creates no change", __LINE__);
	}

	// Selection seed is stable and inside the region pool.
	{
		CoreState first{};
		CoreState second{};
		Add(first, 100, 200, { Region::BodyVaginal }, 0xABCDEF, defaults);
		Add(second, 100, 200, { Region::BodyVaginal }, 0xABCDEF, defaults);
		const auto firstIndex = first.actors.at(200).regions[RegionIndex(Region::BodyVaginal)].layers.front().textureIndex;
		const auto secondIndex = second.actors.at(200).regions[RegionIndex(Region::BodyVaginal)].layers.front().textureIndex;
		Expect(firstIndex == secondIndex, "same seed same texture", __LINE__);
		Expect(firstIndex >= 1 && firstIndex <= 12, "vaginal pool 1..12", __LINE__);

		CoreState legs{};
		Add(legs, 100, 200, { Region::BodyLegs }, 0xABCDEF, defaults);
		const auto legsIndex = legs.actors.at(200).regions[RegionIndex(Region::BodyLegs)].layers.front().textureIndex;
		Expect(legsIndex >= 1 && legsIndex <= 4, "legs pool 1..4", __LINE__);
	}

	// Clear is idempotent and reports the revision of the first clear.
	{
		const auto revisionBefore = state.actors.at(200).regions[RegionIndex(Region::BodyVaginal)].revision;
		const auto first = Core::apply(state, CoreOp{ ClearRegions{ 200, { Region::BodyVaginal } } }, defaults, GameTime{});
		const auto revisionAfter = state.actors.at(200).regions[RegionIndex(Region::BodyVaginal)].revision;
		Expect(first.regions.size() == 1, "clear emits target", __LINE__);
		Expect(first.regions.front().layers.empty(), "clear target empty", __LINE__);
		Expect(revisionAfter != revisionBefore, "clear bump revision", __LINE__);
		const auto second = Core::apply(state, CoreOp{ ClearRegions{ 200, { Region::BodyVaginal } } }, defaults, GameTime{});
		Expect(second.regions.size() == 1, "repeat clear still emits", __LINE__);
		Expect(state.actors.at(200).regions[RegionIndex(Region::BodyVaginal)].revision == revisionAfter, "repeat clear idempotent", __LINE__);
		Expect(second.regions.front().revision == revisionAfter, "repeat clear same revision", __LINE__);
	}

	// SettingsChanged truncates the newest layers (LIFO drop order).
	{
		CoreState shrink{};
		for (int i = 0; i < 5; ++i) {
			Add(shrink, 100, 200, { Region::BodyVaginal }, static_cast<std::uint64_t>(i), defaults);
		}
		Settings two = defaults;
		two.maxLayersPerRegion = 2;
		const auto change = Core::apply(shrink, CoreOp{ SettingsChanged{} }, two, GameTime{});
		const auto& layers = shrink.actors.at(200).regions[RegionIndex(Region::BodyVaginal)].layers;
		Expect(layers.size() == 2, "settings truncate to two", __LINE__);
		Expect(layers[0].id == 1 && layers[1].id == 2, "oldest layers kept", __LINE__);
		Expect(change.regions.size() == kRegionCount, "settings emits all regions", __LINE__);
	}

	// replaceState clamps loaded layers and restores nextLayerId.
	{
		CoreState loaded{};
		ActorState actor{};
		actor.actor = 300;
		actor.formId = 0x12345;
		auto& region = actor.regions[RegionIndex(Region::BodyVaginal)];
		for (std::uint64_t i = 0; i < 7; ++i) {
			region.layers.push_back(LayerRecord{ 10 + i, static_cast<std::uint16_t>(i + 1), TextureVariant::Normal });
		}
		region.revision = 4;
		const auto change = Core::replaceState(loaded, { actor }, defaults, GameTime{});
		Expect(loaded.actors.at(300).regions[RegionIndex(Region::BodyVaginal)].layers.size() == 5, "replace clamps to five", __LINE__);
		Expect(loaded.nextLayerId == 15, "nextLayerId after accepted layers", __LINE__);
		Expect(change.regions.size() == kRegionCount, "replace emits all regions", __LINE__);
		Expect(loaded.actors.at(300).formId == 0x12345, "form id preserved", __LINE__);
	}

	if (g_failures != 0) {
		std::cerr << g_failures << " S4 check(s) failed\n";
		return 1;
	}
	std::cout << "SLACS2 NG S4 checks passed\n";
	return 0;
}
