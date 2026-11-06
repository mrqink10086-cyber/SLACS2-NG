#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace slacs
{
	using ActorId = std::uint64_t;
	using FormId = std::uint32_t;
	using SessionEpoch = std::uint64_t;
	using RequestId = std::uint64_t;
	using IngressSequence = std::uint64_t;
	using Revision = std::uint64_t;
	using DeadlineId = std::uint64_t;
	using Tid = std::int32_t;
	using GameHours = double;
	using ActiveSeconds = double;
	using SteadyTime = std::chrono::steady_clock::time_point;

	inline constexpr ActorId kInvalidActorId = 0;
	inline constexpr FormId kInvalidFormId = 0;

	struct GameTime
	{
		GameHours calendarHours{};
		ActiveSeconds activeSeconds{};
	};

	enum class Region : std::uint8_t
	{
		BodyVaginal,
		BodyAnal,
		BodyBreast,
		BodyLegs,
		Face,
		Hands,
		Feet,
	};

	inline constexpr std::size_t kRegionCount = 7;

	inline constexpr std::array<Region, kRegionCount> kRegions{
		Region::BodyVaginal,
		Region::BodyAnal,
		Region::BodyBreast,
		Region::BodyLegs,
		Region::Face,
		Region::Hands,
		Region::Feet,
	};

	constexpr std::size_t RegionIndex(Region a_region) noexcept
	{
		return static_cast<std::size_t>(a_region);
	}

	constexpr std::string_view RegionName(Region a_region) noexcept
	{
		switch (a_region) {
		case Region::BodyVaginal:
			return "BodyVaginal";
		case Region::BodyAnal:
			return "BodyAnal";
		case Region::BodyBreast:
			return "BodyBreast";
		case Region::BodyLegs:
			return "BodyLegs";
		case Region::Face:
			return "Face";
		case Region::Hands:
			return "Hands";
		case Region::Feet:
			return "Feet";
		}
		return "Unknown";
	}

	enum class TextureVariant : std::uint8_t
	{
		Normal,
		Mega,
	};

	struct LayerRecord
	{
		std::uint64_t id{};
		std::uint16_t textureIndex{};
		TextureVariant variant{ TextureVariant::Normal };
	};

	struct RegionState
	{
		std::vector<LayerRecord> layers;
		double decayRemainderGameHours{};
		Revision revision{};
	};

	struct ActorState
	{
		ActorId actor{};
		FormId formId{};
		std::array<RegionState, kRegionCount> regions{};
		double lastSettledGameHours{};
		std::optional<ActiveSeconds> timedClearAt;
		Revision revision{};
	};

	struct InteractionTarget
	{
		ActorId actor{};
		std::vector<Region> regions;
		TextureVariant variant{ TextureVariant::Normal };
	};

	struct ResolvedOrgasm
	{
		ActorId source{};
		Tid tid{};
		IngressSequence sequence{};
		SessionEpoch epoch{};
		std::vector<InteractionTarget> targets;
	};

	enum class PPlusFailureCode : std::uint8_t
	{
		Cancelled,
		TimedOut,
		VmDispatchFailed,
		InvalidThread,
		SourceUnavailable,
		ParticipantUnavailable,
		InvalidQueryData,
	};

	struct PPlusFailure
	{
		RequestId requestId{};
		SessionEpoch epoch{};
		Tid tid{};
		IngressSequence sequence{};
		PPlusFailureCode code{ PPlusFailureCode::InvalidQueryData };
	};

	struct EnvironmentSample
	{
		ActorId actor{};
		bool interior{};
		bool raining{};
		bool snowing{};
		bool swimming{};
		GameHours observedRainHours{};
		GameTime at;
	};

	enum class RuntimeModule : std::uint8_t
	{
		Runtime,
		Ingress,
		PPlus,
		Core,
		Scheduler,
		Environment,
		Overlay,
		Effects,
		Settings,
		Menu,
		Persistence,
		Papyrus,
	};

	enum class RuntimeDiagnosticCode : std::uint8_t
	{
		DependencyUnavailable,
		VersionMismatch,
		InvalidNativeForm,
		ActorUnavailable,
		QueryFailed,
		QueryTimedOut,
		QueryCancelled,
		StaleEpoch,
		StaleRevision,
		CapacityUnavailable,
		AdapterFailure,
		SettingsRejected,
		PersistenceFailure,
		QueueOverloaded,
	};

	struct RuntimeDiagnostic
	{
		RuntimeModule module{};
		RuntimeDiagnosticCode code{};
		SessionEpoch epoch{};
		std::optional<Tid> tid;
		std::optional<IngressSequence> sequence;
		std::optional<RequestId> requestId;
		std::optional<ActorId> actor;
		std::optional<FormId> form;
		std::optional<std::uint64_t> operationId;
	};

	enum class DependencyKind : std::uint8_t
	{
		Runtime1170,
		PPlus,
		RaceMenuV2,
		MenuFramework,
		RequiredForms,
	};

	enum class DependencyStatus : std::uint8_t
	{
		Available,
		Missing,
		VersionMismatch,
	};

	struct DependencyCheck
	{
		DependencyKind dependency{};
		DependencyStatus status{};
		std::string expectedVersion;
		std::string actualVersion;
	};

	struct DependencyReport
	{
		bool ready{};
		std::vector<DependencyCheck> checks;
	};

	struct WorkBudget
	{
		std::uint32_t maxCommands{ 256 };
		std::uint32_t maxDeadlines{ 256 };
		std::chrono::microseconds maxWallTime{ 5000 };
	};

	struct PumpReport
	{
		std::uint32_t commandsProcessed{};
		std::uint32_t deadlinesProcessed{};
		std::uint32_t rejectedStale{};
		std::uint32_t deferred{};
		bool queueHasMore{};
		std::vector<RuntimeDiagnostic> diagnostics;
	};

	enum class IngressStatus : std::uint8_t
	{
		Accepted,
		SessionClosed,
		Overloaded,
	};

	struct IngressResult
	{
		IngressStatus status{};
		std::optional<IngressSequence> sequence;
	};

	enum class ClockDomain : std::uint8_t
	{
		CalendarGameHours,
		ActiveSeconds,
	};

	enum class DeadlineKind : std::uint8_t
	{
		NaturalDecay,
		TimedClear,
		SwimSample,
		OverlayRetry,
	};

	struct Deadline
	{
		DeadlineId id{};
		ActorId actor{};
		DeadlineKind kind{};
		ClockDomain clock{};
		double due{};
		SessionEpoch epoch{};
		std::optional<Region> region;
		Revision ownerRevision{};
	};

	struct DeadlineIntent
	{
		enum class Action : std::uint8_t
		{
			Upsert,
			Cancel,
		};

		Action action{};
		ActorId actor{};
		DeadlineKind kind{};
		ClockDomain clock{};
		std::optional<Region> region;
		std::optional<double> due;
		Revision ownerRevision{};
	};

	struct DeadlineElapsed
	{
		DeadlineId id{};
		ActorId actor{};
		DeadlineKind kind{};
		SessionEpoch epoch{};
		std::optional<Region> region;
		Revision ownerRevision{};
	};

	enum class DeadlineUpdateStatus : std::uint8_t
	{
		Updated,
		RejectedInvalid,
	};

	enum class CoreDiagnosticCode : std::uint8_t
	{
		AtCapacity,
		InvalidTarget,
		InvalidRegion,
		InvalidSettings,
	};

	struct CoreDiagnostic
	{
		CoreDiagnosticCode code{};
		ActorId actor{};
		std::optional<Region> region;
	};

	struct AddLayers
	{
		ActorId source{};
		Tid tid{};
		IngressSequence sequence{};
		std::uint64_t selectionSeed{};
		std::vector<InteractionTarget> targets;
	};

	struct ClearRegions
	{
		ActorId actor{};
		std::vector<Region> regions;
	};

	struct Advance
	{
		ActorId actor{};
		GameTime now;
		std::optional<EnvironmentSample> environment;
	};

	struct SettingsChanged
	{
	};

	using CoreOp = std::variant<AddLayers, ClearRegions, Advance, SettingsChanged>;

	struct LayerVisual
	{
		std::uint64_t layerId{};
		std::uint16_t textureIndex{};
		TextureVariant variant{ TextureVariant::Normal };
	};

	struct DesiredSpell
	{
		std::uint8_t combinationIndex{};
		bool enabled{};
	};

	struct RegionTarget
	{
		ActorId actor{};
		Region region{};
		std::vector<LayerVisual> layers;
		Revision revision{};
	};

	struct SpellTarget
	{
		ActorId actor{};
		DesiredSpell desired;
		Revision revision{};
	};

	struct ChangeSet
	{
		std::vector<RegionTarget> regions;
		std::vector<SpellTarget> spells;
		std::vector<DeadlineIntent> deadlines;
		std::vector<CoreDiagnostic> diagnostics;
	};

	struct CoreState
	{
		std::unordered_map<ActorId, ActorState> actors;
		std::uint64_t nextLayerId{ 1 };
	};

	enum class SessionReason : std::uint8_t
	{
		NewGame,
		PreLoadGame,
		Revert,
	};

	enum class LoadCommitStatus : std::uint8_t
	{
		Committed,
		NotReady,
		NoStaging,
		Invalid,
	};

	struct LoadCommitResult
	{
		LoadCommitStatus status{};
		std::uint32_t restored{};
		std::uint32_t skipped{};
	};
}
