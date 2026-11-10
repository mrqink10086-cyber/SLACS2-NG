#pragma once

#include "config/Settings.h"
#include "core/Core.h"
#include "integration/Effects.h"
#include "integration/OverlayAdapter.h"
#include "integration/PPlusAdapter.h"
#include "integration/QuestBridge.h"
#include "menu/MenuTypes.h"
#include "runtime/ActiveClock.h"
#include "persistence/Serialization.h"
#include "runtime/ActorRegistry.h"
#include "runtime/BackgroundPump.h"
#include "runtime/Environment.h"
#include "runtime/Ingress.h"
#include "runtime/Scheduler.h"
#include "runtime/RoundRobin.h"
#include "runtime/RetrySchedule.h"

#include <SKSE/Interfaces.h>

#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_set>
#include <unordered_map>

namespace slacs
{
	class Runtime
	{
	public:
		static Runtime& instance();

		bool install();
		DependencyReport checkDependencies() const;
		bool initialize();
		void beginSession(SessionReason a_reason);
		void onLoadStaging(LoadResult a_result);
		LoadCommitResult commitLoadStaging();
		void onSessionReady();
		void shutdown();
		void publishSaveSnapshot();

		[[nodiscard]] SessionEpoch epoch() const noexcept;
		IngressResult enqueue(RuntimeCommand a_command);
		PumpReport pump(const GameTime& a_now, WorkBudget a_budget);

		[[nodiscard]] Ingress& ingress() noexcept;
		[[nodiscard]] const Settings& settings() const noexcept;
		[[nodiscard]] const DependencyReport& dependencies() const noexcept;

		static void onMessage(SKSE::MessagingInterface::Message* a_message);
		static void onSave(SKSE::SerializationInterface* a_intfc);
		static void onLoad(SKSE::SerializationInterface* a_intfc);
		static void onRevert(SKSE::SerializationInterface* a_intfc);
		static void onFormDelete(RE::VMHandle a_handle);

		void saveTo(SKSE::SerializationInterface& a_intfc) const;

	private:
		struct SceneContext
		{
			bool hasPlayer{};
			SessionEpoch epoch{};
		};

		struct TidOrderState
		{
			std::set<IngressSequence> outstanding;
			std::map<IngressSequence, PPlusCompleted> completed;
		};

		Runtime();

		void pumpNow();
		PumpReport pumpLocked(const GameTime& a_now, WorkBudget a_budget);
		GameTime currentGameTime();
		void applySessionTransition();
		void activateSession();
		void requestLoadCommit();
		void processCommand(const RuntimeCommand& a_command, PumpReport& a_report);
		void processNativeOrgasm(const NativeOrgasm& a_command, PumpReport& a_report);
		void processSceneNotice(const SceneNotice& a_command, PumpReport& a_report);
		void processPPlusCompleted(const PPlusCompleted& a_command, PumpReport& a_report);
		void processMenuAction(const MenuAction& a_action, PumpReport& a_report);
		void dispatchPPlusCompletion(const PPlusCompleted& a_command, PumpReport& a_report);
		void applyChangeSet(const ChangeSet& a_change, PumpReport& a_report);
		void sampleEnvironment(const GameTime& a_now, PumpReport& a_report);
		void processDirtyVisuals(PumpReport& a_report, std::size_t a_maxTargets);
		void processDirtyEffects(PumpReport& a_report, std::size_t a_maxTargets);
		void enqueueCurrentTarget(ActorId a_actor, Region a_region, bool a_resetRetry);
		void invalidateActorVisuals(ActorId a_actor, VisualInvalidation a_reason, bool a_resetRetry = false);
		void processQuestRetry();
		void scheduleQuestRetry();
		void processDueWork(const GameTime& a_now, SteadyTime a_started, const WorkBudget& a_budget, PumpReport& a_report);
		void processDeadlineElapsed(const DeadlineElapsed& a_elapsed, const GameTime& a_now, PumpReport& a_report);
		void processEnvironmentObserved(const EnvironmentObserved& a_command, const GameTime& a_now, PumpReport& a_report);
		void advanceActor(ActorId a_actor, const GameTime& a_now, const EnvironmentSample* a_sample, PumpReport& a_report);
		void settleAllWithOldSettings(const GameTime& a_now, PumpReport& a_report);
		void applyDeadlineIntents(const ChangeSet& a_change, PumpReport& a_report);
		DeadlineId stableDeadlineId(ActorId a_actor, DeadlineKind a_kind, const std::optional<Region>& a_region);
		void scheduleOverlayRetry(ActorId a_actor, Region a_region);
		void cancelOverlayRetry(ActorId a_actor, Region a_region);
		void resetSessionState();
		void removeActorForm(FormId a_formId);
		void discardLoadStaging();
		void handleLoadFailure();
		void addStale(PumpReport& a_report, RuntimeModule a_module, SessionEpoch a_epoch);
		void publishMenuSnapshot();
		void appendMenuDiagnostics(const PumpReport& a_report);
		void recordPumpTiming(SteadyTime a_started, const PumpReport& a_report);
		void recordRequestLatency(RequestId a_requestId);
		[[nodiscard]] std::string actorDisplayName(FormId a_formId);

		static std::filesystem::path dataPath();

		std::atomic<SessionEpoch> epoch_{ 1 };
		std::atomic<bool> initialized_{ false };
		std::atomic<bool> shutdown_{ false };
		std::atomic<bool> sessionTransitionPending_{ false };
		std::atomic<SessionReason> transitionReason_{ SessionReason::NewGame };
		std::atomic<bool> loadCommitRequested_{ false };
		std::atomic<bool> readyRequested_{ false };
		std::atomic<bool> sessionReady_{ false };
		std::atomic<bool> loadExpected_{ false };
		std::atomic<bool> overlayReady_{ false };
		bool installed_{};
		std::mutex pumpMutex_;
		std::mutex stagingMutex_;
		Ingress ingress_;
		BackgroundPump background_;
		QuestBridge questBridge_;
		ActorRegistry actorRegistry_;
		PPlusAdapter pplus_;
		OverlayAdapter overlay_;
		Effects effects_;
		Environment environment_;
		Settings settings_;
		DependencyReport dependencies_;
		CoreState coreState_;
		std::optional<LoadResult> loadStaging_;
		std::atomic<std::shared_ptr<const SaveSnapshot>> saveSnapshot_{};
		Scheduler scheduler_;
		std::map<std::tuple<ActorId, DeadlineKind, std::optional<Region>>, DeadlineId> deadlineIds_;
		DeadlineId nextDeadlineId_{ 1 };
		SessionEpoch stagingEpoch_{};
		std::unordered_map<Tid, SceneContext> sceneTids_;
		std::unordered_map<Tid, TidOrderState> tidOrder_;
		std::map<std::uint64_t, RegionTarget> dirtyVisuals_;
		std::map<ActorId, SpellTarget> dirtyEffects_;
		RetrySchedule<std::uint64_t> dirtyVisualRetries_;
		RetrySchedule<ActorId> dirtyEffectRetries_;
		std::map<ActorId, bool> visual3DReady_;
		RoundRobin<std::uint64_t> visualCursor_;
		RoundRobin<ActorId> effectCursor_;
		RoundRobin<ActorId> actor3DCursor_;
		RoundRobin<ActorId> environmentCursor_;
		std::size_t environmentScanRemaining_{};
		std::unordered_map<std::uint64_t, SteadyTime> capacityNoticeAt_;
		SteadyTime nextEnvironmentSample_{};
		bool effectsReady_{};
		RequestId nextRequestId_{ 1 };
		IngressSequence nextTestSequence_{ 1 };

		SettingsFeedback menuFeedback_{};
		std::deque<RuntimeDiagnostic> menuDiagnostics_;
		std::unordered_map<FormId, std::string> displayNames_;
		std::uint64_t menuRevision_{};

		std::uint64_t perfPumpCount_{};
		std::uint64_t perfPumpMicrosTotal_{};
		std::uint64_t perfPumpMicrosMax_{};
		std::uint64_t perfEnvironmentSamples_{};
		std::uint64_t perfOverlaySubmits_{};
		std::uint64_t perfOverlaySlots_{};
		std::uint64_t perfMaxQueueDepth_{};
		std::uint64_t perfRequestCount_{};
		std::uint64_t perfRequestLatencyMicrosTotal_{};
		std::uint64_t perfRequestLatencyMicrosMax_{};
		SteadyTime perfWindowStart_{};
		std::unordered_map<RequestId, SteadyTime> requestStarted_;

		ActiveClock activeClock_;

		bool questRetryPending_{};
		int questRetryAttempts_{};
		SteadyTime nextQuestRetry_{};

		std::uint64_t orgasmsAccepted_{};
		std::uint64_t sceneStarts_{};
		std::uint64_t sceneEnds_{};
	};
}
