#include "integration/OverlayAdapter.h"

#include "integration/racemenu/IPluginInterface.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <condition_variable>
#include <mutex>
#include <string_view>
#include <utility>

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
		constexpr std::uint32_t kMaxOverlayCount = 127;
		constexpr const char* kDefaultDiffuse = "textures\\actors\\character\\overlays\\default.dds";

		struct RegionProfile
		{
			const char* folder;
			const char* prefix;
			std::uint16_t textureCount;
			bool hasNormal;
			bool hasSpecular;
			OverlayDomain domain;
		};

		constexpr std::array<RegionProfile, kRegionCount> kRegionProfiles{ {
			{ "Vaginal", "body", 12, true, true, OverlayDomain::Body },
			{ "Anal", "body", 12, true, true, OverlayDomain::Body },
			{ "Breast", "chest", 12, true, true, OverlayDomain::Body },
			{ "Footjob", "legs", 4, false, false, OverlayDomain::Body },
			{ "Oral", "head", 12, true, false, OverlayDomain::Face },
			{ "Handjob", "hands", 4, false, false, OverlayDomain::Hands },
			{ "Footjob", "feet", 4, false, false, OverlayDomain::Feet },
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

		std::uint32_t OverlayCount(::IOverlayInterface* a_overlay, OverlayDomain a_domain)
		{
			if (!a_overlay) {
				return 0;
			}
			return std::min<std::uint32_t>(
				a_overlay->GetOverlayCount(::IOverlayInterface::OverlayType::Normal, LocationFor(a_domain)),
				kMaxOverlayCount);
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

		struct InstallCallbackGate
		{
			std::mutex mutex;
			std::condition_variable drained;
			OverlayAdapter* owner{};
			std::size_t active{};
			bool accepting{};
		};

		// Runtime is constructed after namespace-scope objects, so its destructor can
		// drain this gate before the gate itself is destroyed during process teardown.
		InstallCallbackGate g_installCallbackGate;

		void closeInstallCallbackGate(OverlayAdapter* a_owner)
		{
			std::lock_guard lock(g_installCallbackGate.mutex);
			if (g_installCallbackGate.owner == a_owner) {
				g_installCallbackGate.accepting = false;
				g_installCallbackGate.owner = nullptr;
			}
		}

		void waitForInstallCallbacks()
		{
			std::unique_lock lock(g_installCallbackGate.mutex);
			g_installCallbackGate.drained.wait(lock, [] { return g_installCallbackGate.active == 0; });
		}
	}

	OverlayAdapter::OverlayAdapter() = default;

	OverlayAdapter::~OverlayAdapter()
	{
		shutdown();
	}

	void OverlayAdapter::setDependencies(OverlayDependencies a_dependencies)
	{
		dependencies_ = std::move(a_dependencies);
	}

	OverlayHandshake OverlayAdapter::initialize()
	{
		std::lock_guard lifecycleLock(callbackLifecycleMutex_);
		OverlayHandshake handshake;
		if (shutdown_) {
			handshake.error = OverlayErrorCode::ApiFailure;
			return handshake;
		}
		if (callbackRegistered_) {
			handshake.status = OverlayHandshake::Status::Available;
			handshake.apiVersion = api_.version();
			handshake.error = OverlayErrorCode::None;
			return handshake;
		}
		if (!api_.initialize()) {
			handshake.status = api_.version() > 0 ? OverlayHandshake::Status::VersionMismatch : OverlayHandshake::Status::Unavailable;
			handshake.apiVersion = api_.version();
			handshake.error = OverlayErrorCode::AbiMismatch;
			return handshake;
		}

		handshake.status = OverlayHandshake::Status::Available;
		handshake.apiVersion = api_.version();
		handshake.error = OverlayErrorCode::None;

		auto* overlay = static_cast<::IOverlayInterface*>(api_.overlay());
		bool gateClaimed{};
		{
			std::lock_guard gateLock(g_installCallbackGate.mutex);
			if (!g_installCallbackGate.owner || g_installCallbackGate.owner == this) {
				g_installCallbackGate.owner = this;
				g_installCallbackGate.accepting = true;
				gateClaimed = true;
			}
		}
		if (!gateClaimed) {
			logs::warn("RaceMenu install callback registration failed (another adapter owns the callback gate)");
		} else if (!overlay->RegisterInstallCallback("SLACS2NG", &OverlayAdapter::installCallback)) {
			closeInstallCallbackGate(this);
			waitForInstallCallbacks();
			logs::warn("RaceMenu install callback registration failed (key may already be in use)");
		} else {
			callbackRegistered_ = true;
		}
		logs::info("Overlay texture profile UBE is unavailable (textures/slacs2_ube is missing)");
		return handshake;
	}

	void OverlayAdapter::shutdown() noexcept
	{
		std::lock_guard lifecycleLock(callbackLifecycleMutex_);
		if (shutdown_) {
			return;
		}
		shutdown_ = true;

		closeInstallCallbackGate(this);
		const bool unregisterCallback = std::exchange(callbackRegistered_, false);
		auto* overlay = unregisterCallback ? static_cast<::IOverlayInterface*>(api_.overlay()) : nullptr;
		if (overlay) {
			// RaceMenu may hold its callback-dispatch lock here. Never keep our gate
			// locked across this call; an entered callback must be able to retire.
			(void)overlay->UnregisterInstallCallback("SLACS2NG");
		}
		waitForInstallCallbacks();
	}

	bool OverlayAdapter::available() const
	{
		return api_.available();
	}

	std::array<std::uint32_t, 4> OverlayAdapter::capacities() const
	{
		if (!api_.available()) {
			return {};
		}
		auto* overlay = static_cast<::IOverlayInterface*>(api_.overlay());
		return {
			OverlayCount(overlay, OverlayDomain::Body),
			OverlayCount(overlay, OverlayDomain::Face),
			OverlayCount(overlay, OverlayDomain::Hands),
			OverlayCount(overlay, OverlayDomain::Feet),
		};
	}

	void OverlayAdapter::installCallback(RE::TESObjectREFR* a_reference, RE::NiAVObject*)
	{
		OverlayAdapter* self{};
		{
			std::lock_guard lock(g_installCallbackGate.mutex);
			if (!g_installCallbackGate.accepting || !g_installCallbackGate.owner) {
				return;
			}
			self = g_installCallbackGate.owner;
			++g_installCallbackGate.active;
		}
		struct CallbackExit
		{
			~CallbackExit()
			{
				std::lock_guard lock(g_installCallbackGate.mutex);
				if (--g_installCallbackGate.active == 0) {
					g_installCallbackGate.drained.notify_all();
				}
			}
		} callbackExit;

		if (!a_reference) {
			return;
		}
		const auto installed = self->dependencies_.installed;
		if (!installed) {
			return;
		}
		const auto formId = a_reference->GetFormID();
		if (formId == 0) {
			return;
		}
		const auto epoch = self->dependencies_.epoch;
		installed(formId, epoch ? epoch() : 0);
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

		const auto configured = OverlayCount(overlay, profile.domain);
		const std::size_t availableSlots = configured;
		std::vector<std::string> nodeNames;
		nodeNames.reserve(availableSlots);
		for (std::size_t slotIndex = 0; slotIndex < availableSlots; ++slotIndex) {
			nodeNames.push_back(nodeNameForSlot(format, static_cast<std::uint8_t>(slotIndex)));
		}

		const std::uint64_t key = (a_target.actor << 3) | static_cast<std::uint64_t>(RegionIndex(a_target.region));
		const auto cached = applied_.find(key);
		if (cached != applied_.end() && a_target.revision < cached->second.revision) {
			return failure(a_target.actor, a_target.region, a_target.revision, OverlayStatus::StaleRevision, OverlayErrorCode::StaleRevision);
		}

		std::vector<std::uint8_t> reservedByOther(availableSlots, 0);
		for (const auto& [otherKey, otherRegion] : applied_) {
			if (otherKey == key || static_cast<ActorId>(otherKey >> 3) != a_target.actor) {
				continue;
			}
			const auto otherRegionIndex = static_cast<std::size_t>(otherKey & 0x7);
			if (otherRegionIndex >= kRegionCount ||
				ProfileFor(static_cast<Region>(otherRegionIndex)).domain != profile.domain) {
				continue;
			}
			for (const auto& owned : otherRegion.slots) {
				if (owned.slot < reservedByOther.size()) {
					reservedByOther[owned.slot] = 1;
				}
			}
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
					removed |= resetSlotIfOurs(reference, female, nodeNames[slot], cleanupRegions[domainIndex]);
			}
			if (removed) overrideApi->SetNodeProperties(reference, true);
			restored |= domainBit;
		}

		if (!a_settings.overlaysEnabled) {
			bool removed = false;
			if (cached != applied_.end()) {
				for (const auto& slot : cached->second.slots) {
						const auto node = slot.slot < nodeNames.size() ? nodeNames[slot.slot] : nodeNameForSlot(format, slot.slot);
						removeSlot(reference, female, node, slot, a_target.region);
					removed = true;
				}
				applied_.erase(cached);
			}
			for (std::size_t slotIndex = 0; slotIndex < availableSlots; ++slotIndex) {
				const auto slot = static_cast<std::uint8_t>(slotIndex);
					if (reservedByOther[slot] != 0) {
					continue;
				}
					if (resetSlotIfOurs(reference, female, nodeNames[slot], a_target.region)) {
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
		bool materialDrift = false;
		const auto* previous = cached != applied_.end() ? &cached->second : nullptr;
		std::vector<SlotSnapshot> snapshots(availableSlots);

		const auto ensureDiffuse = [&](std::size_t a_slot) -> SlotSnapshot& {
			auto& snapshot = snapshots[a_slot];
			if (!snapshot.diffuseRead) {
				snapshot.hasDiffuse = readDiffuse(reference, female, nodeNames[a_slot], snapshot.diffuse);
				snapshot.diffuseRead = true;
			}
			return snapshot;
		};
		const auto ensureNormal = [&](std::size_t a_slot) -> SlotSnapshot& {
			auto& snapshot = snapshots[a_slot];
			if (!snapshot.normalRead) {
				snapshot.hasNormal = readTexture(reference, female, nodeNames[a_slot], kTextureNormalIndex, snapshot.normal);
				snapshot.normalRead = true;
			}
			return snapshot;
		};
		const auto ensureSpecular = [&](std::size_t a_slot) -> SlotSnapshot& {
			auto& snapshot = snapshots[a_slot];
			if (!snapshot.specularRead) {
				snapshot.hasSpecular = readTexture(reference, female, nodeNames[a_slot], kTextureSpecularIndex, snapshot.specular);
				snapshot.specularRead = true;
			}
			return snapshot;
		};
		const auto ensureMaterial = [&](std::size_t a_slot) -> SlotSnapshot& {
			auto& snapshot = snapshots[a_slot];
			if (!snapshot.materialRead) {
				snapshot.hasMaterial = overrideApi->HasNodeOverride(reference, female, nodeNames[a_slot].c_str(), 6, 0xFF);
				snapshot.materialRead = true;
			}
			return snapshot;
		};
		const auto ensureAlpha = [&](std::size_t a_slot) -> SlotSnapshot& {
			auto& snapshot = snapshots[a_slot];
			if (!snapshot.alphaRead) {
				FloatReader alpha;
				snapshot.hasAlpha = overrideApi->GetNodeOverride(reference, female, nodeNames[a_slot].c_str(), kShaderAlphaKey, kAlphaIndex, alpha) && alpha.present;
				snapshot.alpha = alpha.value;
				snapshot.alphaRead = true;
			}
			return snapshot;
		};
		const auto appearsFree = [&](std::size_t a_slot) {
			auto& snapshot = ensureDiffuse(a_slot);
			if (snapshot.hasDiffuse && !snapshot.diffuse.empty()) {
				if (isOurTexture(snapshot.diffuse)) return true;
				if (!isDefaultTexture(snapshot.diffuse)) return false;
			}
			if (ensureMaterial(a_slot).hasMaterial) return false;
			if (profile.hasNormal && !ensureNormal(a_slot).normal.empty()) return false;
			if (profile.hasSpecular && !ensureSpecular(a_slot).specular.empty()) return false;
			return true;
		};
		const auto matchesAppliedMaterial = [&](std::size_t a_slot, const AppliedSlot& a_prior) {
			auto& snapshot = ensureDiffuse(a_slot);
			if (!snapshot.hasDiffuse || !equalsIgnoreCase(snapshot.diffuse, a_prior.diffuse)) return false;
			if (profile.hasNormal) {
				auto& normal = ensureNormal(a_slot);
				if (a_prior.normal.empty() ?
					(normal.hasNormal && !normal.normal.empty()) :
					(!normal.hasNormal || !equalsIgnoreCase(normal.normal, a_prior.normal))) {
					return false;
				}
			}
			if (profile.hasSpecular) {
				auto& specular = ensureSpecular(a_slot);
				if (a_prior.specular.empty() ?
					(specular.hasSpecular && !specular.specular.empty()) :
					(!specular.hasSpecular || !equalsIgnoreCase(specular.specular, a_prior.specular))) {
					return false;
				}
			}
			if (a_prior.hasAlpha) {
				const auto& alpha = ensureAlpha(a_slot);
				if (!alpha.hasAlpha || alpha.alpha != a_prior.alpha) return false;
			}
			return true;
		};

		if (!a_target.layers.empty()) {
			std::vector<const AppliedSlot*> priorBySlot(availableSlots, nullptr);
			if (previous) {
				for (const auto& slot : previous->slots) {
					if (slot.slot < priorBySlot.size()) priorBySlot[slot.slot] = &slot;
				}
			}
			std::vector<const AppliedSlot*> priorByLayer(a_target.layers.size(), nullptr);
			if (previous) {
				for (std::size_t layerIndex = 0; layerIndex < a_target.layers.size(); ++layerIndex) {
					for (const auto& slot : previous->slots) {
						if (slot.layerId == a_target.layers[layerIndex].layerId) {
							priorByLayer[layerIndex] = &slot;
							break;
						}
					}
				}
			}

			// A fully applied, unchanged target can be validated through its prior
			// slots alone. Partial targets still rescan the pool so released slots
			// remain retryable at the same revision.
			if (previous && cached->second.revision == a_target.revision &&
				previous->slots.size() == a_target.layers.size()) {
				std::vector<Assignment> stable;
				stable.reserve(a_target.layers.size());
				std::vector<std::uint8_t> seen(availableSlots, 0);
				bool allStable = true;
				for (std::size_t layerIndex = a_target.layers.size(); layerIndex-- > 0;) {
					const auto* prior = priorByLayer[layerIndex];
					if (!prior || prior->slot >= availableSlots || reservedByOther[prior->slot] || seen[prior->slot]) {
						allStable = false;
						break;
					}
					auto& snapshot = ensureDiffuse(prior->slot);
					if (!matchesAppliedMaterial(prior->slot, *prior)) {
						materialDrift = priorSlotStillOurs(snapshot, *prior, a_target.region);
						allStable = false;
						break;
					}
					seen[prior->slot] = 1;
					stable.push_back(Assignment{ &a_target.layers[layerIndex], prior->slot, prior });
				}
				if (allStable && makeSignature(a_target, stable, format, std::clamp(a_settings.overlayOpacity, 0.0F, 1.0F)) == cached->second.signature) {
					return failure(a_target.actor, a_target.region, a_target.revision, OverlayStatus::NoChange, OverlayErrorCode::None);
				}
			}

			std::vector<std::uint8_t> usableBySlot(availableSlots, 0);
			std::vector<std::uint8_t> usableSlots;
			usableSlots.reserve(availableSlots);
			for (std::size_t slotIndex = 0; slotIndex < availableSlots; ++slotIndex) {
				if (reservedByOther[slotIndex]) continue;
				const auto* prior = priorBySlot[slotIndex];
				bool usable = false;
				if (prior) {
					auto& snapshot = ensureDiffuse(slotIndex);
					if (profile.hasNormal) (void)ensureNormal(slotIndex);
					if (profile.hasSpecular) (void)ensureSpecular(slotIndex);
					usable = priorSlotStillOurs(snapshot, *prior, a_target.region);
					if (usable && !matchesAppliedMaterial(slotIndex, *prior)) materialDrift = true;
				}
				if (!usable) usable = appearsFree(slotIndex);
				if (usable) {
					usableBySlot[slotIndex] = 1;
					usableSlots.push_back(static_cast<std::uint8_t>(slotIndex));
				}
			}

			const auto assignmentCount = std::min(a_target.layers.size(), usableSlots.size());
			const auto firstLayer = a_target.layers.size() - assignmentCount;
			partial = assignmentCount < a_target.layers.size();
			std::vector<std::int16_t> slotForLayer(a_target.layers.size(), -1);
			std::vector<const AppliedSlot*> priorForLayer(a_target.layers.size(), nullptr);
			std::vector<std::uint8_t> usedSlot(availableSlots, 0);
			for (std::size_t layerIndex = a_target.layers.size(); layerIndex-- > firstLayer;) {
				const auto* prior = priorByLayer[layerIndex];
				if (!prior || prior->slot >= availableSlots || !usableBySlot[prior->slot] || usedSlot[prior->slot]) continue;
				slotForLayer[layerIndex] = prior->slot;
				priorForLayer[layerIndex] = prior;
				usedSlot[prior->slot] = 1;
			}
			for (std::size_t layerIndex = a_target.layers.size(); layerIndex-- > firstLayer;) {
				if (slotForLayer[layerIndex] >= 0) continue;
				for (const auto slot : usableSlots) {
					if (usedSlot[slot]) continue;
					slotForLayer[layerIndex] = slot;
					priorForLayer[layerIndex] = priorBySlot[slot];
					usedSlot[slot] = 1;
					break;
				}
			}
			for (std::size_t layerIndex = a_target.layers.size(); layerIndex-- > firstLayer;) {
				if (slotForLayer[layerIndex] >= 0) {
					assignments.push_back(Assignment{ &a_target.layers[layerIndex], static_cast<std::uint8_t>(slotForLayer[layerIndex]), priorForLayer[layerIndex] });
				}
			}
		}

		const float opacity = std::clamp(a_settings.overlayOpacity, 0.0F, 1.0F);
		const auto signature = makeSignature(a_target, assignments, format, opacity);
		if (cached != applied_.end() && cached->second.revision == a_target.revision && cached->second.signature == signature) {
			if (!partial && !materialDrift) {
				return failure(a_target.actor, a_target.region, a_target.revision, OverlayStatus::NoChange, OverlayErrorCode::None);
			}
			if (partial && !materialDrift) {
				OverlayResult result;
				result.status = OverlayStatus::NoOwnedCapacity;
				result.actor = a_target.actor;
				result.region = a_target.region;
				result.revision = a_target.revision;
				result.slotsApplied = static_cast<std::uint32_t>(assignments.size());
				result.error = OverlayErrorCode::NoOwnedSlot;
				return result;
			}
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
						const auto node = slot.slot < nodeNames.size() ? nodeNames[slot.slot] : nodeNameForSlot(format, slot.slot);
						removeSlot(reference, female, node, slot, a_target.region);
					changed = true;
				}
			}
		}

		// After a load applied_ is empty while RaceMenu's co-save still holds
		// our old node overrides. Clear any slot whose diffuse still points at
		// this plugin's texture but which the new logical state does not use.
		for (std::size_t slotIndex = 0; slotIndex < availableSlots; ++slotIndex) {
			const auto slot = static_cast<std::uint8_t>(slotIndex);
				if (reservedByOther[slotIndex] != 0) {
				continue;
			}
			const bool assigned = std::any_of(
				assignments.begin(),
				assignments.end(),
				[&](const Assignment& assignment) { return assignment.slot == slot; });
			if (assigned) {
				continue;
			}
				auto& snapshot = snapshots[slotIndex];
				if (!snapshot.diffuseRead) {
					snapshot.hasDiffuse = readDiffuse(reference, female, nodeNames[slotIndex], snapshot.diffuse);
					snapshot.diffuseRead = true;
				}
				if (snapshot.hasDiffuse && isOurTexture(snapshot.diffuse)) {
					AppliedSlot cleared{};
					cleared.diffuse = snapshot.diffuse;
					removeSlot(reference, female, nodeNames[slotIndex], cleared, a_target.region);
				changed = true;
			}
		}

		AppliedRegion updated;
		updated.revision = a_target.revision;
		updated.signature = signature;
		updated.slots.reserve(assignments.size());

		for (const auto& assignment : assignments) {
			const auto& node = nodeNames[assignment.slot];
			AppliedSlot slot;
			bool slotChanged = false;
			if (!applySlot(reference, female, node, snapshots[assignment.slot], *assignment.layer, slot, slotChanged, a_settings, a_target.region)) {
				return failure(a_target.actor, a_target.region, a_target.revision, OverlayStatus::Failed, OverlayErrorCode::ApiFailure);
			}
			slot.slot = assignment.slot;
			slot.layerId = assignment.layer->layerId;
			updated.slots.push_back(std::move(slot));
			changed = changed || slotChanged;
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

	bool OverlayAdapter::priorSlotStillOurs(const SlotSnapshot& a_snapshot, const AppliedSlot& a_prior, Region a_region) const
	{
		const auto& profile = ProfileFor(a_region);
		if (a_snapshot.hasDiffuse && !a_snapshot.diffuse.empty() &&
			!equalsIgnoreCase(a_snapshot.diffuse, a_prior.diffuse)) {
			return false;
		}
		if (profile.hasNormal) {
			const bool normalPresent = a_snapshot.hasNormal && !a_snapshot.normal.empty();
			if (a_prior.normal.empty() ? normalPresent : (normalPresent && !equalsIgnoreCase(a_snapshot.normal, a_prior.normal))) {
				return false;
			}
		}
		if (profile.hasSpecular) {
			const bool specularPresent = a_snapshot.hasSpecular && !a_snapshot.specular.empty();
			if (a_prior.specular.empty() ? specularPresent : (specularPresent && !equalsIgnoreCase(a_snapshot.specular, a_prior.specular))) {
				return false;
			}
		}
		return true;
	}

	bool OverlayAdapter::applySlot(
		RE::TESObjectREFR* a_reference,
		bool a_female,
		const std::string& a_node,
		SlotSnapshot& a_snapshot,
		const LayerVisual& a_layer,
		AppliedSlot& a_slot,
		bool& a_changed,
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

		// Reuse the slot observation gathered during allocation. If an owned slot
		// was found through its diffuse alone, read the remaining maps here once.
		if (!a_snapshot.diffuseRead) {
			a_snapshot.hasDiffuse = readDiffuse(a_reference, a_female, a_node, a_snapshot.diffuse);
			a_snapshot.diffuseRead = true;
		}
		if (profile.hasNormal && !a_snapshot.normalRead) {
			a_snapshot.hasNormal = readTexture(a_reference, a_female, a_node, kTextureNormalIndex, a_snapshot.normal);
			a_snapshot.normalRead = true;
		}
		if (profile.hasSpecular && !a_snapshot.specularRead) {
			a_snapshot.hasSpecular = readTexture(a_reference, a_female, a_node, kTextureSpecularIndex, a_snapshot.specular);
			a_snapshot.specularRead = true;
		}
		const bool diffuseChanged = !a_snapshot.hasDiffuse || a_snapshot.diffuse.empty() ||
			!equalsIgnoreCase(a_snapshot.diffuse, a_slot.diffuse);
		const bool normalChanged = profile.hasNormal &&
			(a_slot.normal.empty() ?
				(a_snapshot.hasNormal && !a_snapshot.normal.empty()) :
				(!a_snapshot.hasNormal || a_snapshot.normal.empty() || !equalsIgnoreCase(a_snapshot.normal, a_slot.normal)));
		const bool specularChanged = profile.hasSpecular &&
			(a_slot.specular.empty() ?
				(a_snapshot.hasSpecular && !a_snapshot.specular.empty()) :
				(!a_snapshot.hasSpecular || a_snapshot.specular.empty() || !equalsIgnoreCase(a_snapshot.specular, a_slot.specular)));
		const bool textureChanged = diffuseChanged || normalChanged || specularChanged;

		if (textureChanged) {
			StringVariant diffuse(a_slot.diffuse);
			overrideApi->AddNodeOverride(a_reference, a_female, node, kShaderTextureKey, kTextureDiffuseIndex, diffuse);
			a_snapshot.diffuse = a_slot.diffuse;
			a_snapshot.hasDiffuse = true;

			if (profile.hasNormal && !a_slot.normal.empty()) {
				StringVariant normal(a_slot.normal);
				overrideApi->AddNodeOverride(a_reference, a_female, node, kShaderTextureKey, kTextureNormalIndex, normal);
				a_snapshot.normal = a_slot.normal;
				a_snapshot.hasNormal = true;
			} else if (profile.hasNormal) {
				// Clear a map left over from another variant, including after a
				// load where no applied_ cache exists.
				clearTexture(a_reference, a_female, a_node, kTextureNormalIndex);
				a_snapshot.normal.clear();
				a_snapshot.hasNormal = true;
			}

			if (profile.hasSpecular && !a_slot.specular.empty()) {
				StringVariant specular(a_slot.specular);
				overrideApi->AddNodeOverride(a_reference, a_female, node, kShaderTextureKey, kTextureSpecularIndex, specular);
				a_snapshot.specular = a_slot.specular;
				a_snapshot.hasSpecular = true;
			} else if (profile.hasSpecular) {
				clearTexture(a_reference, a_female, a_node, kTextureSpecularIndex);
				a_snapshot.specular.clear();
				a_snapshot.hasSpecular = true;
			}
		}

		const float opacity = std::clamp(a_settings.overlayOpacity, 0.0F, 1.0F);
		if (!a_snapshot.alphaRead) {
			FloatReader liveAlpha;
			a_snapshot.hasAlpha = overrideApi->GetNodeOverride(a_reference, a_female, node, kShaderAlphaKey, kAlphaIndex, liveAlpha) && liveAlpha.present;
			a_snapshot.alpha = liveAlpha.value;
			a_snapshot.alphaRead = true;
		}
		const bool alphaChanged = !a_snapshot.hasAlpha || a_snapshot.alpha != opacity;
		if (alphaChanged) {
			FloatVariant alpha(opacity);
			overrideApi->AddNodeOverride(a_reference, a_female, node, kShaderAlphaKey, kAlphaIndex, alpha);
			a_snapshot.alpha = opacity;
			a_snapshot.hasAlpha = true;
		}
		a_slot.hasAlpha = true;
		a_slot.alpha = opacity;
		a_changed = textureChanged || alphaChanged;
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
