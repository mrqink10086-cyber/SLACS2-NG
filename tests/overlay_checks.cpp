#include "integration/OverlayAdapter.h"

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

	std::string Node(unsigned a_slot)
	{
		return "Body [Ovl" + std::to_string(a_slot) + "]";
	}

	RegionTarget Target(Region a_region)
	{
		RegionTarget target{ 1, a_region, {}, 1 };
		for (unsigned i = 1; i <= 5; ++i) {
			target.layers.push_back({ 100 * RegionIndex(a_region) + i, static_cast<std::uint16_t>(i), TextureVariant::Normal });
		}
		return target;
	}

	void Setup(OverlayAdapter& a_adapter)
	{
		fakeOverride.data.clear();
		fakeOverlay.count = 40;
		a_adapter.setDependencies({ [] { return std::filesystem::path("overlay-test-no-optional-maps"); }, [] { return 1; }, {} });
	}

	unsigned Visible(unsigned a_first, unsigned a_last, std::string_view a_prefix)
	{
		unsigned count{};
		for (unsigned slot = a_first; slot <= a_last; ++slot) {
			const auto it = fakeOverride.data.find({ Node(slot), 9, 0 });
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
			for (unsigned slot = 8; slot <= 12; ++slot) {
				fakeOverride.data[{ Node(slot), 9, 0 }] = defaultTexture;
				fakeOverride.data[{ Node(slot), 8, 255 }] = alpha;
			}
			const auto result = adapter.reconcile(actor, Target(Region::BodyVaginal), Settings{});
			Expect(result.status == OverlayStatus::Applied && result.slotsApplied == 5,
				"blank diffuse slots remain usable with residual alpha");
		}

		OverlayAdapter adapter;
		Setup(adapter);
		auto target = Target(Region::BodyVaginal);
		adapter.reconcile(actor, target, Settings{});
		for (unsigned slot = 7; slot <= 12; ++slot) {
			fakeOverride.data[{ Node(slot), 9, 0 }] = defaultTexture;
			fakeOverride.data[{ Node(slot), 8, 255 }] = 0.0F;
		}
		++target.revision;
		const auto result = adapter.reconcile(actor, target, Settings{});
		Expect(result.status == OverlayStatus::Applied && result.slotsApplied == 5,
			"cleared cached slots are reused on the first reconcile");
	}

	void CheckForeignOwnership()
	{
		for (unsigned kind = 0; kind < 3; ++kind) {
			OverlayAdapter adapter;
			Setup(adapter);
			for (unsigned slot = 8; slot <= 12; ++slot) {
				fakeOverride.data[{ Node(slot), 9, 0 }] = defaultTexture;
				fakeOverride.data[{ Node(slot), 8, 255 }] = 0.0F;
				if (kind == 0) fakeOverride.data[{ Node(slot), 9, 0 }] = "textures\\foreign\\tattoo.dds";
				if (kind == 1) fakeOverride.data[{ Node(slot), 9, 1 }] = "textures\\foreign\\normal.dds";
				if (kind == 2) fakeOverride.data[{ Node(slot), 6, 255 }] = 1.0F;
			}
			const auto before = fakeOverride.data;
			const auto result = adapter.reconcile(actor, Target(Region::BodyVaginal), Settings{});
			Expect(result.status == OverlayStatus::NoOwnedCapacity && result.slotsApplied == 1,
				"foreign textures and materials still reserve their slots");
			for (const auto& [key, value] : before) {
				Expect(fakeOverride.data.at(key) == value, "foreign slot properties remain unchanged");
			}
		}
	}

	void CheckMigration(unsigned a_oldStart, unsigned a_newStart, bool a_restore = false)
	{
		OverlayAdapter adapter;
		Setup(adapter);
		Settings settings;
		settings.overlayOpacity = 0.35F;
		settings.firstOwnedSlot[0] = static_cast<std::uint8_t>(a_oldStart);
		constexpr std::array regions{ Region::BodyVaginal, Region::BodyAnal, Region::BodyBreast, Region::BodyLegs };
		constexpr std::array prefixes{ "textures\\slacs2\\Vaginal\\", "textures\\slacs2\\Anal\\", "textures\\slacs2\\Breast\\", "textures\\slacs2\\Footjob\\" };
		const auto targetFor = [](Region a_region) {
			auto target = Target(a_region);
			if (a_region == Region::BodyLegs) target.layers.resize(2);
			return target;
		};
		for (auto region : regions) {
			adapter.reconcile(actor, targetFor(region), settings);
		}
		if (a_restore) adapter.clearSession(2);
		settings.firstOwnedSlot[0] = static_cast<std::uint8_t>(a_newStart);
		for (std::size_t i = 0; i < regions.size(); ++i) {
			const auto region = regions[a_restore ? regions.size() - 1 - i : i];
			const auto result = adapter.reconcile(actor, targetFor(region), settings);
			Expect((result.status == OverlayStatus::Applied && result.slotsApplied == targetFor(region).layers.size()) ||
				result.status == OverlayStatus::NoChange, "migration applies every requested layer");
		}
		for (std::size_t i = 0; i < regions.size(); ++i) {
			const auto range = OverlaySlotsForRegion(settings, regions[i]);
			Expect(Visible(range.firstSlot, range.firstSlot + range.slotCount - 1, prefixes[i]) == targetFor(regions[i]).layers.size(),
				"later region cleanup must preserve earlier region's migrated layers");
		}
		Expect(Visible(0, 39, "textures\\slacs2\\") == 17, "migration leaves no orphaned layers in old slots");
		for (unsigned slot = 0; slot < 40; ++slot) {
			if (Visible(slot, slot, "textures\\slacs2\\") != 0) {
				Expect(std::get<float>(fakeOverride.data.at({ Node(slot), 8, 255 })) == settings.overlayOpacity,
					"migration preserves the new region's opacity");
			}
		}
		adapter.clearActor(actor, 1);
		Expect(Visible(0, 39, "textures\\slacs2\\") == 0, "clear removes every migrated layer");
	}

	void CheckCapacityIntersection()
	{
		OverlayAdapter adapter;
		Setup(adapter);
		Settings settings;
		settings.firstOwnedSlot[0] = 20;
		fakeOverlay.count = 21;
		const auto result = adapter.reconcile(actor, Target(Region::BodyVaginal), settings);
		Expect(result.status == OverlayStatus::NoOwnedCapacity && result.slotsApplied == 1,
			"actual RaceMenu count limits the configured region range");
		fakeOverlay.count = 40;
		const auto recovered = adapter.reconcile(actor, Target(Region::BodyVaginal), settings);
		Expect(recovered.status == OverlayStatus::Applied && recovered.slotsApplied == 5,
			"partial projection recovers all layers when capacity becomes available");
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
				fakeOverride.data[{ Node(7), 9, 0 }] = "textures\\foreign\\tattoo.dds";
				fakeOverride.data[{ Node(7), 8, 255 }] = foreignAlpha;
				if (clearActor) {
					adapter.clearActor(actor, 1);
				} else {
					target.layers.clear();
					++target.revision;
					adapter.reconcile(actor, target, settings);
				}
				Expect(std::get<std::string>(fakeOverride.data.at({ Node(7), 9, 0 })) == "textures\\foreign\\tattoo.dds",
					"clear preserves a foreign diffuse that replaced our layer");
				Expect(std::get<float>(fakeOverride.data.at({ Node(7), 8, 255 })) == (foreignAlpha == 0.25F ? 1.0F : foreignAlpha),
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
				if (diffuseMode == 0) fakeOverride.data[{ Node(7), 9, 0 }] = defaultTexture;
				if (diffuseMode == 1) fakeOverride.data.erase({ Node(7), 9, 0 });
				fakeOverride.data[{ Node(7), 9, 1 }] = "textures\\foreign\\normal.dds";
				fakeOverride.data[{ Node(7), 9, 2 }] = "textures\\foreign\\specular.dds";
				fakeOverride.data[{ Node(7), 8, 255 }] = 0.7F;
				if (clearActor) {
					adapter.clearActor(actor, 1);
				} else {
					target.layers.clear();
					++target.revision;
					adapter.reconcile(actor, target, settings);
				}
				Expect(std::get<std::string>(fakeOverride.data.at({ Node(7), 9, 1 })) == "textures\\foreign\\normal.dds" &&
					std::get<std::string>(fakeOverride.data.at({ Node(7), 9, 2 })) == "textures\\foreign\\specular.dds" &&
					std::get<float>(fakeOverride.data.at({ Node(7), 8, 255 })) == 0.7F,
					"clear preserves independently replaced material keys for missing, default and owned diffuse");
			}
		}
	}

	void CheckRestoredCleanup()
	{
		constexpr std::array regions{ Region::BodyLegs, Region::Face, Region::Hands, Region::Feet };
		constexpr std::array domains{ "Body", "Face", "Hands", "Feet" };
		for (std::size_t i = 0; i < regions.size(); ++i) {
			for (unsigned mode = 0; mode < 3; ++mode) {
				OverlayAdapter adapter;
				Setup(adapter);
				const auto node = [&](unsigned slot) { return std::string(domains[i]) + " [Ovl" + std::to_string(slot) + "]"; };
				const std::string owned = "textures\\slacs2\\Vaginal\\body01.dds";
				for (unsigned slot = 0; slot < 2; ++slot) {
					fakeOverride.data[{ node(slot), 9, 0 }] = owned;
					fakeOverride.data[{ node(slot), 9, 1 }] = slot == 0 ? owned : "textures\\foreign\\normal.dds";
					fakeOverride.data[{ node(slot), 9, 2 }] = "textures\\foreign\\specular.dds";
					fakeOverride.data[{ node(slot), 8, 255 }] = 0.7F;
				}
				fakeOverride.data[{ node(2), 9, 0 }] = "textures\\foreign\\tattoo.dds";
				fakeOverride.data[{ node(2), 6, 255 }] = 1.0F;
				Settings settings;
				settings.overlaysEnabled = mode != 1;
				if (mode == 2) fakeOverlay.count = 3;
				auto target = Target(regions[i]);
				if (mode == 0) target.layers.clear();
				adapter.reconcile(actor, target, settings);
				for (unsigned slot = 0; slot < 2; ++slot) {
					Expect(std::get<std::string>(fakeOverride.data.at({ node(slot), 9, 0 })) == defaultTexture,
						"restore clears old SLACS diffuse outside the current range for empty, disabled and no-capacity targets");
					Expect(std::get<float>(fakeOverride.data.at({ node(slot), 8, 255 })) == 0.7F,
						"cacheless cleanup preserves opacity whose previous owner cannot be established");
					Expect(std::get<std::string>(fakeOverride.data.at({ node(slot), 9, 2 })) == "textures\\foreign\\specular.dds",
						"cacheless cleanup preserves foreign material maps");
				}
				Expect(std::get<std::string>(fakeOverride.data.at({ node(1), 9, 1 })) == "textures\\foreign\\normal.dds" &&
					std::get<std::string>(fakeOverride.data.at({ node(2), 9, 0 })) == "textures\\foreign\\tattoo.dds" &&
					std::get<float>(fakeOverride.data.at({ node(2), 6, 255 })) == 1.0F,
					"restore preserves foreign diffuse, normal and texture-set overrides");
				if (i < 2) Expect(std::get<std::string>(fakeOverride.data.at({ node(0), 9, 1 })).empty(),
					"restore clears owned maps even when the first Body target has no normal channel");
			}
		}
	}
}

int main()
{
	CheckBlankSlots();
	CheckForeignOwnership();
	for (unsigned oldStart = 0; oldStart <= 20; ++oldStart) {
		for (unsigned newStart = 0; newStart <= 20; ++newStart) {
			CheckMigration(oldStart, newStart);
			CheckMigration(oldStart, newStart, true);
		}
	}
	CheckCapacityIntersection();
	CheckForeignTakeover();
	CheckForeignMaterialKeys();
	CheckRestoredCleanup();
	if (failures == 0) std::cout << "SLACS2 NG overlay checks passed\n";
	return failures == 0 ? 0 : 1;
}
