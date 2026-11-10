#include "integration/OverlayAdapter.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

namespace
{
	using namespace slacs;
	int failures{};
	constexpr auto defaultTexture = "textures\\actors\\character\\overlays\\default.dds";
	constexpr auto legacyDefaultTexture = "actors\\character\\overlays\\default.dds";
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

	void Setup(OverlayAdapter& a_adapter, std::filesystem::path a_dataRoot = "overlay-test-no-optional-maps")
	{
		fakeOverride.data.clear();
		fakeOverride.writes = 0;
		fakeOverride.setNodePropertiesCalls = 0;
		fakeOverlay.counts = { 6, 3, 3, 3 };
		fakeOverlay.countCalls = 0;
		fakeOverlay.formatCalls = 0;
		fakeOverlay.addOverlaysCalls = 0;
		fakeOverride.getCalls = 0;
		fakeOverride.hasCalls = 0;
		a_adapter.setDependencies({ [dataRoot = std::move(a_dataRoot)] { return dataRoot; }, [] { return 1; }, {} });
	}

	struct OptionalTextureFixture
	{
		std::filesystem::path root = std::filesystem::temp_directory_path() /
			("slacs-overlay-drift-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

		OptionalTextureFixture()
		{
			const auto folder = root / "Textures" / "slacs2" / "Vaginal";
			std::filesystem::create_directories(folder);
			std::ofstream(folder / "body1_n.dds").put('n');
			std::ofstream(folder / "body1_s.dds").put('s');
		}

		~OptionalTextureFixture()
		{
			std::error_code error;
			std::filesystem::remove_all(root, error);
		}
	};

	void CheckTextureResourcePreflight()
	{
		const std::string diffuse = "textures\\slacs2\\Vaginal\\body1.dds";
		for (const auto& missing : { std::string(defaultTexture), diffuse }) {
			OverlayAdapter adapter;
			Setup(adapter);
			adapter.setDependencies({ [] { return std::filesystem::path("missing-loose-assets"); },
				[] { return SessionEpoch{ 1 }; }, {},
				[&](const std::string& path) { return path != missing; } });
			const auto result = adapter.reconcile(actor, Target(Region::BodyVaginal, 1), Settings{});
			Expect(result.status == OverlayStatus::Failed && result.error == OverlayErrorCode::MissingTexture,
				"unreadable required resource rejects the target");
			Expect(result.resourcePath == missing, "unreadable resource reports its precise path");
			Expect(fakeOverride.writes == 0 && fakeOverlay.addOverlaysCalls == 0 && fakeOverride.setNodePropertiesCalls == 0,
				"resource rejection happens before creating overlays or changing materials");
		}
		OverlayAdapter adapter;
		Setup(adapter);
		adapter.setDependencies({ [] { return std::filesystem::path("missing-loose-assets"); },
			[] { return SessionEpoch{ 1 }; }, {}, [](const std::string&) { return true; } });
		const auto result = adapter.reconcile(actor, Target(Region::BodyVaginal, 1), Settings{});
		Expect(result.status == OverlayStatus::Applied, "virtual resources are accepted without loose files");
		bool hasNormal = false;
		bool hasSpecular = false;
		for (const auto& [key, value] : fakeOverride.data) {
			if (std::get<1>(key) != 9 || !std::holds_alternative<std::string>(value)) continue;
			const auto& path = std::get<std::string>(value);
			hasNormal |= std::get<2>(key) == 1 && path.ends_with("body1_n.dds");
			hasSpecular |= std::get<2>(key) == 2 && path.ends_with("body1_s.dds");
		}
		Expect(hasNormal && hasSpecular, "optional maps use the virtual resource query as well");
	}

	void CheckApiCallMetrics()
	{
		OverlayAdapter adapter;
		Setup(adapter);
		(void)adapter.reconcile(actor, Target(Region::BodyVaginal), Settings{});
		const auto firstGets = fakeOverride.getCalls;
		const auto firstHas = fakeOverride.hasCalls;
		Expect(firstGets < 42, "slot snapshots reduce first reconcile GetNodeOverride calls below baseline");
		std::cout << "overlay API after first reconcile: get=" << firstGets
			<< " has=" << firstHas << " format=" << fakeOverlay.formatCalls
			<< " count=" << fakeOverlay.countCalls << " addOverlays=" << fakeOverlay.addOverlaysCalls << '\n';
		fakeOverride.getCalls = 0;
		fakeOverride.hasCalls = 0;
		fakeOverlay.formatCalls = 0;
		fakeOverlay.countCalls = 0;
		fakeOverlay.addOverlaysCalls = 0;
		(void)adapter.reconcile(actor, Target(Region::BodyVaginal), Settings{});
		Expect(fakeOverride.getCalls == 20 && fakeOverride.hasCalls == 0,
			"unchanged full target validates each owned slot once without rescanning the pool");
		std::cout << "overlay API after unchanged reconcile: get=" << fakeOverride.getCalls
			<< " has=" << fakeOverride.hasCalls << " format=" << fakeOverlay.formatCalls
			<< " count=" << fakeOverlay.countCalls << " addOverlays=" << fakeOverlay.addOverlaysCalls << '\n';
	}

	void CheckDistinctLayerSlots()
	{
		for (const auto region : { Region::BodyVaginal, Region::BodyLegs }) {
			OverlayAdapter adapter;
			Setup(adapter);
			auto target = Target(region);
			if (region == Region::BodyLegs) {
				target.layers.back().textureIndex = 4;
			}
			const auto result = adapter.reconcile(actor, target, Settings{});
			const auto expectedDistinct = region == Region::BodyLegs ? 4U : 5U;
			std::vector<std::string> textures;
			for (unsigned slot = 0; slot < 5; ++slot) {
				const auto it = fakeOverride.data.find({ Node(slot), 9, 0 });
				if (it != fakeOverride.data.end()) {
					textures.push_back(std::get<std::string>(it->second));
				}
			}
			std::sort(textures.begin(), textures.end());
			textures.erase(std::unique(textures.begin(), textures.end()), textures.end());
			Expect(result.slotsApplied == 5, "five logical layers occupy five independent RaceMenu nodes");
			Expect(textures.size() == expectedDistinct,
				"each layer uses its textureIndex; only the four-texture legs pool repeats one texture");
		}
	}

	void CheckAppendingLayerPreservesSlots()
	{
		OverlayAdapter adapter;
		Setup(adapter);
		fakeOverlay.counts[0] = 5;
		auto target = Target(Region::BodyVaginal, 2);
		adapter.reconcile(actor, target, Settings{});
		std::array<std::map<IOverrideInterface::Key, std::variant<std::string, float>>, 2> priorMaterials;
		for (unsigned slot = 0; slot < priorMaterials.size(); ++slot) {
			for (const auto& [key, value] : fakeOverride.data) {
				if (std::get<0>(key) == Node(slot)) priorMaterials[slot].emplace(key, value);
			}
		}
		const auto priorWrites = fakeOverride.writes;
		target.layers.push_back({ 999, 3, TextureVariant::Normal });
		++target.revision;
		const auto result = adapter.reconcile(actor, target, Settings{});
		Expect(result.status == OverlayStatus::Applied && result.slotsApplied == 3,
			"appending a layer applies the expanded target");
		for (unsigned slot = 0; slot < priorMaterials.size(); ++slot) {
			for (const auto& [key, value] : priorMaterials[slot]) {
				const auto found = fakeOverride.data.find(key);
				Expect(found != fakeOverride.data.end() && found->second == value,
					"appending a layer leaves each existing slot material unchanged");
			}
		}
		Expect(fakeOverride.writes == priorWrites + 4,
			"appending one layer writes only the four material keys for its new slot");
		Expect(std::get<std::string>(fakeOverride.data.at({ Node(2), 9, 0 })) == "textures\\slacs2\\Vaginal\\body3.dds",
			"the appended layer claims the next free RaceMenu slot");
	}

	void CheckUnchangedTargetRepairsLiveMaterial()
	{
		OverlayAdapter adapter;
		OptionalTextureFixture textureFixture;
		Setup(adapter, textureFixture.root);
		Settings settings;
		settings.overlayOpacity = 0.25F;
		const auto target = Target(Region::BodyVaginal, 1);
		adapter.reconcile(actor, target, settings);
		const auto diffusePath = std::string("textures\\slacs2\\Vaginal\\body1.dds");
		const auto ownedDiffuse = std::find_if(fakeOverride.data.begin(), fakeOverride.data.end(), [](const auto& entry) {
			return std::get<1>(entry.first) == 9 && std::get<2>(entry.first) == 0 &&
				std::get_if<std::string>(&entry.second) &&
				*std::get_if<std::string>(&entry.second) == "textures\\slacs2\\Vaginal\\body1.dds";
		});
		Expect(ownedDiffuse != fakeOverride.data.end(), "initial projection owns the expected diffuse node");
		const std::string node = ownedDiffuse != fakeOverride.data.end() ? std::get<0>(ownedDiffuse->first) : Node(0);
		const auto checkRepair = [&](unsigned a_key, unsigned a_index, std::string_view a_expected, std::string_view a_message) {
			fakeOverride.data.erase({ node, a_key, a_index });
			const auto result = adapter.reconcile(actor, target, settings);
			const auto found = fakeOverride.data.find({ node, a_key, a_index });
			Expect(result.status == OverlayStatus::Applied, "same-revision material drift is reapplied");
			Expect(found != fakeOverride.data.end() && std::get<std::string>(found->second) == a_expected, a_message);
		};
		checkRepair(9, 0, diffusePath, "missing diffuse is restored for an unchanged target");
		checkRepair(9, 1, "textures\\slacs2\\Vaginal\\body1_n.dds", "missing normal map is restored for an unchanged target");
		checkRepair(9, 2, "textures\\slacs2\\Vaginal\\body1_s.dds", "missing specular map is restored for an unchanged target");
		fakeOverride.data[{ node, 8, 255 }] = 0.7F;
		const auto alphaResult = adapter.reconcile(actor, target, settings);
		Expect(alphaResult.status == OverlayStatus::Applied, "same-revision alpha drift is reapplied");
		Expect(std::get<float>(fakeOverride.data.at({ node, 8, 255 })) == 0.25F,
			"changed alpha is restored for an unchanged target");
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

	void CheckLegacyTexturePathClassification()
	{
		{
			OverlayAdapter adapter;
			Setup(adapter);
			for (unsigned slot = 0; slot < 6; ++slot) {
				fakeOverride.data[{ Node(slot), 9, 0 }] = legacyDefaultTexture;
			}
			const auto result = adapter.reconcile(actor, Target(Region::BodyVaginal), Settings{});
			Expect(result.status == OverlayStatus::Applied && result.slotsApplied == 5,
				"the legacy default diffuse path leaves RaceMenu slots available");
		}

		{
			OverlayAdapter adapter;
			Setup(adapter);
			constexpr auto legacyDiffuse = "data\\textures\\slacs2\\Vaginal\\body01.dds";
			fakeOverride.data[{ Node(0), 9, 0 }] = legacyDiffuse;
			fakeOverride.data[{ Node(0), 9, 1 }] = "data\\textures\\slacs2\\Vaginal\\body01_n.dds";
			fakeOverride.data[{ Node(0), 9, 2 }] = "data\\textures\\slacs2\\Vaginal\\body01_s.dds";
			fakeOverride.data[{ Node(0), 8, 255 }] = 0.7F;
			fakeOverride.data[{ Node(1), 9, 0 }] = "data\\textures\\slacs2_ube\\Vaginal\\body01.dds";

			const auto result = adapter.reconcile(actor, Target(Region::BodyVaginal, 0), Settings{});
			Expect(result.status == OverlayStatus::Applied, "cacheless migration cleanup completes");
			Expect(std::get<std::string>(fakeOverride.data.at({ Node(0), 9, 0 })) == defaultTexture,
				"cacheless cleanup recognizes legacy data\\textures\\slacs2 diffuse paths");
			Expect(std::get<std::string>(fakeOverride.data.at({ Node(0), 9, 1 })).empty() &&
				std::get<std::string>(fakeOverride.data.at({ Node(0), 9, 2 })).empty(),
				"cacheless cleanup clears legacy owned normal and specular paths");
			Expect(std::get<std::string>(fakeOverride.data.at({ Node(1), 9, 0 })) ==
				"data\\textures\\slacs2_ube\\Vaginal\\body01.dds",
				"migration cleanup leaves the separate slacs2_ube resource root untouched");
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

	void CheckDestructorWaitsForInstallCallback()
	{
		fakeOverlay.ResetCallbacksForTesting();
		auto adapter = std::make_unique<OverlayAdapter>();
		Setup(*adapter);

		std::mutex callbackMutex;
		std::condition_variable callbackChanged;
		bool callbackEntered{};
		bool releaseCallback{};
		unsigned installedCalls{};
		adapter->setDependencies({
			[] { return std::filesystem::path("."); },
			[] { return SessionEpoch{ 1 }; },
			[&](FormId, SessionEpoch) {
				++installedCalls;
				std::unique_lock lock(callbackMutex);
				callbackEntered = true;
				callbackChanged.notify_all();
				callbackChanged.wait(lock, [&] { return releaseCallback; });
			},
		});
		Expect(adapter->initialize().status == OverlayHandshake::Status::Available,
			"callback lifetime fixture initializes RaceMenu API");
		const auto callback = fakeOverlay.SnapshotInstallCallback();
		Expect(callback != nullptr, "install callback is registered");

		std::thread dispatch([&] { fakeOverlay.InvokeInstallCallback(actor.actor.get(), actor.root.get()); });
		bool enteredCallback{};
		{
			std::unique_lock lock(callbackMutex);
			enteredCallback = callbackChanged.wait_for(lock, std::chrono::seconds(1), [&] { return callbackEntered; });
			Expect(enteredCallback, "install callback entered before adapter destruction");
			if (!enteredCallback) releaseCallback = true;
		}
		callbackChanged.notify_all();
		if (!enteredCallback) {
			dispatch.join();
			adapter.reset();
			return;
		}

		std::mutex stopMutex;
		std::condition_variable stopChanged;
		bool destructionReturned{};
		std::thread destroy([&] {
			adapter.reset();
			{
				std::lock_guard lock(stopMutex);
				destructionReturned = true;
			}
			stopChanged.notify_all();
		});

		const bool unregisterStarted = fakeOverlay.WaitForUnregisterAttempt(std::chrono::seconds(1));
		Expect(unregisterStarted, "destructor unregisters the install callback");
		if (unregisterStarted && callback) {
			callback(actor.actor.get(), actor.root.get());
		}
		{
			std::unique_lock lock(stopMutex);
			const bool returnedEarly = stopChanged.wait_for(lock, std::chrono::milliseconds(50), [&] { return destructionReturned; });
			Expect(!returnedEarly, "destructor waits while an install callback holds its owner");
		}

		{
			std::lock_guard lock(callbackMutex);
			releaseCallback = true;
		}
		callbackChanged.notify_all();
		dispatch.join();
		destroy.join();
		Expect(installedCalls == 1, "late callback after stop begins is a no-op");
		Expect(fakeOverlay.unregisterCalls == 1, "install callback registration is removed exactly once");
		Expect(fakeOverlay.SnapshotInstallCallback() == nullptr, "RaceMenu no longer retains the callback after destruction");
	}

	void CheckExplicitShutdownUnregistersCallback()
	{
		fakeOverlay.ResetCallbacksForTesting();
		OverlayAdapter adapter;
		Setup(adapter);
		unsigned installedCalls{};
		adapter.setDependencies({
			[] { return std::filesystem::path("."); },
			[] { return SessionEpoch{ 1 }; },
			[&](FormId, SessionEpoch) { ++installedCalls; },
		});
		Expect(adapter.initialize().status == OverlayHandshake::Status::Available,
			"explicit shutdown fixture initializes RaceMenu API");
		const auto callback = fakeOverlay.SnapshotInstallCallback();
		adapter.shutdown();
		adapter.shutdown();
		Expect(fakeOverlay.unregisterCalls == 1, "explicit shutdown is idempotent and unregisters once");
		Expect(fakeOverlay.SnapshotInstallCallback() == nullptr, "explicit shutdown removes RaceMenu callback");
		if (callback) callback(actor.actor.get(), actor.root.get());
		Expect(installedCalls == 0, "a saved callback invoked after shutdown is a no-op");
	}
}

int main()
{
	CheckApiCallMetrics();
	CheckTextureResourcePreflight();
	CheckDistinctLayerSlots();
	CheckAppendingLayerPreservesSlots();
	CheckUnchangedTargetRepairsLiveMaterial();
	CheckBlankSlots();
	CheckLegacyTexturePathClassification();
	CheckRaceMenuCapacities();
	CheckCapacityGrowth();
	CheckPartialRetryAndReleasedSlots();
	CheckSharedBodySlots();
	CheckForeignOwnership();
	CheckForeignTakeover();
	CheckForeignMaterialKeys();
	CheckRestoredCleanup();
	CheckDestructorWaitsForInstallCallback();
	CheckExplicitShutdownUnregistersCallback();
	if (failures == 0) std::cout << "SLACS2 NG overlay checks passed\n";
	return failures == 0 ? 0 : 1;
}
