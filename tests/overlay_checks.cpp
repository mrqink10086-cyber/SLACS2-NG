#include "integration/OverlayAdapter.h"

#include <array>
#include <iostream>
#include <string_view>

namespace
{
	using namespace slacs;
	int failures{};
	constexpr auto defaultTexture = "textures\\actors\\character\\overlays\\default.dds";
	const ActorLease actor{ std::make_shared<RE::Actor>(), std::make_shared<RE::NiAVObject>() };

	void Expect(bool a_condition, std::string_view a_message)
	{
		if (!a_condition) {
			++failures;
			std::cerr << "FAIL: " << a_message << '\n';
		}
	}

	std::string Domain(Region a_region)
	{
		switch (a_region) {
		case Region::Face: return "Face";
		case Region::Hands: return "Hands";
		case Region::Feet: return "Feet";
		default: return "Body";
		}
	}

	std::string Node(unsigned a_slot, std::string_view a_domain = "Body")
	{
		return std::string(a_domain) + " [Ovl" + std::to_string(a_slot) + "]";
	}

	RegionTarget Target(Region a_region, unsigned a_layerCount = 5)
	{
		RegionTarget target{ 1, a_region, {}, 1 };
		for (unsigned i = 1; i <= a_layerCount; ++i) {
			target.layers.push_back({ 100 * RegionIndex(a_region) + i, static_cast<std::uint16_t>(i), TextureVariant::Normal });
		}
		return target;
	}

	void Setup(OverlayAdapter& a_adapter)
	{
		fakeOverride.data.clear();
		fakeOverride.writes = 0;
		fakeOverride.setNodePropertiesCalls = 0;
		fakeOverlay.counts = { 6, 3, 3, 3 };
		a_adapter.setDependencies({ [] { return std::filesystem::path("overlay-test-no-optional-maps"); }, [] { return 1; }, {} });
	}

	unsigned Visible(unsigned a_count, std::string_view a_prefix, std::string_view a_domain = "Body")
	{
		unsigned count{};
		for (unsigned slot = 0; slot < a_count; ++slot) {
			const auto it = fakeOverride.data.find({ Node(slot, a_domain), 9, 0 });
			if (it != fakeOverride.data.end()) {
				const auto* value = std::get_if<std::string>(&it->second);
				count += value && value->starts_with(a_prefix);
			}
		}
		return count;
	}

	void CheckBlankSlots()
	{
		for (const float alpha : { 0.0F, 0.5F, 0.98F, 1.0F }) {
			OverlayAdapter adapter;
			Setup(adapter);
			for (unsigned slot = 0; slot < 5; ++slot) {
				fakeOverride.data[{ Node(slot), 9, 0 }] = defaultTexture;
				fakeOverride.data[{ Node(slot), 8, 255 }] = alpha;
			}
			const auto result = adapter.reconcile(actor, Target(Region::BodyVaginal), Settings{});
			Expect(result.status == OverlayStatus::Applied && result.slotsApplied == 5,
				"blank diffuse slots remain usable with residual alpha");
		}
	}

	void CheckRaceMenuCapacities()
	{
		OverlayAdapter adapter;
		Setup(adapter);
		Expect(adapter.capacities() == std::array<std::uint32_t, 4>{ 6, 3, 3, 3 },
			"capacities report loaded RaceMenu counts in Body, Face, Hands, Feet order");
		fakeRaceMenuApiAvailable = false;
		Expect(adapter.capacities() == std::array<std::uint32_t, 4>{}, "unavailable RaceMenu reports zero capacities");
		fakeRaceMenuApiAvailable = true;

		fakeOverlay.counts = { 300, 300, 300, 300 };
		Expect(adapter.capacities() == std::array<std::uint32_t, 4>{ 127, 127, 127, 127 },
			"reported capacities clamp to RaceMenu's 127-slot maximum");

		fakeOverlay.counts = { 6, 3, 3, 3 };
		constexpr std::array regions{ Region::BodyVaginal, Region::Face, Region::Hands, Region::Feet };
		constexpr std::array counts{ 5U, 3U, 3U, 3U };
		constexpr std::array prefixes{
			"textures\\slacs2\\Vaginal\\",
			"textures\\slacs2\\Oral\\",
			"textures\\slacs2\\Handjob\\",
			"textures\\slacs2\\Footjob\\",
		};
		for (std::size_t i = 0; i < regions.size(); ++i) {
			const auto result = adapter.reconcile(actor, Target(regions[i]), Settings{});
			Expect(result.slotsApplied == counts[i], "default RaceMenu capacities expose the available slots");
			Expect(Visible(counts[i], prefixes[i], Domain(regions[i])) == counts[i],
				"default RaceMenu capacities project textures in every domain");
			Expect(result.status == (counts[i] == 5 ? OverlayStatus::Applied : OverlayStatus::NoOwnedCapacity),
				"capacity below five logical layers is reported as partial");
		}
	}

	void CheckCapacityGrowth()
	{
		OverlayAdapter adapter;
		Setup(adapter);
		fakeOverlay.counts[0] = 1;
		auto target = Target(Region::BodyVaginal);
		const auto partial = adapter.reconcile(actor, target, Settings{});
		Expect(partial.status == OverlayStatus::NoOwnedCapacity && partial.slotsApplied == 1,
			"partial projection uses the loaded RaceMenu capacity");

		fakeOverlay.counts[0] = 6;
		const auto recovered = adapter.reconcile(actor, target, Settings{});
		Expect(recovered.status == OverlayStatus::Applied && recovered.slotsApplied == 5,
			"a larger loaded RaceMenu capacity restores all logical layers");
		Expect(Visible(6, "textures\\slacs2\\Vaginal\\") == 5,
			"capacity growth projects the previously hidden layers");
	}

	void CheckPartialRetryAndReleasedSlots()
	{
		OverlayAdapter adapter;
		Setup(adapter);
		fakeOverlay.counts[0] = 5;
		for (unsigned slot = 0; slot < 4; ++slot) {
			fakeOverride.data[{ Node(slot), 9, 0 }] = "textures\\foreign\\tattoo.dds";
		}
		auto target = Target(Region::BodyVaginal);
		const auto partial = adapter.reconcile(actor, target, Settings{});
		Expect(partial.status == OverlayStatus::NoOwnedCapacity && partial.slotsApplied == 1,
			"foreign occupancy produces a partial projection");
		const auto writesAfterFirst = fakeOverride.writes;
		const auto commitsAfterFirst = fakeOverride.setNodePropertiesCalls;

		const auto repeated = adapter.reconcile(actor, target, Settings{});
		Expect(repeated.status == OverlayStatus::NoOwnedCapacity && repeated.slotsApplied == 1,
			"an unchanged partial target remains retryable");
		Expect(fakeOverride.writes == writesAfterFirst && fakeOverride.setNodePropertiesCalls == commitsAfterFirst,
			"an unchanged partial retry performs no material writes or node commit");

		for (unsigned slot = 0; slot < 4; ++slot) {
			fakeOverride.data.erase({ Node(slot), 9, 0 });
		}
		const auto restored = adapter.reconcile(actor, target, Settings{});
		Expect(restored.status == OverlayStatus::Applied && restored.slotsApplied == 5,
			"released RaceMenu slots restore the full projection at the same revision");
		Expect(Visible(5, "textures\\slacs2\\Vaginal\\") == 5,
			"the same partial revision expands into newly released slots");
	}

	void CheckSharedBodySlots()
	{
		OverlayAdapter adapter;
		Setup(adapter);
		fakeOverlay.counts[0] = 8;
		const auto vaginal = Target(Region::BodyVaginal);
		const auto anal = Target(Region::BodyAnal);
		const auto breast = Target(Region::BodyBreast);
		Expect(adapter.reconcile(actor, vaginal, Settings{}).slotsApplied == 5,
			"the first Body region claims only its own available RaceMenu slots");
		const auto analResult = adapter.reconcile(actor, anal, Settings{});
		Expect(analResult.status == OverlayStatus::NoOwnedCapacity && analResult.slotsApplied == 3,
			"another Body region can use the remaining shared slots");
		Expect(adapter.reconcile(actor, breast, Settings{}).slotsApplied == 0,
			"a third Body region cannot take slots owned by earlier regions");
		Expect(Visible(8, "textures\\slacs2\\Vaginal\\") == 5 &&
			Visible(8, "textures\\slacs2\\Anal\\") == 3,
			"reconciling another Body region preserves earlier Body projections");

		auto clearVaginal = vaginal;
		clearVaginal.layers.clear();
		++clearVaginal.revision;
		adapter.reconcile(actor, clearVaginal, Settings{});
		const auto recovered = adapter.reconcile(actor, breast, Settings{});
		Expect(recovered.slotsApplied == 5 && recovered.status == OverlayStatus::Applied,
			"clearing one Body region makes its slots available to another");
		Expect(Visible(8, "textures\\slacs2\\Anal\\") == 3 &&
			Visible(8, "textures\\slacs2\\Breast\\") == 5,
			"reallocation preserves the remaining Body region");
	}

	void CheckForeignOwnership()
	{
		for (unsigned kind = 0; kind < 3; ++kind) {
			OverlayAdapter adapter;
			Setup(adapter);
			for (unsigned slot = 0; slot < 5; ++slot) {
				fakeOverride.data[{ Node(slot), 9, 0 }] = defaultTexture;
				fakeOverride.data[{ Node(slot), 8, 255 }] = 0.0F;
				if (kind == 0) fakeOverride.data[{ Node(slot), 9, 0 }] = "textures\\foreign\\tattoo.dds";
				if (kind == 1) fakeOverride.data[{ Node(slot), 9, 1 }] = "textures\\foreign\\normal.dds";
				if (kind == 2) fakeOverride.data[{ Node(slot), 6, 255 }] = 1.0F;
			}
			const auto before = fakeOverride.data;
			const auto result = adapter.reconcile(actor, Target(Region::BodyVaginal), Settings{});
			Expect(result.status == OverlayStatus::NoOwnedCapacity && result.slotsApplied == 1,
				"foreign texture, normal map and material values reserve slots");
			for (const auto& [key, value] : before) {
				Expect(fakeOverride.data.at(key) == value, "foreign slot properties remain unchanged");
			}
		}
	}

	void CheckForeignTakeover()
	{
		for (const bool clearActor : { false, true }) {
			for (const float foreignAlpha : { 0.25F, 0.7F }) {
				OverlayAdapter adapter;
				Setup(adapter);
				Settings settings;
				settings.overlayOpacity = 0.25F;
				auto target = Target(Region::BodyVaginal);
				adapter.reconcile(actor, target, settings);
				fakeOverride.data[{ Node(0), 9, 0 }] = "textures\\foreign\\tattoo.dds";
				fakeOverride.data[{ Node(0), 8, 255 }] = foreignAlpha;
				if (clearActor) {
					adapter.clearActor(actor, 1);
				} else {
					target.layers.clear();
					++target.revision;
					adapter.reconcile(actor, target, settings);
				}
				Expect(std::get<std::string>(fakeOverride.data.at({ Node(0), 9, 0 })) == "textures\\foreign\\tattoo.dds",
					"clear preserves a foreign diffuse that replaced our layer");
				Expect(std::get<float>(fakeOverride.data.at({ Node(0), 8, 255 })) == (foreignAlpha == 0.25F ? 1.0F : foreignAlpha),
					"clear restores only our residual opacity and preserves a foreign opacity change");
			}
		}
	}

	void CheckForeignMaterialKeys()
	{
		for (const bool clearActor : { false, true }) {
			for (unsigned diffuseMode = 0; diffuseMode < 3; ++diffuseMode) {
				OverlayAdapter adapter;
				Setup(adapter);
				Settings settings;
				settings.overlayOpacity = 0.25F;
				auto target = Target(Region::BodyVaginal);
				adapter.reconcile(actor, target, settings);
				if (diffuseMode == 0) fakeOverride.data[{ Node(0), 9, 0 }] = defaultTexture;
				if (diffuseMode == 1) fakeOverride.data.erase({ Node(0), 9, 0 });
				fakeOverride.data[{ Node(0), 9, 1 }] = "textures\\foreign\\normal.dds";
				fakeOverride.data[{ Node(0), 9, 2 }] = "textures\\foreign\\specular.dds";
				fakeOverride.data[{ Node(0), 8, 255 }] = 0.7F;
				if (clearActor) {
					adapter.clearActor(actor, 1);
				} else {
					target.layers.clear();
					++target.revision;
					adapter.reconcile(actor, target, settings);
				}
				Expect(std::get<std::string>(fakeOverride.data.at({ Node(0), 9, 1 })) == "textures\\foreign\\normal.dds" &&
					std::get<std::string>(fakeOverride.data.at({ Node(0), 9, 2 })) == "textures\\foreign\\specular.dds" &&
					std::get<float>(fakeOverride.data.at({ Node(0), 8, 255 })) == 0.7F,
					"clear preserves independently replaced maps and opacity");
			}
		}
	}

	void CheckRestoredCleanup()
	{
		constexpr std::array regions{ Region::BodyLegs, Region::Face, Region::Hands, Region::Feet };
		constexpr std::array prefixes{
			"textures\\slacs2\\Vaginal\\",
			"textures\\slacs2\\Vaginal\\",
			"textures\\slacs2\\Vaginal\\",
			"textures\\slacs2\\Vaginal\\",
		};
		for (std::size_t i = 0; i < regions.size(); ++i) {
			OverlayAdapter adapter;
			Setup(adapter);
			const auto domain = Domain(regions[i]);
			const std::string owned = std::string(prefixes[i]) + "body01.dds";
			for (unsigned slot = 0; slot < 2; ++slot) {
				fakeOverride.data[{ Node(slot, domain), 9, 0 }] = owned;
				fakeOverride.data[{ Node(slot, domain), 9, 1 }] = slot == 0 ? owned : "textures\\foreign\\normal.dds";
				fakeOverride.data[{ Node(slot, domain), 9, 2 }] = "textures\\foreign\\specular.dds";
				fakeOverride.data[{ Node(slot, domain), 8, 255 }] = 0.7F;
			}
			fakeOverride.data[{ Node(2, domain), 9, 0 }] = "textures\\foreign\\tattoo.dds";
			fakeOverride.data[{ Node(2, domain), 6, 255 }] = 1.0F;
			Settings settings;
			auto target = Target(regions[i], 0);
			const auto result = adapter.reconcile(actor, target, settings);
			Expect(result.status == OverlayStatus::Applied, "restored co-save cleanup completes for empty targets");
			for (unsigned slot = 0; slot < 2; ++slot) {
				Expect(std::get<std::string>(fakeOverride.data.at({ Node(slot, domain), 9, 0 })) == defaultTexture,
					"restore clears old SLACS diffuse across the loaded RaceMenu range");
				Expect(std::get<float>(fakeOverride.data.at({ Node(slot, domain), 8, 255 })) == 0.7F,
					"cacheless cleanup preserves opacity whose previous owner cannot be established");
				Expect(std::get<std::string>(fakeOverride.data.at({ Node(slot, domain), 9, 2 })) == "textures\\foreign\\specular.dds",
					"cacheless cleanup preserves foreign material maps");
			}
			Expect(std::get<std::string>(fakeOverride.data.at({ Node(1, domain), 9, 1 })) == "textures\\foreign\\normal.dds" &&
				std::get<std::string>(fakeOverride.data.at({ Node(2, domain), 9, 0 })) == "textures\\foreign\\tattoo.dds" &&
				std::get<float>(fakeOverride.data.at({ Node(2, domain), 6, 255 })) == 1.0F,
				"restore preserves foreign diffuse, normal and material overrides");
		}
	}
}

int main()
{
	CheckBlankSlots();
	CheckRaceMenuCapacities();
	CheckCapacityGrowth();
	CheckPartialRetryAndReleasedSlots();
	CheckSharedBodySlots();
	CheckForeignOwnership();
	CheckForeignTakeover();
	CheckForeignMaterialKeys();
	CheckRestoredCleanup();
	if (failures == 0) std::cout << "SLACS2 NG overlay checks passed\n";
	return failures == 0 ? 0 : 1;
}
