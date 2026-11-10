#pragma once

#include "config/Settings.h"
#include "core/Types.h"
#include "integration/RaceMenuApi.h"
#include "runtime/ActorRegistry.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace slacs
{
	enum class OverlayStatus : std::uint8_t
	{
		Applied,
		NoChange,
		DeferredNo3D,
		ApiUnavailable,
		NoOwnedCapacity,
		StaleRevision,
		InvalidTarget,
		Failed,
	};

	enum class OverlayErrorCode : std::uint8_t
	{
		None,
		AbiMismatch,
		MissingProfile,
		NoOwnedSlot,
		InvalidTarget,
		StaleRevision,
		ApiFailure,
	};

	enum class VisualInvalidation : std::uint8_t
	{
		RaceMenuReady,
		Actor3DRebuilt,
		CellChanged,
	};

	struct OverlayHandshake
	{
		enum class Status : std::uint8_t
		{
			Available,
			VersionMismatch,
			Unavailable,
		};

		Status status{ Status::Unavailable };
		std::uint32_t apiVersion{};
		OverlayErrorCode error{ OverlayErrorCode::AbiMismatch };
	};

	struct OverlayResult
	{
		OverlayStatus status{ OverlayStatus::Failed };
		ActorId actor{};
		Region region{ Region::BodyVaginal };
		Revision revision{};
		std::uint32_t slotsApplied{};
		OverlayErrorCode error{ OverlayErrorCode::None };
	};

	struct OverlayDependencies
	{
		std::function<std::filesystem::path()> dataRoot;
		std::function<SessionEpoch()> epoch;
		std::function<void(FormId, SessionEpoch)> installed;
	};

	class OverlayAdapter
	{
	public:
		OverlayAdapter();
		~OverlayAdapter();
		OverlayAdapter(const OverlayAdapter&) = delete;
		OverlayAdapter& operator=(const OverlayAdapter&) = delete;

		void setDependencies(OverlayDependencies a_dependencies);
		OverlayHandshake initialize();
		OverlayResult reconcile(const ActorLease& a_actor, const RegionTarget& a_target, const Settings& a_settings);
		void invalidateActor(ActorId a_actor, VisualInvalidation a_reason);
		void clearActor(const ActorLease& a_actor, ActorId a_actorId);
		void clearActor(ActorId a_actorId);
		void clearSession(SessionEpoch a_epoch);

		[[nodiscard]] bool available() const;
		[[nodiscard]] std::array<std::uint32_t, 4> capacities() const;

	private:
		struct AppliedSlot
		{
			std::uint8_t slot{};
			std::uint64_t layerId{};
			std::uint16_t textureIndex{};
			TextureVariant variant{ TextureVariant::Normal };
			std::string diffuse;
			std::string normal;
			std::string specular;
			bool hasAlpha{};
			float alpha{};
		};

		struct AppliedRegion
		{
			Revision revision{};
			std::string signature;
			std::vector<AppliedSlot> slots;
		};

		struct SlotSnapshot
		{
			bool diffuseRead{};
			bool hasDiffuse{};
			std::string diffuse;
			bool normalRead{};
			bool hasNormal{};
			std::string normal;
			bool specularRead{};
			bool hasSpecular{};
			std::string specular;
			bool materialRead{};
			bool hasMaterial{};
			bool alphaRead{};
			bool hasAlpha{};
			float alpha{};
		};

		struct Assignment
		{
			const LayerVisual* layer{};
			std::uint8_t slot{};
			const AppliedSlot* prior{};
		};

		static void installCallback(RE::TESObjectREFR* a_reference, RE::NiAVObject* a_root);

		void removeSlot(RE::TESObjectREFR* a_reference, bool a_female, const std::string& a_node, const AppliedSlot& a_slot, Region a_region);
		bool resetSlotIfOurs(RE::TESObjectREFR* a_reference, bool a_female, const std::string& a_node, Region a_region);
		void clearTexture(RE::TESObjectREFR* a_reference, bool a_female, const std::string& a_node, std::uint8_t a_index);
		bool readTexture(RE::TESObjectREFR* a_reference, bool a_female, const std::string& a_node, std::uint8_t a_index, std::string& a_value) const;
		bool readDiffuse(RE::TESObjectREFR* a_reference, bool a_female, const std::string& a_node, std::string& a_value) const;
		bool priorSlotStillOurs(const SlotSnapshot& a_snapshot, const AppliedSlot& a_prior, Region a_region) const;
		bool applySlot(
			RE::TESObjectREFR* a_reference,
			bool a_female,
			const std::string& a_node,
			SlotSnapshot& a_snapshot,
			const LayerVisual& a_layer,
			AppliedSlot& a_slot,
			bool& a_changed,
			const Settings& a_settings,
			Region a_region);
		std::string makeSignature(const RegionTarget& a_target, const std::vector<Assignment>& a_assignments, const std::string& a_format, float a_opacity) const;
		std::string nodeNameForSlot(const std::string& a_format, std::uint8_t a_slot) const;
		std::filesystem::path texturePath(Region a_region, std::uint16_t a_index, TextureVariant a_variant, const char* a_suffix) const;
		std::string textureGamePath(Region a_region, std::uint16_t a_index, TextureVariant a_variant, const char* a_suffix) const;

		OverlayDependencies dependencies_;
		RaceMenuApi api_;
		std::unordered_map<std::uint64_t, AppliedRegion> applied_;
		std::unordered_map<ActorId, SessionEpoch> initializedActors_;
		std::unordered_map<ActorId, std::uint8_t> restoredDomains_;
	};
}
