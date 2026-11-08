#include "integration/OverlayAdapter.h"

#include "integration/racemenu/IPluginInterface.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <string_view>

namespace slacs
{
	namespace
	{
		constexpr std::uint16_t kShaderTextureKey = 9;
		constexpr std::uint16_t kShaderAlphaKey = 8;
		constexpr std::uint8_t kTextureDiffuseIndex = 0;
		constexpr std::uint8_t kTextureNormalIndex = 1;
		// Body skin texture3. Only the three body profiles use this channel;
		// Face index 2 is a FaceGen slot and must not be touched.
		constexpr std::uint8_t kTextureSpecularIndex = 2;
		constexpr std::uint8_t kAlphaIndex = 0xFF;
		constexpr const char* kDefaultDiffuse = "textures\\actors\\character\\overlays\\default.dds";

		struct RegionProfile
		{
			const char* folder;
			const char* prefix;
			std::uint16_t textureCount;
			bool hasNormal;
			bool hasSpecular;
			OverlayDomain domain;
			std::uint16_t slotOffset;
			std::uint16_t slotCount;
		};

		constexpr std::array<RegionProfile, kRegionCount> kRegionProfiles{ {
			{ "Vaginal", "body", 12, true, true, OverlayDomain::Body, 0, 6 },
			{ "Anal", "body", 12, true, true, OverlayDomain::Body, 6, 6 },
			{ "Breast", "chest", 12, true, true, OverlayDomain::Body, 12, 6 },
			{ "Footjob", "legs", 4, false, false, OverlayDomain::Body, 18, 2 },
			{ "Oral", "head", 12, true, false, OverlayDomain::Face, 0, 6 },
			{ "Handjob", "hands", 4, false, false, OverlayDomain::Hands, 0, 2 },
			{ "Footjob", "feet", 4, false, false, OverlayDomain::Feet, 0, 2 },
		} };

		// The UBE profile is intentionally not listed here. The legacy UBE
		// root (textures/slacs2_ube) is absent from the current package, so
		// there is no profile whose assets could be selected. TextureVariant
		// stays Normal/Mega; adding UBE later needs a package resource drop and
		// a new profile, not a public type change.

		constexpr std::array<std::string_view, 6> kOurTexturePrefixes{
			"textures\\slacs2\\vaginal\\",
			"textures\\slacs2\\anal\\",
			"textures\\slacs2\\breast\\",
			"textures\\slacs2\\footjob\\",
			"textures\\slacs2\\handjob\\",
			"textures\\slacs2\\oral\\",
		};

		const RegionProfile& ProfileFor(Region a_region)
		{
			const auto index = RegionIndex(a_region);
			return kRegionProfiles[index < kRegionCount ? index : 0];
		}

		::IOverlayInterface::OverlayLocation LocationFor(OverlayDomain a_domain)
		{
			// Settings orders the domains Body, Face, Hands, Feet; RaceMenu
			// orders the same locations Body, Hand, Feet, Face.
			switch (a_domain) {
			case OverlayDomain::Body:
				return ::IOverlayInterface::OverlayLocation::Body;
			case OverlayDomain::Face:
				return ::IOverlayInterface::OverlayLocation::Face;
			case OverlayDomain::Hands:
				return ::IOverlayInterface::OverlayLocation::Hand;
			case OverlayDomain::Feet:
				return ::IOverlayInterface::OverlayLocation::Feet;
			}
			return ::IOverlayInterface::OverlayLocation::Body;
		}

		class StringVariant final : public ::IOverrideInterface::SetVariant
		{
		public:
			explicit StringVariant(const std::string& a_value) :
				value_(a_value)
			{}

			Type GetType() override { return Type::String; }
			const char* String() override { return value_.c_str(); }

		private:
			std::string value_;
		};

		class FloatVariant final : public ::IOverrideInterface::SetVariant
		{
		public:
			explicit FloatVariant(float a_value) :
				value_(a_value)
			{}

			Type GetType() override { return Type::Float; }
			float Float() override { return value_; }

		private:
			float value_;
		};

		class StringReader final : public ::IOverrideInterface::GetVariant
		{
		public:
			void Int(skee_i32) override {}
			void Float(float) override {}
			void String(const char* a_value) override { value = a_value ? a_value : ""; }
			void Bool(bool) override {}
			void TextureSet(const BGSTextureSet*) override {}

			std::string value;
		};

		class FloatReader final : public ::IOverrideInterface::GetVariant
		{
		public:
			void Int(skee_i32) override {}
			void Float(float a_value) override
			{
				value = a_value;
				present = true;
			}
			void String(const char*) override {}
			void Bool(bool) override {}
			void TextureSet(const BGSTextureSet*) override {}

			float value{};
			bool present{};
		};

		bool equalsIgnoreCase(std::string_view a_left, std::string_view a_right)
		{
			if (a_left.size() != a_right.size()) {
				return false;
			}
			for (std::size_t i = 0; i < a_left.size(); ++i) {
				const auto left = static_cast<unsigned char>(a_left[i]);
				const auto right = static_cast<unsigned char>(a_right[i]);
				if (std::tolower(left) != std::tolower(right)) {
					return false;
				}
			}
			return true;
		}

		bool isDefaultTexture(std::string_view a_value)
		{
			return a_value.empty() || equalsIgnoreCase(a_value, kDefaultDiffuse);
		}

		bool isOurTexture(std::string_view a_value)
		{
			return std::any_of(kOurTexturePrefixes.begin(), kOurTexturePrefixes.end(), [a_value](std::string_view a_prefix) {
				return a_value.size() >= a_prefix.size() &&
					equalsIgnoreCase(a_value.substr(0, a_prefix.size()), a_prefix);
			});
		}

		OverlayResult failure(ActorId a_actor, Region a_region, Revision a_revision, OverlayStatus a_status, OverlayErrorCode a_error)
		{
			OverlayResult result;
			result.status = a_status;
			result.actor = a_actor;
			result.region = a_region;
			result.revision = a_revision;
			result.error = a_error;
			return result;
		}

		std::atomic<OverlayAdapter*> g_active{};
	}

	OverlayAdapter::OverlayAdapter() = default;

	OverlayAdapter::~OverlayAdapter()
	{
		OverlayAdapter* expected = this;
		(void)g_active.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
	}

	void OverlayAdapter::setDependencies(OverlayDependencies a_dependencies)
	{
		dependencies_ = std::move(a_dependencies);
	}

	OverlayHandshake OverlayAdapter::initialize()
	{
		OverlayHandshake handshake;
		if (!api_.initialize()) {
			handshake.status = api_.version() > 0 ? OverlayHandshake::Status::VersionMismatch : OverlayHandshake::Status::Unavailable;
			handshake.apiVersion = api_.version();
			handshake.error = OverlayErrorCode::AbiMismatch;
			return handshake;
		}

		handshake.status = OverlayHandshake::Status::Available;
		handshake.apiVersion = api_.version();
		handshake.error = OverlayErrorCode::None;

		g_active.store(this, std::memory_order_release);
		auto* overlay = static_cast<::IOverlayInterface*>(api_.overlay());
		if (!overlay->RegisterInstallCallback("SLACS2NG", &OverlayAdapter::installCallback)) {
			logs::warn("RaceMenu install callback registration failed (key may already be in use)");
		}
		logs::info("Overlay texture profile UBE is unavailable (textures/slacs2_ube is missing)");
		return handshake;
	}

	bool OverlayAdapter::available() const
	{
		return api_.available();
	}

	void OverlayAdapter::installCallback(RE::TESObjectREFR* a_reference, RE::NiAVObject*)
	{
		auto* self = g_active.load(std::memory_order_acquire);
		if (!self || !a_reference || !self->dependencies_.installed) {
			return;
		}
		const auto formId = a_reference->GetFormID();
		if (formId == 0) {
			return;
		}
		const auto epoch = self->dependencies_.epoch ? self->dependencies_.epoch() : 0;
		self->dependencies_.installed(formId, epoch);
	}

	OverlayResult OverlayAdapter::reconcile(const ActorLease& a_actor, const RegionTarget& a_target, const Settings& a_settings)
	{
		if (RegionIndex(a_target.region) >= kRegionCount) {
			return failure(a_target.actor, a_target.region, a_target.revision, OverlayStatus::Failed, OverlayErrorCode::MissingProfile);
		}
		if (!a_actor.actor) {
			return failure(a_target.actor, a_target.region, a_target.revision, OverlayStatus::InvalidTarget, OverlayErrorCode::InvalidTarget);
		}
		if (!api_.available()) {
			return failure(a_target.actor, a_target.region, a_target.revision, OverlayStatus::ApiUnavailable, OverlayErrorCode::AbiMismatch);
		}
		if (!a_actor.root || !a_actor.actor->Is3DLoaded()) {
			return failure(a_target.actor, a_target.region, a_target.revision, OverlayStatus::DeferredNo3D, OverlayErrorCode::None);
		}
		if (!dependencies_.dataRoot) {
			return failure(a_target.actor, a_target.region, a_target.revision, OverlayStatus::Failed, OverlayErrorCode::ApiFailure);
		}

		auto* overlay = static_cast<::IOverlayInterface*>(api_.overlay());
		auto* overrideApi = static_cast<::IOverrideInterface*>(api_.overrideApi());
		auto* reference = a_actor.actor.get();
		const bool female = reference->GetActorBase() && reference->GetActorBase()->GetSex() == RE::SEX::kFemale;

		const auto& profile = ProfileFor(a_target.region);
		const auto location = LocationFor(profile.domain);
		const char* formatRaw = overlay->GetOverlayFormat(
			::IOverlayInterface::OverlayType::Normal,
			location);
		if (!formatRaw || !*formatRaw || std::string_view(formatRaw).find("{}") == std::string_view::npos) {
			return failure(a_target.actor, a_target.region, a_target.revision, OverlayStatus::Failed, OverlayErrorCode::ApiFailure);
		}
		const std::string format = formatRaw;

		const auto configured = overlay->GetOverlayCount(
			::IOverlayInterface::OverlayType::Normal,
			location);
		const auto range = OverlaySlotsForRegion(a_settings, a_target.region);
		const auto firstSlot = range.firstSlot;
		const std::size_t availableSlots = configured > firstSlot ?
			std::min<std::size_t>(range.slotCount, configured - firstSlot) :
			0;

		const std::uint64_t key = (a_target.actor << 3) | static_cast<std::uint64_t>(RegionIndex(a_target.region));
		const auto cached = applied_.find(key);
		if (cached != applied_.end() && a_target.revision < cached->second.revision) {
			return failure(a_target.actor, a_target.region, a_target.revision, OverlayStatus::StaleRevision, OverlayErrorCode::StaleRevision);
		}

		const auto domainIndex = static_cast<std::size_t>(profile.domain);
		const auto domainBit = static_cast<std::uint8_t>(1U << domainIndex);
		auto& restored = restoredDomains_[a_target.actor];
		if ((restored & domainBit) == 0) {
			// RaceMenu co-saves can restore slots outside today's global settings.
			// Sweep once per domain so later Body regions retain earlier projections.
			constexpr std::array cleanupRegions{ Region::BodyVaginal, Region::Face, Region::Hands, Region::Feet };
			bool removed = false;
			for (std::uint32_t slot = 0; slot < configured; ++slot) {
				removed |= resetSlotIfOurs(reference, female, nodeNameForSlot(format, static_cast<std::uint8_t>(slot)), cleanupRegions[domainIndex]);
			}
			if (removed) overrideApi->SetNodeProperties(reference, true);
			restored |= domainBit;
		}

		if (!a_settings.overlaysEnabled) {
			bool removed = false;
			if (cached != applied_.end()) {
				for (const auto& slot : cached->second.slots) {
					removeSlot(reference, female, nodeNameForSlot(format, slot.slot), slot, a_target.region);
					removed = true;
				}
				applied_.erase(cached);
			}
			for (std::size_t slotIndex = 0; slotIndex < availableSlots; ++slotIndex) {
				const auto slot = static_cast<std::uint8_t>(firstSlot + slotIndex);
				if (resetSlotIfOurs(reference, female, nodeNameForSlot(format, slot), a_target.region)) {
					removed = true;
				}
			}
			if (removed) {
				overrideApi->SetNodeProperties(reference, true);
			}
			return failure(a_target.actor, a_target.region, a_target.revision, OverlayStatus::Applied, OverlayErrorCode::None);
		}

		std::vector<Assignment> assignments;
		assignments.reserve(a_target.layers.size());
		bool partial = false;

		if (!a_target.layers.empty()) {
			if (availableSlots == 0) {
				return failure(a_target.actor, a_target.region, a_target.revision, OverlayStatus::NoOwnedCapacity, OverlayErrorCode::NoOwnedSlot);
			}

			std::vector<bool> usedSlot(availableSlots, false);
			std::vector<bool> assignedLayer(a_target.layers.size(), false);
			const auto* previous = cached != applied_.end() ? &cached->second : nullptr;

			const auto priorForSlot = [&](std::uint8_t a_slot) -> const AppliedSlot* {
				if (!previous) {
					return nullptr;
				}
				for (const auto& candidate : previous->slots) {
					if (candidate.slot == a_slot) {
						return &candidate;
					}
				}
				return nullptr;
			};

			// Newest layers claim capacity first. This is what makes a full
			// region keep the newest logical layers instead of letting the
			// oldest cached assignment pin every slot forever.
			for (std::size_t layerIndex = a_target.layers.size(); layerIndex-- > 0; ) {
				const auto& layer = a_target.layers[layerIndex];

				// Prefer the slot this layer used before, provided it is still
				// in range, unused, and still ours.
				if (previous) {
					for (const auto& slot : previous->slots) {
						if (slot.layerId != layer.layerId || slot.slot < firstSlot) {
							continue;
						}
						const auto slotIndex = static_cast<std::size_t>(slot.slot - firstSlot);
						if (slotIndex >= availableSlots || usedSlot[slotIndex]) {
							continue;
						}
						const auto node = nodeNameForSlot(format, slot.slot);
						if (!priorSlotStillOurs(reference, female, node, slot, a_target.region)) {
							continue;
						}
						assignments.push_back(Assignment{ &layer, slot.slot, &slot });
						usedSlot[slotIndex] = true;
						assignedLayer[layerIndex] = true;
						break;
					}
				}
				if (assignedLayer[layerIndex]) {
					continue;
				}

				for (std::size_t slotIndex = 0; slotIndex < availableSlots; ++slotIndex) {
					if (usedSlot[slotIndex]) {
						continue;
					}
					const auto slot = static_cast<std::uint8_t>(firstSlot + slotIndex);
					const auto node = nodeNameForSlot(format, slot);
					const auto* prior = priorForSlot(slot);
					const bool usable =
						(prior && priorSlotStillOurs(reference, female, node, *prior, a_target.region)) ||
						slotAppearsFree(reference, female, node, a_target.region);
					if (!usable) {
						continue;
					}

					assignments.push_back(Assignment{ &layer, slot, prior });
					usedSlot[slotIndex] = true;
					assignedLayer[layerIndex] = true;
					break;
				}
				if (!assignedLayer[layerIndex]) {
					partial = true;
				}
			}

			if (assignments.empty()) {
				return failure(a_target.actor, a_target.region, a_target.revision, OverlayStatus::NoOwnedCapacity, OverlayErrorCode::NoOwnedSlot);
			}
		}

		const float opacity = std::clamp(a_settings.overlayOpacity, 0.0F, 1.0F);
		const auto signature = makeSignature(a_target, assignments, format, opacity);
		if (cached != applied_.end() && cached->second.revision == a_target.revision && cached->second.signature == signature) {
			return failure(a_target.actor, a_target.region, a_target.revision, OverlayStatus::NoChange, OverlayErrorCode::None);
		}

		const auto initialized = initializedActors_.find(a_target.actor);
		const auto currentEpoch = dependencies_.epoch ? dependencies_.epoch() : 0;
		const bool needsInitialBuild = !a_target.layers.empty() &&
			(initialized == initializedActors_.end() || initialized->second != currentEpoch);
		if (needsInitialBuild) {
			// RaceMenu 0.4.20 names the parameter "defer" but treats true as
			// "immediate" and false as "queue a task". We are already in the
			// verified pump phase, so request the explicit immediate path.
			overlay->AddOverlays(reference, true);
			initializedActors_[a_target.actor] = currentEpoch;
		}

		bool changed = false;

		if (cached != applied_.end()) {
			for (const auto& slot : cached->second.slots) {
				const bool reused = std::any_of(
					assignments.begin(),
					assignments.end(),
					[&](const Assignment& assignment) { return assignment.slot == slot.slot && assignment.prior; });
				if (!reused) {
					const auto node = nodeNameForSlot(format, slot.slot);
					removeSlot(reference, female, node, slot, a_target.region);
					changed = true;
				}
			}
		}

		// After a load applied_ is empty while RaceMenu's co-save still holds
		// our old node overrides. Clear any slot whose diffuse still points at
		// this plugin's texture but which the new logical state does not use.
		for (std::size_t slotIndex = 0; slotIndex < availableSlots; ++slotIndex) {
			const auto slot = static_cast<std::uint8_t>(firstSlot + slotIndex);
			const bool assigned = std::any_of(
				assignments.begin(),
				assignments.end(),
				[&](const Assignment& assignment) { return assignment.slot == slot; });
			if (assigned) {
				continue;
			}
			if (resetSlotIfOurs(reference, female, nodeNameForSlot(format, slot), a_target.region)) {
				changed = true;
			}
		}

		AppliedRegion updated;
		// A partial projection keeps revision 0 so the next pump retries the
		// layers that did not fit once slots become available.
		updated.revision = partial ? 0 : a_target.revision;
		updated.signature = signature;
		updated.slots.reserve(assignments.size());

		for (const auto& assignment : assignments) {
			const auto node = nodeNameForSlot(format, assignment.slot);
			AppliedSlot slot;
			if (!applySlot(reference, female, node, *assignment.layer, slot, a_settings, a_target.region)) {
				return failure(a_target.actor, a_target.region, a_target.revision, OverlayStatus::Failed, OverlayErrorCode::ApiFailure);
			}
			slot.slot = assignment.slot;
			slot.layerId = assignment.layer->layerId;
			updated.slots.push_back(std::move(slot));
			changed = true;
		}

		if (changed) {
			overrideApi->SetNodeProperties(reference, true);
		}

		if (updated.slots.empty()) {
			applied_.erase(key);
		} else {
			applied_[key] = std::move(updated);
		}

		OverlayResult result;
		result.status = partial ? OverlayStatus::NoOwnedCapacity : OverlayStatus::Applied;
		result.actor = a_target.actor;
		result.region = a_target.region;
		result.revision = a_target.revision;
		result.slotsApplied = static_cast<std::uint32_t>(assignments.size());
		result.error = partial ? OverlayErrorCode::NoOwnedSlot : OverlayErrorCode::None;
		return result;
	}

	void OverlayAdapter::invalidateActor(ActorId a_actor, VisualInvalidation a_reason)
	{
		// Keep slot ownership so the next reconcile can still prove that the
		// node overrides belong to us. Clearing the revision/signature forces a
		// full reapply while retaining the mapping between layerId and slot.
		for (auto& [key, region] : applied_) {
			const auto actor = static_cast<ActorId>(key >> 3);
			if (actor == a_actor) {
				region.revision = 0;
				region.signature.clear();
			}
		}
		if (a_reason == VisualInvalidation::Actor3DRebuilt || a_reason == VisualInvalidation::CellChanged) {
			initializedActors_.erase(a_actor);
			restoredDomains_.erase(a_actor);
		}
		logs::debug("Overlay projection invalidated for actor {} (reason {})", a_actor, static_cast<int>(a_reason));
	}

	void OverlayAdapter::clearActor(const ActorLease& a_actor, ActorId a_actorId)
	{
		if (!a_actor.actor || !api_.available()) {
			return;
		}
		auto* overlay = static_cast<::IOverlayInterface*>(api_.overlay());
		auto* overrideApi = static_cast<::IOverrideInterface*>(api_.overrideApi());
		if (!overlay || !overrideApi) {
			return;
		}
		auto* reference = a_actor.actor.get();
		const bool female = reference->GetActorBase() && reference->GetActorBase()->GetSex() == RE::SEX::kFemale;
		bool changed = false;
		for (auto it = applied_.begin(); it != applied_.end();) {
			if (static_cast<ActorId>(it->first >> 3) != a_actorId) {
				++it;
				continue;
			}
			const auto region = static_cast<Region>(it->first & 0x7);
			const auto& profile = ProfileFor(region);
			const char* formatRaw = overlay->GetOverlayFormat(
				::IOverlayInterface::OverlayType::Normal,
				LocationFor(profile.domain));
			if (formatRaw && *formatRaw) {
				const std::string format = formatRaw;
				for (const auto& slot : it->second.slots) {
					removeSlot(reference, female, nodeNameForSlot(format, slot.slot), slot, region);
					changed = true;
				}
			}
			it = applied_.erase(it);
		}
		initializedActors_.erase(a_actorId);
		restoredDomains_.erase(a_actorId);
		if (changed) {
			overrideApi->SetNodeProperties(reference, true);
		}
	}

	void OverlayAdapter::clearActor(ActorId a_actorId)
	{
		for (auto it = applied_.begin(); it != applied_.end();) {
			if (static_cast<ActorId>(it->first >> 3) == a_actorId) {
				it = applied_.erase(it);
			} else {
				++it;
			}
		}
		initializedActors_.erase(a_actorId);
		restoredDomains_.erase(a_actorId);
	}

	void OverlayAdapter::clearSession(SessionEpoch)
	{
		applied_.clear();
		initializedActors_.clear();
		restoredDomains_.clear();
	}

	bool OverlayAdapter::resetSlotIfOurs(RE::TESObjectREFR* a_reference, bool a_female, const std::string& a_node, Region a_region)
	{
		std::string diffuse;
		if (!readDiffuse(a_reference, a_female, a_node, diffuse) || !isOurTexture(diffuse)) {
			return false;
		}
		AppliedSlot slot{};
		slot.diffuse = diffuse;
		removeSlot(a_reference, a_female, a_node, slot, a_region);
		return true;
	}

	void OverlayAdapter::clearTexture(
		RE::TESObjectREFR* a_reference,
		bool a_female,
		const std::string& a_node,
		std::uint8_t a_index)
	{
		// Removing only the override leaves the last texture in the material.
		// An empty path makes RaceMenu clear the material texture; the empty
		// override then acts as an explicit free marker for this slot.
		auto* overrideApi = static_cast<::IOverrideInterface*>(api_.overrideApi());
		StringVariant empty(std::string{});
		overrideApi->AddNodeOverride(a_reference, a_female, a_node.c_str(), kShaderTextureKey, a_index, empty);
	}

	void OverlayAdapter::removeSlot(
		RE::TESObjectREFR* a_reference,
		bool a_female,
		const std::string& a_node,
		const AppliedSlot& a_slot,
		Region a_region)
	{
		auto* overrideApi = static_cast<::IOverrideInterface*>(api_.overrideApi());
		const auto& profile = ProfileFor(a_region);
		std::string current;
		const bool differentDiffuse = readDiffuse(a_reference, a_female, a_node, current) && !current.empty() &&
			!isDefaultTexture(current) &&
			!equalsIgnoreCase(current, a_slot.diffuse);
		if (differentDiffuse && isOurTexture(current)) {
			return;
		}

		// RaceMenu/skee64/OverrideInterface.cpp::RemoveNodeOverride only erases
		// the map entry; reset owned values explicitly to restore the material.
		if (!differentDiffuse && !current.empty() && !isDefaultTexture(current)) {
			StringVariant diffuse(kDefaultDiffuse);
			overrideApi->AddNodeOverride(a_reference, a_female, a_node.c_str(), kShaderTextureKey, kTextureDiffuseIndex, diffuse);
		}
		const auto clearOwnedMap = [&](std::uint8_t a_index, const std::string& a_cached) {
			std::string live;
			if (readTexture(a_reference, a_female, a_node, a_index, live) && !live.empty() &&
				((!a_cached.empty() && equalsIgnoreCase(live, a_cached)) || (!differentDiffuse && isOurTexture(live)))) {
				clearTexture(a_reference, a_female, a_node, a_index);
			}
		};
		if (profile.hasNormal) {
			clearOwnedMap(kTextureNormalIndex, a_slot.normal);
		}
		if (profile.hasSpecular) {
			clearOwnedMap(kTextureSpecularIndex, a_slot.specular);
		}
		if (a_slot.hasAlpha) {
			FloatReader alpha;
			if (overrideApi->GetNodeOverride(a_reference, a_female, a_node.c_str(), kShaderAlphaKey, kAlphaIndex, alpha) &&
				alpha.present && alpha.value == a_slot.alpha) {
				FloatVariant opaque(1.0F);
				overrideApi->AddNodeOverride(a_reference, a_female, a_node.c_str(), kShaderAlphaKey, kAlphaIndex, opaque);
			}
		}
	}

	bool OverlayAdapter::readTexture(
		RE::TESObjectREFR* a_reference,
		bool a_female,
		const std::string& a_node,
		std::uint8_t a_index,
		std::string& a_value) const
	{
		auto* overrideApi = static_cast<::IOverrideInterface*>(api_.overrideApi());
		StringReader reader;
		if (!overrideApi->GetNodeOverride(a_reference, a_female, a_node.c_str(), kShaderTextureKey, a_index, reader)) {
			return false;
		}
		a_value = std::move(reader.value);
		return true;
	}

	bool OverlayAdapter::readDiffuse(RE::TESObjectREFR* a_reference, bool a_female, const std::string& a_node, std::string& a_value) const
	{
		return readTexture(a_reference, a_female, a_node, kTextureDiffuseIndex, a_value);
	}

	bool OverlayAdapter::priorSlotStillOurs(
		RE::TESObjectREFR* a_reference,
		bool a_female,
		const std::string& a_node,
		const AppliedSlot& a_prior,
		Region a_region) const
	{
		const auto& profile = ProfileFor(a_region);
		std::string diffuse;
		if (readDiffuse(a_reference, a_female, a_node, diffuse) && !diffuse.empty() &&
			!equalsIgnoreCase(diffuse, a_prior.diffuse)) {
			return false;
		}
		if (profile.hasNormal) {
			std::string normal;
			const bool hasNormal = readTexture(a_reference, a_female, a_node, kTextureNormalIndex, normal);
			const bool normalPresent = hasNormal && !normal.empty();
			if (a_prior.normal.empty() ? normalPresent : (normalPresent && !equalsIgnoreCase(normal, a_prior.normal))) {
				return false;
			}
		}
		if (profile.hasSpecular) {
			std::string specular;
			const bool hasSpecular = readTexture(a_reference, a_female, a_node, kTextureSpecularIndex, specular);
			const bool specularPresent = hasSpecular && !specular.empty();
			if (a_prior.specular.empty() ? specularPresent : (specularPresent && !equalsIgnoreCase(specular, a_prior.specular))) {
				return false;
			}
		}
		return true;
	}

	bool OverlayAdapter::slotAppearsFree(RE::TESObjectREFR* a_reference, bool a_female, const std::string& a_node, Region a_region) const
	{
		auto* overrideApi = static_cast<::IOverrideInterface*>(api_.overrideApi());
		const auto& profile = ProfileFor(a_region);

		std::string diffuse;
		const bool hasDiffuse = readDiffuse(a_reference, a_female, a_node, diffuse);
		if (hasDiffuse && !diffuse.empty()) {
			// After a load applied_ is empty but RaceMenu's co-save still
			// carries our old node overrides. Our own texture is reclaimable.
			if (isOurTexture(diffuse)) {
				return true;
			}
			if (!isDefaultTexture(diffuse)) {
				return false;
			}
		}

		// Key 6 is non-indexed; RaceMenu normalizes its index to 0xFF.
		if (overrideApi->HasNodeOverride(a_reference, a_female, a_node.c_str(), 6, 0xFF)) {
			return false;
		}
		std::string normal;
		if (profile.hasNormal &&
			readTexture(a_reference, a_female, a_node, kTextureNormalIndex, normal) && !normal.empty()) {
			return false;
		}
		std::string specular;
		if (profile.hasSpecular &&
			readTexture(a_reference, a_female, a_node, kTextureSpecularIndex, specular) && !specular.empty()) {
			return false;
		}
		return true;
	}

	bool OverlayAdapter::applySlot(
		RE::TESObjectREFR* a_reference,
		bool a_female,
		const std::string& a_node,
		const LayerVisual& a_layer,
		AppliedSlot& a_slot,
		const Settings& a_settings,
		Region a_region)
	{
		auto* overrideApi = static_cast<::IOverrideInterface*>(api_.overrideApi());
		const auto node = a_node.c_str();
		const auto& profile = ProfileFor(a_region);

		a_slot.textureIndex = a_layer.textureIndex;
		a_slot.variant = a_layer.variant;
		a_slot.diffuse = textureGamePath(a_region, a_layer.textureIndex, a_layer.variant, "");
		a_slot.normal.clear();
		a_slot.specular.clear();
		if (profile.hasNormal) {
			a_slot.normal = textureGamePath(a_region, a_layer.textureIndex, a_layer.variant, "_n");
		}
		if (profile.hasSpecular) {
			a_slot.specular = textureGamePath(a_region, a_layer.textureIndex, a_layer.variant, "_s");
		}

		std::error_code normalError;
		std::error_code specularError;
		if (profile.hasNormal &&
			!std::filesystem::exists(texturePath(a_region, a_layer.textureIndex, a_layer.variant, "_n"), normalError)) {
			a_slot.normal.clear();
		}
		if (profile.hasSpecular &&
			!std::filesystem::exists(texturePath(a_region, a_layer.textureIndex, a_layer.variant, "_s"), specularError)) {
			a_slot.specular.clear();
		}

		// Compare against the live node, not only the cached values: a 3D
		// rebuild or another mod may have removed a key while leaving the
		// diffuse intact. This also rewrites optional maps after a load where
		// no applied_ cache exists.
		std::string liveDiffuse;
		std::string liveNormal;
		std::string liveSpecular;
		const bool hasDiffuse = readDiffuse(a_reference, a_female, a_node, liveDiffuse);
		const bool hasNormal = profile.hasNormal &&
			readTexture(a_reference, a_female, a_node, kTextureNormalIndex, liveNormal);
		const bool hasSpecular = profile.hasSpecular &&
			readTexture(a_reference, a_female, a_node, kTextureSpecularIndex, liveSpecular);
		const bool diffuseChanged = !hasDiffuse || liveDiffuse.empty() ||
			!equalsIgnoreCase(liveDiffuse, a_slot.diffuse);
		const bool normalChanged = profile.hasNormal &&
			(a_slot.normal.empty() ?
				(hasNormal && !liveNormal.empty()) :
				(!hasNormal || liveNormal.empty() || !equalsIgnoreCase(liveNormal, a_slot.normal)));
		const bool specularChanged = profile.hasSpecular &&
			(a_slot.specular.empty() ?
				(hasSpecular && !liveSpecular.empty()) :
				(!hasSpecular || liveSpecular.empty() || !equalsIgnoreCase(liveSpecular, a_slot.specular)));
		const bool textureChanged = diffuseChanged || normalChanged || specularChanged;

		if (textureChanged) {
			StringVariant diffuse(a_slot.diffuse);
			overrideApi->AddNodeOverride(a_reference, a_female, node, kShaderTextureKey, kTextureDiffuseIndex, diffuse);

			if (profile.hasNormal && !a_slot.normal.empty()) {
				StringVariant normal(a_slot.normal);
				overrideApi->AddNodeOverride(a_reference, a_female, node, kShaderTextureKey, kTextureNormalIndex, normal);
			} else if (profile.hasNormal) {
				// Clear a map left over from another variant, including after a
				// load where no applied_ cache exists.
				clearTexture(a_reference, a_female, a_node, kTextureNormalIndex);
			}

			if (profile.hasSpecular && !a_slot.specular.empty()) {
				StringVariant specular(a_slot.specular);
				overrideApi->AddNodeOverride(a_reference, a_female, node, kShaderTextureKey, kTextureSpecularIndex, specular);
			} else if (profile.hasSpecular) {
				clearTexture(a_reference, a_female, a_node, kTextureSpecularIndex);
			}
		}

		const float opacity = std::clamp(a_settings.overlayOpacity, 0.0F, 1.0F);
		FloatVariant alpha(opacity);
		overrideApi->AddNodeOverride(a_reference, a_female, node, kShaderAlphaKey, kAlphaIndex, alpha);
		a_slot.hasAlpha = true;
		a_slot.alpha = opacity;
		return true;
	}

	std::string OverlayAdapter::makeSignature(const RegionTarget& a_target, const std::vector<Assignment>& a_assignments, const std::string& a_format, float a_opacity) const
	{
		std::string signature = a_format;
		signature += '|';
		signature += std::to_string(static_cast<int>(a_target.region));
		signature += '|';
		signature += std::to_string(a_opacity);
		for (const auto& assignment : a_assignments) {
			signature += '|';
			signature += std::to_string(assignment.slot);
			signature += ':';
			signature += std::to_string(assignment.layer->layerId);
			signature += ':';
			signature += std::to_string(assignment.layer->textureIndex);
			signature += ':';
			signature += std::to_string(static_cast<int>(assignment.layer->variant));
		}
		return signature;
	}

	std::string OverlayAdapter::nodeNameForSlot(const std::string& a_format, std::uint8_t a_slot) const
	{
		const auto placeholder = a_format.find("{}");
		if (placeholder == std::string::npos) {
			return a_format;
		}
		auto result = a_format;
		result.replace(placeholder, 2, std::to_string(a_slot));
		return result;
	}

	std::filesystem::path OverlayAdapter::texturePath(Region a_region, std::uint16_t a_index, TextureVariant a_variant, const char* a_suffix) const
	{
		const auto& profile = ProfileFor(a_region);
		std::string name;
		if (a_variant == TextureVariant::Mega) {
			name = "m_";
		}
		name += profile.prefix;
		name += std::to_string(a_index);
		name += a_suffix;
		name += ".dds";
		return dependencies_.dataRoot() / "Textures" / "slacs2" / profile.folder / name;
	}

	std::string OverlayAdapter::textureGamePath(Region a_region, std::uint16_t a_index, TextureVariant a_variant, const char* a_suffix) const
	{
		const auto& profile = ProfileFor(a_region);
		std::string path = "textures\\slacs2\\";
		path += profile.folder;
		path += '\\';
		if (a_variant == TextureVariant::Mega) {
			path += "m_";
		}
		path += profile.prefix;
		path += std::to_string(a_index);
		path += a_suffix;
		path += ".dds";
		return path;
	}
}
