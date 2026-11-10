#include "runtime/Runtime.h"

#include "integration/ActorCandidates.h"
#include "menu/Menu.h"
#include "menu/MenuText.h"

#include <SKSE/SKSE.h>
#include <RE/I/IObjectHandlePolicy.h>
#include <RE/V/VirtualMachine.h>

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>

#pragma comment(lib, "version.lib")

namespace slacs
{
	namespace
	{
		constexpr std::uint32_t kSerializationUniqueId = 0x534C4143;
		constexpr DeadlineId kQuestRetryDeadlineId = 1;
		constexpr RE::FormID kQuestLocalFormId = 0x000D62;
		constexpr RE::FormID kMegaRaceListLocalFormId = 0x0004E66;
		constexpr std::string_view kPluginName = "SLACS_2_Plus_SCO.esp";

		bool ModuleLoaded(const wchar_t* a_name)
		{
			return GetModuleHandleW(a_name) != nullptr;
		}

		std::string ModuleVersion(const wchar_t* a_name)
		{
			const auto module = GetModuleHandleW(a_name);
			if (!module) {
				return {};
			}
			wchar_t path[MAX_PATH]{};
			if (GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path))) == 0) {
				return {};
			}

			DWORD handle = 0;
			const auto size = GetFileVersionInfoSizeW(path, std::addressof(handle));
			if (size == 0) {
				return {};
			}
			std::vector<std::uint8_t> data(size);
			if (!GetFileVersionInfoW(path, 0, size, data.data())) {
				return {};
			}
			VS_FIXEDFILEINFO* info = nullptr;
			UINT length = 0;
			if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(std::addressof(info)), std::addressof(length)) ||
				!info) {
				return {};
			}
			return std::to_string(HIWORD(info->dwFileVersionMS)) + "." +
			       std::to_string(LOWORD(info->dwFileVersionMS)) + "." +
			       std::to_string(HIWORD(info->dwFileVersionLS)) + "." +
			       std::to_string(LOWORD(info->dwFileVersionLS));
		}

		std::string DependencyStatusName(DependencyStatus a_status)
		{
			switch (a_status) {
			case DependencyStatus::Available:
				return "available";
			case DependencyStatus::Missing:
				return "missing";
			case DependencyStatus::VersionMismatch:
				return "version mismatch";
			}
			return "unknown";
		}
	}

	Runtime& Runtime::instance()
	{
		static Runtime runtime;
		return runtime;
	}

	Runtime::Runtime() :
		ingress_(2048, [this] { background_.request(); }),
		background_([this] { pumpNow(); })
	{
		pplus_.setDependencies(PPlusDependencies{
			&actorRegistry_,
			[this]() -> const Settings& { return settings_; },
			[this] { return epoch(); },
			[this] { return ingress_.reserve(1); },
			[this] { ingress_.release(1); },
			[this](PPlusCompleted&& a_completed) {
				return ingress_.postReserved(RuntimeCommand{ std::move(a_completed) }).status == IngressStatus::Accepted;
			},
		});
		overlay_.setDependencies(OverlayDependencies{
			[] { return dataPath(); },
			[this] { return epoch(); },
			[this](FormId a_formId, SessionEpoch a_epoch) {
				ingress_.post(RuntimeCommand{ OverlayInstalled{ a_formId, a_epoch } });
			},
		});
	}

	bool Runtime::install()
	{
		if (installed_) {
			return true;
		}

		auto* serialization = SKSE::GetSerializationInterface();
		if (!serialization) {
			logs::error("Serialization interface unavailable");
			return false;
		}
		serialization->SetUniqueID(kSerializationUniqueId);
		serialization->SetSaveCallback(onSave);
		serialization->SetLoadCallback(onLoad);
		serialization->SetRevertCallback(onRevert);
		serialization->SetFormDeleteCallback(onFormDelete);

		auto* messaging = SKSE::GetMessagingInterface();
		if (!messaging || !messaging->RegisterListener(onMessage)) {
			logs::error("Messaging interface unavailable");
			return false;
		}

		installed_ = true;
		background_.start();
		logs::info("SLACS2 NG systems installed");
		return true;
	}

	std::filesystem::path Runtime::dataPath()
	{
		wchar_t path[MAX_PATH]{};
		if (GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path))) != 0) {
			return std::filesystem::path(path).parent_path() / "Data";
		}
		return std::filesystem::current_path() / "Data";
	}

	DependencyReport Runtime::checkDependencies() const
	{
		DependencyReport report{};
		auto* dataHandler = RE::TESDataHandler::GetSingleton();

		report.checks.push_back({
			DependencyKind::Runtime1170,
			DependencyStatus::Available,
			"1.6.1170.0",
			"1.6.1170.0",
		});

		const bool sexLabEsm = dataHandler && dataHandler->LookupLoadedModByName("SexLab.esm");
		const bool sexLabDll = ModuleLoaded(L"SexLabUtil.dll");
		const auto sexLabVersion = sexLabDll ? ModuleVersion(L"SexLabUtil.dll") : std::string{};
		// The bridge only consumes Papyrus ModEvents, not a linked binary API.
		// Requiring one exact SexLabUtil build makes otherwise compatible P+
		// installs fail at DataLoaded; pinning 2.18.1 is a reference baseline,
		// not a runtime contract.
		const auto sexLabStatus = sexLabEsm && sexLabDll ?
			DependencyStatus::Available :
			DependencyStatus::Missing;
		report.checks.push_back({
			DependencyKind::PPlus,
			sexLabStatus,
			"SexLabUtil.dll + SexLab.esm",
			sexLabVersion,
		});

		const bool raceMenuDll = ModuleLoaded(L"skee64.dll");
		const auto raceMenuVersion = raceMenuDll ? ModuleVersion(L"skee64.dll") : std::string{};
		const auto raceMenuStatus = !raceMenuDll ? DependencyStatus::Missing :
			(raceMenuVersion == "0.4.20.0" ? DependencyStatus::Available :
				DependencyStatus::VersionMismatch);
		report.checks.push_back({
			DependencyKind::RaceMenuV2,
			raceMenuStatus,
			"0.4.20.0 / overlay+override v2",
			raceMenuVersion,
		});

		const bool menuDll = ModuleLoaded(L"SKSEMenuFramework.dll");
		const auto menuVersion = menuDll ? ModuleVersion(L"SKSEMenuFramework.dll") : std::string{};
		const auto menuStatus = !menuDll ? DependencyStatus::Missing :
			(Menu::available() ? DependencyStatus::Available :
				DependencyStatus::VersionMismatch);
		report.checks.push_back({
			DependencyKind::MenuFramework,
			menuStatus,
			"required menu exports",
			menuVersion,
		});

		auto* quest = dataHandler ? dataHandler->LookupForm<RE::TESQuest>(kQuestLocalFormId, kPluginName) : nullptr;
		auto* megaRaces = dataHandler ? dataHandler->LookupForm(kMegaRaceListLocalFormId, kPluginName) : nullptr;
		const bool formsReady = quest && megaRaces && !quest->IsDeleted() && !megaRaces->IsDeleted();
		report.checks.push_back({
			DependencyKind::RequiredForms,
			formsReady ? DependencyStatus::Available : DependencyStatus::Missing,
			"SLACS_Main_Q + SLACS_MegaCumRaces",
			formsReady ? "SLACS_Main_Q + SLACS_MegaCumRaces" : std::string{},
		});

		report.ready = std::all_of(report.checks.begin(), report.checks.end(), [](const DependencyCheck& check) {
			return check.status == DependencyStatus::Available;
		});
		return report;
	}

	bool Runtime::initialize()
	{
		if (initialized_.exchange(true)) {
			return dependencies_.ready;
		}

		const auto configPath = dataPath() / "SKSE" / "Plugins" / "SLACS2NG.json";
		auto settings = Settings::load(configPath);
		switch (settings.status) {
		case SettingsLoadStatus::Loaded:
			settings_ = settings.value;
			logs::info("Settings loaded from {}", configPath.string());
			break;
		case SettingsLoadStatus::MissingUsingDefaults:
			logs::info("Settings file missing; using defaults");
			break;
		case SettingsLoadStatus::Invalid:
			logs::error("Settings invalid; keeping defaults");
			for (const auto& issue : settings.issues) {
				logs::error("Settings issue {} code={}", issue.key, static_cast<int>(issue.code));
			}
			break;
		case SettingsLoadStatus::IoError:
			logs::error("Settings I/O error; keeping defaults");
			break;
		}

		dependencies_ = checkDependencies();
		for (const auto& check : dependencies_.checks) {
			logs::info(
				"Dependency {} status={} expected={} actual={}",
				static_cast<int>(check.dependency),
				DependencyStatusName(check.status),
				check.expectedVersion,
				check.actualVersion);
		}
		if (!dependencies_.ready) {
			logs::error("Runtime dependencies unavailable; ingress remains closed");
			return false;
		}

		const auto handshake = overlay_.initialize();
		overlayReady_.store(
			handshake.status == OverlayHandshake::Status::Available,
			std::memory_order_release);
		if (handshake.status == OverlayHandshake::Status::Available) {
			logs::info("Overlay adapter ready apiVersion={}", handshake.apiVersion);
		} else {
			logs::error("Overlay adapter unavailable status={} apiVersion={}",
				static_cast<int>(handshake.status),
				handshake.apiVersion);
		}
		if (!overlayReady_.load(std::memory_order_acquire)) {
			logs::error("Overlay API handshake failed; session ingress stays closed");
			return false;
		}

		effectsReady_ = effects_.initialize();
		if (!effectsReady_) {
			logs::error("Effects initialization failed; spells will remain unavailable");
		}

		logs::info("Runtime initialized for session epoch {}", epoch_.load());
		return true;
	}

	void Runtime::beginSession(SessionReason a_reason)
	{
		std::lock_guard lock(pumpMutex_);
		if (a_reason == SessionReason::Revert && loadExpected_.load(std::memory_order_acquire)) {
			questBridge_.invalidate();
			return;
		}
		ingress_.setOpen(false);
		effects_.clearSession(epoch());
		for (const auto& [actorId, state] : coreState_.actors) {
			if (auto lease = actorRegistry_.resolve(actorId, ResolveNeed::ActorOnly)) {
				try {
					overlay_.clearActor(*lease, actorId);
					SpellTarget target{};
					target.actor = actorId;
					target.desired = DesiredSpell{ 0, false };
					target.revision = state.revision;
					effects_.reconcile(*lease, target);
				} catch (...) {
					logs::warn("Visual clear before session change failed actor={}", actorId);
				}
			}
		}
		environment_.invalidateAll();
		loadExpected_.store(a_reason == SessionReason::PreLoadGame, std::memory_order_release);
		const auto oldEpoch = epoch();
		const auto nextEpoch = epoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
		pplus_.cancelEpoch(oldEpoch);
		scheduler_.cancelEpoch(oldEpoch);
		scheduler_.clear();
		deadlineIds_.clear();
		nextDeadlineId_ = 1;
		discardLoadStaging();
		questBridge_.invalidate();
		sessionReady_.store(false, std::memory_order_release);
		sessionTransitionPending_.store(true, std::memory_order_release);
		transitionReason_.store(a_reason, std::memory_order_release);
		readyRequested_.store(false, std::memory_order_release);
		loadCommitRequested_.store(false, std::memory_order_release);
		logs::info("Session begin requested reason={} epoch={}", static_cast<int>(a_reason), nextEpoch);
		background_.request();
	}

	void Runtime::applySessionTransition()
	{
		// Drain terminal P+ requests from the previous epoch before dropping
		// ingress reservations. They are already cancelled; this only releases
		// their reserved terminal slots and removes them from the adapter map.
		pplus_.poll(std::chrono::steady_clock::now(), 1024);
		ingress_.clear();
		resetSessionState();
		publishSaveSnapshot();
		logs::info("Session transition applied epoch={} reason={}",
			epoch(),
			static_cast<int>(transitionReason_.load(std::memory_order_acquire)));
	}

	void Runtime::resetSessionState()
	{
		scheduler_.clear();
		deadlineIds_.clear();
		nextDeadlineId_ = 1;
		questRetryPending_ = false;
		questRetryAttempts_ = 0;
		nextQuestRetry_ = {};
		questBridge_.reset();
		actorRegistry_.reset(epoch());
		sceneTids_.clear();
		tidOrder_.clear();
		dirtyVisuals_.clear();
		dirtyEffects_.clear();
		dirtyVisualRetries_.clear();
		dirtyEffectRetries_.clear();
		visual3DReady_.clear();
		visualCursor_.reset();
		effectCursor_.reset();
		actor3DCursor_.reset();
		environmentCursor_.reset();
		environmentScanRemaining_ = 0;
		capacityNoticeAt_.clear();
		nextEnvironmentSample_ = {};
		overlay_.clearSession(epoch());
		effects_.clearSession(epoch());
		environment_.reset(epoch());
		coreState_ = {};
		orgasmsAccepted_ = 0;
		sceneStarts_ = 0;
		sceneEnds_ = 0;
		menuFeedback_ = {};
		menuDiagnostics_.clear();
		displayNames_.clear();
		actorCandidates_.clear();
		selectedCandidate_ = 0;
		selectionRevision_ = 0;
		actorFeedback_.clear();
		perfPumpCount_ = 0;
		perfPumpMicrosTotal_ = 0;
		perfPumpMicrosMax_ = 0;
		perfEnvironmentSamples_ = 0;
		perfOverlaySubmits_ = 0;
		perfOverlaySlots_ = 0;
		perfMaxQueueDepth_ = 0;
		perfRequestCount_ = 0;
		perfRequestLatencyMicrosTotal_ = 0;
		perfRequestLatencyMicrosMax_ = 0;
		perfWindowStart_ = {};
		requestStarted_.clear();
		sessionReady_.store(false, std::memory_order_release);
	}

	void Runtime::removeActorForm(FormId a_formId)
	{
		const auto actorId = actorRegistry_.remove(a_formId);
		if (!actorId) {
			return;
		}

		for (auto it = deadlineIds_.begin(); it != deadlineIds_.end();) {
			if (std::get<0>(it->first) == *actorId) {
				scheduler_.cancel(it->second);
				it = deadlineIds_.erase(it);
			} else {
				++it;
			}
		}
		coreState_.actors.erase(*actorId);
		std::erase_if(actorCandidates_, [actorId](const auto& a_row) { return a_row.actor == *actorId; });
		overlay_.clearActor(*actorId);
		effects_.invalidate(*actorId);
		environment_.invalidate(*actorId);
		dirtyEffects_.erase(*actorId);
		dirtyEffectRetries_.clear(*actorId);
		visual3DReady_.erase(*actorId);
		for (auto it = dirtyVisuals_.begin(); it != dirtyVisuals_.end();) {
			if (static_cast<ActorId>(it->first >> 3) == *actorId) {
				dirtyVisualRetries_.clear(it->first);
				it = dirtyVisuals_.erase(it);
			} else {
				++it;
			}
		}
		publishSaveSnapshot();
	}

	void Runtime::discardLoadStaging()
	{
		std::lock_guard lock(stagingMutex_);
		loadStaging_.reset();
		stagingEpoch_ = 0;
	}

	void Runtime::onLoadStaging(LoadResult a_result)
	{
		if (!loadExpected_.load(std::memory_order_acquire)) {
			logs::warn("Ignoring load staging outside a pre-load session status={} actors={}",
				static_cast<int>(a_result.status),
				a_result.actors.size());
			return;
		}
		logs::info("Load staging received status={} actors={} errors={}",
			static_cast<int>(a_result.status),
			a_result.actors.size(),
			a_result.recordErrors.size());
		std::lock_guard lock(stagingMutex_);
		if (!loadExpected_.load(std::memory_order_acquire)) {
			logs::warn("Discarding load staging that raced a session transition");
			return;
		}
		loadStaging_ = std::move(a_result);
		stagingEpoch_ = epoch();
	}

	LoadCommitResult Runtime::commitLoadStaging()
	{
		loadExpected_.store(false, std::memory_order_release);
		LoadCommitResult result{};
		std::optional<LoadResult> staging;
		SessionEpoch stagedEpoch = 0;
		{
			std::lock_guard lock(stagingMutex_);
			if (!loadStaging_) {
				// SKSE may skip the load callback when the save has no co-save
				// record for this plugin. Treat that as an empty snapshot rather
				// than leaving the session permanently closed.
				resetSessionState();
				result.status = LoadCommitStatus::Committed;
				publishSaveSnapshot();
				return result;
			}
			staging = std::move(*loadStaging_);
			loadStaging_.reset();
			stagedEpoch = stagingEpoch_;
			stagingEpoch_ = 0;
		}

		if (stagedEpoch != epoch()) {
			logs::warn("Discarding stale load staging epoch={} current={}", stagedEpoch, epoch());
			resetSessionState();
			result.status = LoadCommitStatus::Invalid;
			publishSaveSnapshot();
			return result;
		}

		switch (staging->status) {
		case LoadStatus::UnsupportedSchema:
		case LoadStatus::InvalidData:
		case LoadStatus::IoError:
			resetSessionState();
			result.status = LoadCommitStatus::Invalid;
			publishSaveSnapshot();
			return result;
		case LoadStatus::Empty:
			resetSessionState();
			result.status = LoadCommitStatus::Committed;
			publishSaveSnapshot();
			return result;
		case LoadStatus::Loaded:
			break;
		}

		resetSessionState();
		const auto now = currentGameTime();
		std::vector<ActorState> restored;
		restored.reserve(staging->actors.size());
		std::unordered_set<FormId> seenForms;
		for (const auto& persisted : staging->actors) {
			if (persisted.formId == kInvalidFormId || !seenForms.insert(persisted.formId).second) {
				resetSessionState();
				result.status = LoadCommitStatus::Invalid;
				publishSaveSnapshot();
				return result;
			}
			const auto actorId = actorRegistry_.bind(persisted.formId);
			if (!actorId) {
				++result.skipped;
				continue;
			}
			ActorState state{};
			state.actor = *actorId;
			state.formId = persisted.formId;
			state.regions = persisted.regions;
			state.lastSettledGameHours = persisted.lastSettledGameHours;
			if (persisted.timedClearRemainingSeconds) {
				state.timedClearAt = now.activeSeconds + std::max(0.0, *persisted.timedClearRemainingSeconds);
			}
			for (const auto& region : state.regions) {
				for (const auto& layer : region.layers) {
					if (layer.id == std::numeric_limits<std::uint64_t>::max()) {
						resetSessionState();
						result.status = LoadCommitStatus::Invalid;
						publishSaveSnapshot();
						return result;
					}
				}
			}
			restored.push_back(std::move(state));
			++result.restored;
		}

		PumpReport loadReport;
		auto change = Core::replaceState(coreState_, std::move(restored), settings_, now);
		applyChangeSet(change, loadReport);

		result.status = LoadCommitStatus::Committed;
		publishSaveSnapshot();
		return result;
	}

	void Runtime::onSessionReady()
	{
		readyRequested_.store(true, std::memory_order_release);
		background_.request();
	}

	void Runtime::activateSession()
	{
		if (shutdown_.load(std::memory_order_acquire)) {
			return;
		}
		if (!initialized_.load(std::memory_order_acquire) || !dependencies_.ready ||
			!overlayReady_.load(std::memory_order_acquire)) {
			logs::error("Session ready requested before dependencies are available");
			return;
		}
		sessionReady_.store(true, std::memory_order_release);
		loadExpected_.store(false, std::memory_order_release);
		ingress_.setOpen(true);
		publishSaveSnapshot();
		scheduleQuestRetry();
		logs::info("Session ready epoch={} ingress open", epoch());
	}

	void Runtime::requestLoadCommit()
	{
		loadCommitRequested_.store(true, std::memory_order_release);
		readyRequested_.store(true, std::memory_order_release);
		background_.request();
	}

	void Runtime::handleLoadFailure()
	{
		std::lock_guard lock(pumpMutex_);
		ingress_.setOpen(false);
		loadExpected_.store(false, std::memory_order_release);
		sessionReady_.store(false, std::memory_order_release);
		scheduler_.cancelEpoch(epoch());
		scheduler_.clear();
		deadlineIds_.clear();
		discardLoadStaging();
		loadCommitRequested_.store(false, std::memory_order_release);
		readyRequested_.store(false, std::memory_order_release);
		sessionTransitionPending_.store(true, std::memory_order_release);
		transitionReason_.store(SessionReason::PreLoadGame, std::memory_order_release);
		logs::error("Load failed; session remains closed epoch={}", epoch());
		background_.request();
	}

	void Runtime::shutdown()
	{
		if (shutdown_.exchange(true, std::memory_order_acq_rel)) {
			return;
		}
		ingress_.setOpen(false);
		const auto oldEpoch = epoch();
		epoch_.fetch_add(1, std::memory_order_acq_rel);
		background_.stop();
		{
			std::lock_guard lock(pumpMutex_);
			pplus_.cancelEpoch(oldEpoch);
			scheduler_.cancelEpoch(oldEpoch);
			scheduler_.clear();
			deadlineIds_.clear();
			discardLoadStaging();
		}
		questBridge_.invalidate();
		logs::info("Runtime shutdown");
	}

	SessionEpoch Runtime::epoch() const noexcept
	{
		return epoch_.load(std::memory_order_acquire);
	}

	IngressResult Runtime::enqueue(RuntimeCommand a_command)
	{
		return ingress_.post(std::move(a_command));
	}

	Ingress& Runtime::ingress() noexcept
	{
		return ingress_;
	}

	const Settings& Runtime::settings() const noexcept
	{
		return settings_;
	}

	const DependencyReport& Runtime::dependencies() const noexcept
	{
		return dependencies_;
	}

	void Runtime::pumpNow()
	{
		std::lock_guard lock(pumpMutex_);
		if (shutdown_.load(std::memory_order_acquire)) {
			return;
		}
		WorkBudget budget;
		auto report = pumpLocked(currentGameTime(), budget);
		if (environmentScanRemaining_ > 0) {
			background_.requestAfter(std::chrono::milliseconds{ 25 });
		}
		if (report.queueHasMore) {
			background_.requestAfter(std::chrono::milliseconds{ 25 });
		}
		if (report.deferred) {
			background_.requestAfter(std::chrono::milliseconds{ 25 });
		}
		if (questRetryPending_) {
			background_.requestAfter(std::chrono::milliseconds{ 250 });
		}
		const auto now = std::chrono::steady_clock::now();
		const auto requestRetryWake = [this, now](const auto& a_retries, const auto& a_items) {
			const auto due = a_retries.nextReadyAt(a_items, now);
			if (due && *due > now) {
				background_.requestAfter(std::chrono::ceil<std::chrono::milliseconds>(*due - now));
			}
		};
		requestRetryWake(dirtyVisualRetries_, dirtyVisuals_);
		if (effectsReady_) {
			requestRetryWake(dirtyEffectRetries_, dirtyEffects_);
		}
		if (pplus_.pending() > 0) {
			background_.requestAfter(std::chrono::milliseconds{ 50 });
		}
		if (sessionReady_.load(std::memory_order_acquire)) {
			// Calendar/active clocks advance while no command arrives.
			background_.requestAfter(std::chrono::seconds{ 1 });
		}
	}

	GameTime Runtime::currentGameTime()
	{
		GameTime now{};
		if (auto* calendar = RE::Calendar::GetSingleton()) {
			now.calendarHours = calendar->GetHoursPassed();
		}
		if (auto* timer = RE::BSTimer::GetSingleton()) {
			bool paused = timer->pauseCount != 0;
			if (auto* main = RE::Main::GetSingleton()) {
				paused = paused || main->GetRuntimeData().freezeTime;
			}
			now.activeSeconds = activeClock_.advance(timer->runTimeMS, paused);
		}
		return now;
	}

	PumpReport Runtime::pump(const GameTime& a_now, WorkBudget a_budget)
	{
		std::lock_guard lock(pumpMutex_);
		return pumpLocked(a_now, a_budget);
	}

	PumpReport Runtime::pumpLocked(const GameTime& a_now, WorkBudget a_budget)
	{
		const auto pumpStarted = std::chrono::steady_clock::now();
		PumpReport report{};
		if (shutdown_.load(std::memory_order_acquire)) {
			return report;
		}

		if (sessionTransitionPending_.exchange(false, std::memory_order_acq_rel)) {
			applySessionTransition();
		}
		if (loadCommitRequested_.exchange(false, std::memory_order_acq_rel)) {
			const auto committed = commitLoadStaging();
			if (committed.status != LoadCommitStatus::Committed) {
				logs::error("Deferred load commit rejected status={}; opening empty session",
					static_cast<int>(committed.status));
				readyRequested_.store(true, std::memory_order_release);
			}
		}
		if (readyRequested_.exchange(false, std::memory_order_acq_rel)) {
			activateSession();
		}

		const auto started = std::chrono::steady_clock::now();
		std::uint32_t processed = 0;

		if (sessionReady_.load(std::memory_order_acquire)) {
			pplus_.poll(started, 128);
			if (nextEnvironmentSample_ == SteadyTime{} || started >= nextEnvironmentSample_) {
				sampleEnvironment(a_now, report);
				nextEnvironmentSample_ = started + (environmentScanRemaining_ > 0 ?
					std::chrono::milliseconds{ 25 } : std::chrono::milliseconds{ 1000 });
			}
		}

		while (processed < a_budget.maxCommands) {
			if (a_budget.maxWallTime.count() > 0 &&
				std::chrono::steady_clock::now() - started >= a_budget.maxWallTime) {
				report.deferred = 1;
				break;
			}
			auto command = ingress_.pop();
			if (!command) {
				break;
			}
			++processed;
			++report.commandsProcessed;
			processCommand(*command, report);
			if (a_budget.maxWallTime.count() > 0 &&
				std::chrono::steady_clock::now() - started >= a_budget.maxWallTime) {
				report.deferred = 1;
				break;
			}
		}

		if (ingress_.size() > 0) {
			if (processed >= a_budget.maxCommands) {
				report.deferred = 1;
			}
			report.queueHasMore = true;
		}

		const bool overWallTime = a_budget.maxWallTime.count() > 0 &&
			std::chrono::steady_clock::now() - started >= a_budget.maxWallTime;
		if (overWallTime) {
			report.deferred = 1;
		} else {
			processDueWork(a_now, started, a_budget, report);
		}

		if (!report.deferred) {
			publishSaveSnapshot();
			processDirtyVisuals(report, 64);
		}

		perfMaxQueueDepth_ = std::max<std::uint64_t>(perfMaxQueueDepth_, ingress_.size());
		appendMenuDiagnostics(report);
		recordPumpTiming(pumpStarted, report);
		publishMenuSnapshot();
		return report;
	}

	void Runtime::processDueWork(const GameTime& a_now, SteadyTime a_started, const WorkBudget& a_budget, PumpReport& a_report)
	{
		const auto overBudget = [&]() {
			return a_budget.maxWallTime.count() > 0 &&
				std::chrono::steady_clock::now() - a_started >= a_budget.maxWallTime;
		};

		std::uint32_t processed = 0;
		while (processed < a_budget.maxDeadlines) {
			if (overBudget()) {
				a_report.deferred = 1;
				break;
			}
			auto due = scheduler_.takeDue(a_now, 1);
			if (due.empty()) {
				break;
			}
			++processed;
			++a_report.deadlinesProcessed;
			processDeadlineElapsed(due.front(), a_now, a_report);
		}

		const auto steadyNow = std::chrono::steady_clock::now();
		while (processed < a_budget.maxDeadlines) {
			if (overBudget()) {
				a_report.deferred = 1;
				break;
			}
			auto due = scheduler_.takeDueSteady(steadyNow, 1);
			if (due.empty()) {
				break;
			}
			++processed;
			++a_report.deadlinesProcessed;
			if (due.front() == kQuestRetryDeadlineId) {
				processQuestRetry();
			}
		}

		if (scheduler_.hasDue(a_now) || scheduler_.hasDueSteady(std::chrono::steady_clock::now())) {
			a_report.deferred = 1;
		}
	}

	void Runtime::processDeadlineElapsed(const DeadlineElapsed& a_elapsed, const GameTime& a_now, PumpReport& a_report)
	{
		if (a_elapsed.epoch != epoch()) {
			addStale(a_report, RuntimeModule::Scheduler, a_elapsed.epoch);
			return;
		}
		const auto found = coreState_.actors.find(a_elapsed.actor);
		if (found == coreState_.actors.end()) {
			++a_report.rejectedStale;
			RuntimeDiagnostic diagnostic{};
			diagnostic.module = RuntimeModule::Scheduler;
			diagnostic.code = RuntimeDiagnosticCode::StaleRevision;
			diagnostic.epoch = a_elapsed.epoch;
			diagnostic.actor = a_elapsed.actor;
			a_report.diagnostics.push_back(diagnostic);
			return;
		}
		auto& state = found->second;
		const auto rejectRevision = [&]() {
			++a_report.rejectedStale;
			RuntimeDiagnostic diagnostic{};
			diagnostic.module = RuntimeModule::Scheduler;
			diagnostic.code = RuntimeDiagnosticCode::StaleRevision;
			diagnostic.epoch = a_elapsed.epoch;
			diagnostic.actor = a_elapsed.actor;
			a_report.diagnostics.push_back(diagnostic);
			logs::debug("Stale deadline id={} actor={} kind={}",
				a_elapsed.id,
				a_elapsed.actor,
				static_cast<int>(a_elapsed.kind));
		};

		switch (a_elapsed.kind) {
		case DeadlineKind::NaturalDecay:
			if (!a_elapsed.region ||
				RegionIndex(*a_elapsed.region) >= kRegionCount ||
				state.regions[RegionIndex(*a_elapsed.region)].revision != a_elapsed.ownerRevision) {
				rejectRevision();
				return;
			}
			advanceActor(a_elapsed.actor, a_now, nullptr, a_report);
			break;
		case DeadlineKind::TimedClear:
			if (state.revision != a_elapsed.ownerRevision || !state.timedClearAt) {
				rejectRevision();
				return;
			}
			advanceActor(a_elapsed.actor, a_now, nullptr, a_report);
			break;
		case DeadlineKind::SwimSample:
			advanceActor(a_elapsed.actor, a_now, nullptr, a_report);
			break;
		default:
			rejectRevision();
			break;
		}
	}

	void Runtime::sampleEnvironment(const GameTime& a_now, PumpReport& a_report)
	{
		const auto* timer = RE::BSTimer::GetSingleton();
		bool paused = !timer || timer->pauseCount != 0;
		if (const auto* main = RE::Main::GetSingleton()) {
			paused = paused || main->GetRuntimeData().freezeTime;
		}
		if (paused) {
			environmentScanRemaining_ = 0;
			environment_.invalidateAll();
			return;
		}

		if (environmentScanRemaining_ == 0) {
			environmentScanRemaining_ = visual3DReady_.size();
		}
		const auto started = std::chrono::steady_clock::now();
		std::size_t processed = 0;
		while (environmentScanRemaining_ > 0 && processed < 64 &&
			std::chrono::steady_clock::now() - started < std::chrono::milliseconds{ 2 }) {
			const auto next = environmentCursor_.next(visual3DReady_);
			if (next == visual3DReady_.end()) {
				environmentScanRemaining_ = 0;
				break;
			}
			const auto actor = next->first;
			--environmentScanRemaining_;
			++processed;
			auto lease = actorRegistry_.resolve(actor, ResolveNeed::ActorOnly);
			if (!lease) {
				environment_.invalidate(actor);
				continue;
			}
			auto sample = environment_.sample(actor, *lease->actor, a_now);
			if (!sample) {
				continue;
			}
			++perfEnvironmentSamples_;
			advanceActor(actor, sample->at, &*sample, a_report);
		}
	}

	void Runtime::processEnvironmentObserved(const EnvironmentObserved& a_command, const GameTime& a_now, PumpReport& a_report)
	{
		if (!sessionReady_.load(std::memory_order_acquire)) {
			return;
		}
		auto at = a_command.sample.at;
		if (!std::isfinite(at.calendarHours) || !std::isfinite(at.activeSeconds)) {
			at = a_now;
		}
		advanceActor(a_command.sample.actor, at, &a_command.sample, a_report);
	}

	void Runtime::advanceActor(ActorId a_actor, const GameTime& a_now, const EnvironmentSample* a_sample, PumpReport& a_report)
	{
		if (coreState_.actors.find(a_actor) == coreState_.actors.end()) {
			RuntimeDiagnostic diagnostic{};
			diagnostic.module = RuntimeModule::Core;
			diagnostic.code = RuntimeDiagnosticCode::ActorUnavailable;
			diagnostic.epoch = epoch();
			diagnostic.actor = a_actor;
			a_report.diagnostics.push_back(diagnostic);
			return;
		}
		Advance advance{};
		advance.actor = a_actor;
		advance.now = a_now;
		if (a_sample) {
			advance.environment = *a_sample;
		}
		const auto change = Core::apply(coreState_, CoreOp{ std::move(advance) }, settings_, a_now);
		applyChangeSet(change, a_report);
	}

	void Runtime::settleAllWithOldSettings(const GameTime& a_now, PumpReport& a_report)
	{
		std::vector<ActorId> actors;
		actors.reserve(coreState_.actors.size());
		for (const auto& [actor, state] : coreState_.actors) {
			actors.push_back(actor);
		}
		for (const auto actor : actors) {
			advanceActor(actor, a_now, nullptr, a_report);
		}
	}

	void Runtime::applyDeadlineIntents(const ChangeSet& a_change, PumpReport& a_report)
	{
		(void)a_report;
		for (const auto& intent : a_change.deadlines) {
			const auto id = stableDeadlineId(intent.actor, intent.kind, intent.region);
			if (intent.action == DeadlineIntent::Action::Cancel) {
				scheduler_.cancel(id);
				continue;
			}
			if (!intent.due || !std::isfinite(*intent.due)) {
				scheduler_.cancel(id);
				logs::warn("Deadline intent without valid due actor={} kind={}",
					intent.actor,
					static_cast<int>(intent.kind));
				continue;
			}
			Deadline deadline{};
			deadline.id = id;
			deadline.actor = intent.actor;
			deadline.kind = intent.kind;
			deadline.clock = intent.clock;
			deadline.due = *intent.due;
			deadline.epoch = epoch();
			deadline.region = intent.region;
			deadline.ownerRevision = intent.ownerRevision;
			if (scheduler_.upsert(deadline) != DeadlineUpdateStatus::Updated) {
				logs::warn("Scheduler rejected deadline actor={} kind={} clock={} due={}",
					intent.actor,
					static_cast<int>(intent.kind),
					static_cast<int>(intent.clock),
					*intent.due);
			}
		}
	}

	DeadlineId Runtime::stableDeadlineId(ActorId a_actor, DeadlineKind a_kind, const std::optional<Region>& a_region)
	{
		const auto key = std::make_tuple(a_actor, a_kind, a_region);
		const auto found = deadlineIds_.find(key);
		if (found != deadlineIds_.end()) {
			return found->second;
		}
		auto id = nextDeadlineId_++;
		if (id == 0) {
			id = nextDeadlineId_++;
		}
		deadlineIds_.emplace(key, id);
		return id;
	}

	void Runtime::scheduleOverlayRetry(ActorId a_actor, Region a_region)
	{
		const auto visualKey = (a_actor << 3) | static_cast<std::uint64_t>(RegionIndex(a_region));
		dirtyVisualRetries_.defer(visualKey, std::chrono::steady_clock::now() + std::chrono::seconds{ 2 });
	}

	void Runtime::cancelOverlayRetry(ActorId a_actor, Region a_region)
	{
		const auto visualKey = (a_actor << 3) | static_cast<std::uint64_t>(RegionIndex(a_region));
		dirtyVisualRetries_.clear(visualKey);
	}

	void Runtime::processCommand(const RuntimeCommand& a_command, PumpReport& a_report)
	{
		std::visit(
			[&](const auto& command) {
				using T = std::decay_t<decltype(command)>;
				if constexpr (std::is_same_v<T, NativeOrgasm>) {
					processNativeOrgasm(command, a_report);
				} else if constexpr (std::is_same_v<T, SceneNotice>) {
					processSceneNotice(command, a_report);
				} else if constexpr (std::is_same_v<T, PPlusCompleted>) {
					processPPlusCompleted(command, a_report);
				} else if constexpr (std::is_same_v<T, MenuAction>) {
					processMenuAction(command, a_report);
				} else if constexpr (std::is_same_v<T, EnvironmentObserved>) {
					if (command.epoch != epoch()) {
						addStale(a_report, RuntimeModule::Environment, command.epoch);
					} else {
						processEnvironmentObserved(command, currentGameTime(), a_report);
					}
				} else if constexpr (std::is_same_v<T, DeadlineElapsed>) {
					if (command.epoch != epoch()) {
						addStale(a_report, RuntimeModule::Scheduler, command.epoch);
					} else {
						processDeadlineElapsed(command, currentGameTime(), a_report);
					}
				} else if constexpr (std::is_same_v<T, ActorVisualInvalidated>) {
					if (command.epoch != epoch()) {
						addStale(a_report, RuntimeModule::Overlay, command.epoch);
					} else {
						invalidateActorVisuals(command.actor, VisualInvalidation::Actor3DRebuilt);
					}
				} else if constexpr (std::is_same_v<T, OverlayInstalled>) {
					if (command.epoch != epoch()) {
						addStale(a_report, RuntimeModule::Overlay, command.epoch);
					} else if (const auto actor = actorRegistry_.find(command.formId)) {
						invalidateActorVisuals(*actor, VisualInvalidation::RaceMenuReady);
					}
				} else if constexpr (std::is_same_v<T, LoadNotice>) {
					if (command.epoch != epoch()) {
						addStale(a_report, RuntimeModule::Runtime, command.epoch);
					} else {
						logs::debug("Load notice phase={}", static_cast<int>(command.phase));
					}
				}
			},
			a_command);
	}

	void Runtime::processNativeOrgasm(const NativeOrgasm& a_command, PumpReport& a_report)
	{
		if (a_command.epoch != epoch()) {
			addStale(a_report, RuntimeModule::Ingress, a_command.epoch);
			return;
		}
		if (a_command.sourceFormId == kInvalidFormId) {
			RuntimeDiagnostic diagnostic{};
			diagnostic.module = RuntimeModule::Papyrus;
			diagnostic.code = RuntimeDiagnosticCode::InvalidNativeForm;
			diagnostic.epoch = a_command.epoch;
			diagnostic.tid = a_command.tid;
			diagnostic.sequence = a_command.sequence;
			a_report.diagnostics.push_back(diagnostic);
			return;
		}
		if (!sessionReady_.load(std::memory_order_acquire)) {
			RuntimeDiagnostic diagnostic{};
			diagnostic.module = RuntimeModule::PPlus;
			diagnostic.code = RuntimeDiagnosticCode::QueryFailed;
			diagnostic.epoch = a_command.epoch;
			diagnostic.tid = a_command.tid;
			diagnostic.sequence = a_command.sequence;
			diagnostic.form = a_command.sourceFormId;
			a_report.diagnostics.push_back(diagnostic);
			return;
		}
		const auto scene = sceneTids_.find(a_command.tid);
		if (scene == sceneTids_.end() || scene->second.epoch != a_command.epoch) {
			RuntimeDiagnostic diagnostic{};
			diagnostic.module = RuntimeModule::PPlus;
			diagnostic.code = RuntimeDiagnosticCode::QueryFailed;
			diagnostic.epoch = a_command.epoch;
			diagnostic.tid = a_command.tid;
			diagnostic.sequence = a_command.sequence;
			diagnostic.form = a_command.sourceFormId;
			a_report.diagnostics.push_back(diagnostic);
			logs::warn("Orgasm ignored: tid {} is not an active scene", a_command.tid);
			return;
		}

		const auto actorId = actorRegistry_.bind(a_command.sourceFormId);
		if (!actorId) {
			RuntimeDiagnostic diagnostic{};
			diagnostic.module = RuntimeModule::Core;
			diagnostic.code = RuntimeDiagnosticCode::ActorUnavailable;
			diagnostic.epoch = a_command.epoch;
			diagnostic.tid = a_command.tid;
			diagnostic.sequence = a_command.sequence;
			diagnostic.form = a_command.sourceFormId;
			a_report.diagnostics.push_back(diagnostic);
			logs::warn("Orgasm source unavailable form={:08X} tid={}", a_command.sourceFormId, a_command.tid);
			return;
		}

		OrgasmQuery query{};
		query.requestId = nextRequestId_++;
		query.sourceFormId = a_command.sourceFormId;
		query.source = *actorId;
		query.tid = a_command.tid;
		query.sequence = a_command.sequence;
		query.epoch = a_command.epoch;

		const auto start = pplus_.begin(query);
		if (start == QueryStart::Accepted) {
			requestStarted_[query.requestId] = std::chrono::steady_clock::now();
			tidOrder_[a_command.tid].outstanding.insert(a_command.sequence);
			++orgasmsAccepted_;
			background_.requestAfter(std::chrono::milliseconds{ 5200 });
			logs::info(
				"Orgasm query accepted request={} form={:08X} actor={} tid={} sequence={} total={}",
				query.requestId,
				a_command.sourceFormId,
				*actorId,
				a_command.tid,
				a_command.sequence,
				orgasmsAccepted_);
			return;
		}

		RuntimeDiagnostic diagnostic{};
		diagnostic.module = RuntimeModule::PPlus;
		diagnostic.code = start == QueryStart::RejectedCapacity ?
			RuntimeDiagnosticCode::CapacityUnavailable :
			RuntimeDiagnosticCode::QueryFailed;
		diagnostic.epoch = a_command.epoch;
		diagnostic.tid = a_command.tid;
		diagnostic.sequence = a_command.sequence;
		diagnostic.requestId = query.requestId;
		diagnostic.form = a_command.sourceFormId;
		a_report.diagnostics.push_back(diagnostic);
		logs::warn("Orgasm query rejected request={} reason={}", query.requestId, static_cast<int>(start));
	}

	void Runtime::processSceneNotice(const SceneNotice& a_command, PumpReport& a_report)
	{
		if (a_command.epoch != epoch()) {
			addStale(a_report, RuntimeModule::Ingress, a_command.epoch);
			return;
		}
		if (a_command.phase == ScenePhase::Start) {
			const auto [it, inserted] = sceneTids_.try_emplace(
				a_command.tid,
				SceneContext{ a_command.hasPlayer, a_command.epoch });
			if (!inserted) {
				// P+ reuses animation tids. Results from the previous scene must
				// never commit into the new one.
				pplus_.cancelTid(a_command.tid, a_command.epoch);
				tidOrder_.erase(a_command.tid);
				it->second = SceneContext{ a_command.hasPlayer, a_command.epoch };
				logs::warn("Scene start repeated tid={}", a_command.tid);
			} else if (sceneTids_.size() > 256) {
				sceneTids_.erase(it);
				logs::warn("Scene start ignored, tid capacity reached");
			} else {
				++sceneStarts_;
				logs::info("Scene start tid={} hasPlayer={} total={}", a_command.tid, a_command.hasPlayer, sceneStarts_);
			}
		} else {
			pplus_.cancelTid(a_command.tid, a_command.epoch);
			background_.request();
			// Drop ordering state for this scene. A completion that is still in
			// flight will be rejected as unknown rather than adding layers after
			// the scene ended. Completions processed before this End remain valid.
			tidOrder_.erase(a_command.tid);
			if (sceneTids_.erase(a_command.tid) == 0) {
				logs::warn("Scene end for unknown tid={}", a_command.tid);
			} else {
				++sceneEnds_;
				logs::info("Scene end tid={} hasPlayer={} total={}", a_command.tid, a_command.hasPlayer, sceneEnds_);
			}
		}
	}

	void Runtime::processPPlusCompleted(const PPlusCompleted& a_command, PumpReport& a_report)
	{
		if (a_command.epoch != epoch()) {
			addStale(a_report, RuntimeModule::PPlus, a_command.epoch);
			return;
		}
		recordRequestLatency(a_command.requestId);

		Tid tid{};
		IngressSequence sequence{};
		if (const auto* completed = std::get_if<ResolvedOrgasm>(&a_command.outcome)) {
			tid = completed->tid;
			sequence = completed->sequence;
		} else if (const auto* failure = std::get_if<PPlusFailure>(&a_command.outcome)) {
			tid = failure->tid;
			sequence = failure->sequence;
		} else {
			return;
		}

		auto found = tidOrder_.find(tid);
		if (found == tidOrder_.end() || !found->second.outstanding.contains(sequence)) {
			RuntimeDiagnostic diagnostic{};
			diagnostic.module = RuntimeModule::PPlus;
			diagnostic.code = RuntimeDiagnosticCode::QueryFailed;
			diagnostic.epoch = a_command.epoch;
			diagnostic.tid = tid;
			diagnostic.sequence = sequence;
			diagnostic.requestId = a_command.requestId;
			a_report.diagnostics.push_back(diagnostic);
			logs::warn("P+ completion without accepted query request={} tid={} sequence={}",
				a_command.requestId,
				tid,
				sequence);
			return;
		}

		auto& order = found->second;
		order.completed.emplace(sequence, a_command);
		while (!order.outstanding.empty()) {
			const auto expected = *order.outstanding.begin();
			auto ready = order.completed.find(expected);
			if (ready == order.completed.end()) {
				break;
			}
			PPlusCompleted completion = std::move(ready->second);
			order.completed.erase(ready);
			order.outstanding.erase(order.outstanding.begin());
			dispatchPPlusCompletion(completion, a_report);
		}
		if (order.outstanding.empty() && order.completed.empty()) {
			tidOrder_.erase(found);
		}
	}

	void Runtime::dispatchPPlusCompletion(const PPlusCompleted& a_command, PumpReport& a_report)
	{
		if (const auto* completed = std::get_if<ResolvedOrgasm>(&a_command.outcome)) {
			logs::info("P+ query completed request={} tid={} targets={}",
				a_command.requestId,
				completed->tid,
				completed->targets.size());

			AddLayers op{};
			op.source = completed->source;
			op.tid = completed->tid;
			op.sequence = completed->sequence;
			op.selectionSeed = (completed->sequence * 0x9E3779B97F4A7C15ull) ^
				(static_cast<std::uint64_t>(static_cast<std::uint32_t>(completed->tid)) << 32) ^
				completed->source;
			op.targets = completed->targets;
			std::erase_if(op.targets, [this](const InteractionTarget& target) {
				return !actorRegistry_.resolve(target.actor, ResolveNeed::ActorOnly).has_value();
			});
			if (op.targets.empty()) {
				logs::warn("Dropping P+ completion request={} tid={}: all targets are gone",
					a_command.requestId,
					completed->tid);
				return;
			}

			const auto change = Core::apply(coreState_, CoreOp{ std::move(op) }, settings_, currentGameTime());
			applyChangeSet(change, a_report);
		} else if (const auto* failure = std::get_if<PPlusFailure>(&a_command.outcome)) {
			RuntimeDiagnostic diagnostic{};
			diagnostic.module = RuntimeModule::PPlus;
			diagnostic.code = failure->code == PPlusFailureCode::TimedOut ?
				RuntimeDiagnosticCode::QueryTimedOut :
				(failure->code == PPlusFailureCode::Cancelled ?
						RuntimeDiagnosticCode::QueryCancelled :
						RuntimeDiagnosticCode::QueryFailed);
			diagnostic.epoch = a_command.epoch;
			diagnostic.requestId = a_command.requestId;
			diagnostic.tid = failure->tid;
			diagnostic.sequence = failure->sequence;
			a_report.diagnostics.push_back(diagnostic);
			logs::warn("P+ query failed request={} code={}", a_command.requestId, static_cast<int>(failure->code));
		}
	}

	void Runtime::applyChangeSet(const ChangeSet& a_change, PumpReport& a_report)
	{
		std::unordered_set<ActorId> touched;
		for (const auto& target : a_change.regions) {
			touched.insert(target.actor);
		}
		for (const auto& target : a_change.spells) {
			touched.insert(target.actor);
		}
		for (const auto actor : touched) {
			const auto formId = actorRegistry_.formId(actor);
			const auto found = coreState_.actors.find(actor);
			if (formId && found != coreState_.actors.end()) {
				found->second.formId = *formId;
				visual3DReady_.try_emplace(actor, false);
			}
		}

		for (const auto& diagnostic : a_change.diagnostics) {
			RuntimeDiagnostic runtime{};
			runtime.module = RuntimeModule::Core;
			runtime.epoch = epoch();
			runtime.actor = diagnostic.actor;
			switch (diagnostic.code) {
			case CoreDiagnosticCode::AtCapacity:
				runtime.code = RuntimeDiagnosticCode::CapacityUnavailable;
				break;
			case CoreDiagnosticCode::InvalidTarget:
				runtime.code = RuntimeDiagnosticCode::ActorUnavailable;
				break;
			case CoreDiagnosticCode::InvalidRegion:
			case CoreDiagnosticCode::InvalidSettings:
				runtime.code = RuntimeDiagnosticCode::AdapterFailure;
				break;
			}
			a_report.diagnostics.push_back(runtime);
		}

		for (const auto& target : a_change.regions) {
			const auto key = (target.actor << 3) | static_cast<std::uint64_t>(RegionIndex(target.region));
			const auto queued = dirtyVisuals_.find(key);
			const auto retryNow = std::chrono::steady_clock::now();
			if (queued != dirtyVisuals_.end() && queued->second.revision == target.revision &&
				!dirtyVisualRetries_.ready(key, retryNow)) {
				continue;
			}
			if (queued == dirtyVisuals_.end() || queued->second.revision != target.revision) {
				dirtyVisualRetries_.clear(key);
			}
			const auto lease = actorRegistry_.resolve(target.actor, ResolveNeed::ActorWith3D);
			if (!lease) {
				dirtyVisuals_[key] = target;
				scheduleOverlayRetry(target.actor, target.region);
				continue;
			}

			const auto result = overlay_.reconcile(*lease, target, settings_);
			++perfOverlaySubmits_;
			perfOverlaySlots_ += result.slotsApplied;
			if (result.status == OverlayStatus::Applied || result.status == OverlayStatus::NoChange ||
				result.status == OverlayStatus::StaleRevision) {
				dirtyVisuals_.erase(key);
				cancelOverlayRetry(target.actor, target.region);
				continue;
			}
			if (result.status == OverlayStatus::DeferredNo3D ||
				result.status == OverlayStatus::NoOwnedCapacity ||
				result.status == OverlayStatus::ApiUnavailable) {
				dirtyVisuals_[key] = target;
				scheduleOverlayRetry(target.actor, target.region);
				if (result.status == OverlayStatus::NoOwnedCapacity) {
					const auto now = std::chrono::steady_clock::now();
					auto [notice, inserted] = capacityNoticeAt_.try_emplace(key, now);
					if (inserted || now - notice->second >= std::chrono::seconds(5)) {
						notice->second = now;
						RuntimeDiagnostic runtime{};
						runtime.module = RuntimeModule::Overlay;
						runtime.code = RuntimeDiagnosticCode::CapacityUnavailable;
						runtime.epoch = epoch();
						runtime.actor = target.actor;
						a_report.diagnostics.push_back(runtime);
						logs::warn("Overlay slots exhausted actor={} region={} applied={}",
							target.actor,
							static_cast<int>(target.region),
							result.slotsApplied);
					}
				}
				continue;
			}
			if (result.status != OverlayStatus::Applied && result.status != OverlayStatus::NoChange) {
				dirtyVisuals_[key] = target;
				scheduleOverlayRetry(target.actor, target.region);
				RuntimeDiagnostic runtime{};
				runtime.module = RuntimeModule::Overlay;
				runtime.code = result.status == OverlayStatus::StaleRevision ?
					RuntimeDiagnosticCode::StaleRevision :
					RuntimeDiagnosticCode::AdapterFailure;
				runtime.epoch = epoch();
				runtime.actor = target.actor;
				a_report.diagnostics.push_back(runtime);
				logs::warn("Overlay reconcile failed actor={} region={} status={} error={}",
					target.actor,
					static_cast<int>(target.region),
					static_cast<int>(result.status),
					static_cast<int>(result.error));
			}
		}

		if (effectsReady_) {
			for (const auto& target : a_change.spells) {
				const auto queued = dirtyEffects_.find(target.actor);
				const auto retryNow = std::chrono::steady_clock::now();
				if (queued != dirtyEffects_.end() && queued->second.revision == target.revision &&
					!dirtyEffectRetries_.ready(target.actor, retryNow)) {
					continue;
				}
				if (queued == dirtyEffects_.end() || queued->second.revision != target.revision) {
					dirtyEffectRetries_.clear(target.actor);
				}
				const auto lease = actorRegistry_.resolve(target.actor, ResolveNeed::ActorOnly);
				if (!lease) {
					dirtyEffects_[target.actor] = target;
					dirtyEffectRetries_.defer(target.actor, std::chrono::steady_clock::now() + std::chrono::seconds{ 2 });
					continue;
				}
				const auto result = effects_.reconcile(*lease, target);
				if (result.status == EffectStatus::Applied || result.status == EffectStatus::NoChange) {
					dirtyEffects_.erase(target.actor);
					dirtyEffectRetries_.clear(target.actor);
					continue;
				}
				if (result.status == EffectStatus::StaleRevision) {
					// A newer SpellTarget owns the actor; do not replay this one.
					continue;
				}
				dirtyEffects_[target.actor] = target;
				dirtyEffectRetries_.defer(target.actor, std::chrono::steady_clock::now() + std::chrono::seconds{ 2 });
				if (result.status != EffectStatus::DeferredActorUnavailable) {
					RuntimeDiagnostic runtime{};
					runtime.module = RuntimeModule::Effects;
					runtime.code = result.error == EffectErrorCode::StaleRevision ?
						RuntimeDiagnosticCode::StaleRevision :
						RuntimeDiagnosticCode::AdapterFailure;
					runtime.epoch = epoch();
					runtime.actor = target.actor;
					a_report.diagnostics.push_back(runtime);
					logs::warn("Effects reconcile failed actor={} status={} error={}",
						target.actor,
						static_cast<int>(result.status),
						static_cast<int>(result.error));
				}
			}
		}

		applyDeadlineIntents(a_change, a_report);
	}

	void Runtime::processDirtyVisuals(PumpReport& a_report, std::size_t a_maxTargets)
	{
		// A loaded save restores node overrides from RaceMenu's co-save before
		// the actor has 3D. Watch for the first 3D frame and replay all current
		// logical region targets (including clearing stale slots).
		const auto scanStarted = std::chrono::steady_clock::now();
		const auto scanCount = std::min<std::size_t>(visual3DReady_.size(), 64);
		for (std::size_t i = 0; i < scanCount &&
			std::chrono::steady_clock::now() - scanStarted < std::chrono::milliseconds{ 1 }; ++i) {
			auto next = actor3DCursor_.next(visual3DReady_);
			const auto actor = next->first;
			const auto lease = actorRegistry_.resolve(actor, ResolveNeed::ActorWith3D);
			if (!lease) {
				next->second = false;
				continue;
			}
			if (!next->second) {
				next->second = true;
				invalidateActorVisuals(actor, VisualInvalidation::Actor3DRebuilt);
			}
		}

		const auto started = std::chrono::steady_clock::now();
		constexpr auto kDirtyBudget = std::chrono::milliseconds{ 2 };
		std::vector<std::uint64_t> resolved;
		std::size_t examined = 0;
		std::size_t attempted = 0;
		const auto pending = dirtyVisuals_.size();
		const auto now = std::chrono::steady_clock::now();
		while (examined < pending && attempted < a_maxTargets) {
			if (std::chrono::steady_clock::now() - started >= kDirtyBudget) {
				break;
			}
			++examined;
			const auto next = visualCursor_.next(dirtyVisuals_);
			const auto& [key, target] = *next;
			if (!dirtyVisualRetries_.ready(key, now)) {
				continue;
			}
			++attempted;
			const auto actor = coreState_.actors.find(target.actor);
			if (actor == coreState_.actors.end() ||
				actor->second.regions[RegionIndex(target.region)].revision != target.revision) {
				resolved.push_back(key);
				cancelOverlayRetry(target.actor, target.region);
				continue;
			}
			const auto lease = actorRegistry_.resolve(target.actor, ResolveNeed::ActorWith3D);
			if (!lease) {
				scheduleOverlayRetry(target.actor, target.region);
				continue;
			}
			const auto result = overlay_.reconcile(*lease, target, settings_);
			++perfOverlaySubmits_;
			perfOverlaySlots_ += result.slotsApplied;
			if (result.status != OverlayStatus::Applied && result.status != OverlayStatus::NoChange &&
				result.status != OverlayStatus::StaleRevision) {
				scheduleOverlayRetry(target.actor, target.region);
				continue;
			}
			resolved.push_back(key);
			cancelOverlayRetry(target.actor, target.region);
		}
		for (const auto key : resolved) {
			dirtyVisuals_.erase(key);
		}
		if (dirtyVisualRetries_.hasReady(dirtyVisuals_, std::chrono::steady_clock::now())) {
			a_report.deferred = true;
		}

		processDirtyEffects(a_report, 64);
	}

	void Runtime::processDirtyEffects(PumpReport& a_report, std::size_t a_maxTargets)
	{
		if (!effectsReady_) {
			return;
		}

		const auto started = std::chrono::steady_clock::now();
		constexpr auto kDirtyBudget = std::chrono::milliseconds{ 2 };
		std::vector<ActorId> resolved;
		std::size_t examined = 0;
		std::size_t attempted = 0;
		const auto pending = dirtyEffects_.size();
		const auto now = std::chrono::steady_clock::now();
		while (examined < pending && attempted < a_maxTargets) {
			if (std::chrono::steady_clock::now() - started >= kDirtyBudget) {
				break;
			}
			++examined;
			const auto next = effectCursor_.next(dirtyEffects_);
			const auto& [actor, target] = *next;
			if (!dirtyEffectRetries_.ready(actor, now)) {
				continue;
			}
			++attempted;
			const auto lease = actorRegistry_.resolve(actor, ResolveNeed::ActorOnly);
			if (!lease) {
				dirtyEffectRetries_.defer(actor, std::chrono::steady_clock::now() + std::chrono::seconds{ 2 });
				continue;
			}
			const auto result = effects_.reconcile(*lease, target);
			if (result.status == EffectStatus::Applied ||
				result.status == EffectStatus::NoChange ||
				result.status == EffectStatus::StaleRevision) {
				resolved.push_back(actor);
				dirtyEffectRetries_.clear(actor);
			} else {
				dirtyEffectRetries_.defer(actor, std::chrono::steady_clock::now() + std::chrono::seconds{ 2 });
			}
		}
		for (const auto actor : resolved) {
			dirtyEffects_.erase(actor);
		}
		if (dirtyEffectRetries_.hasReady(dirtyEffects_, std::chrono::steady_clock::now())) {
			a_report.deferred = true;
		}
	}

	void Runtime::enqueueCurrentTarget(ActorId a_actor, Region a_region, bool a_resetRetry)
	{
		const auto found = coreState_.actors.find(a_actor);
		if (found == coreState_.actors.end()) {
			return;
		}
		const auto& region = found->second.regions[RegionIndex(a_region)];
		RegionTarget target{};
		target.actor = a_actor;
		target.region = a_region;
		target.revision = region.revision;
		target.layers.reserve(region.layers.size());
		for (const auto& layer : region.layers) {
			target.layers.push_back(LayerVisual{ layer.id, layer.textureIndex, layer.variant });
		}
		const auto key = (a_actor << 3) | static_cast<std::uint64_t>(RegionIndex(a_region));
		const auto queued = dirtyVisuals_.find(key);
		if (a_resetRetry || queued == dirtyVisuals_.end() || queued->second.revision != target.revision) {
			dirtyVisualRetries_.clear(key);
		}
		dirtyVisuals_[key] = std::move(target);
	}

	void Runtime::invalidateActorVisuals(ActorId a_actor, VisualInvalidation a_reason, bool a_resetRetry)
	{
		overlay_.invalidateActor(a_actor, a_reason);
		for (const auto region : kRegions) {
			enqueueCurrentTarget(a_actor, region, a_resetRetry);
		}
		effects_.invalidate(a_actor);
		const auto found = coreState_.actors.find(a_actor);
		if (found != coreState_.actors.end()) {
			SpellTarget target{};
			target.actor = a_actor;
			target.revision = found->second.revision;
			const auto& state = found->second;
			const auto oral = state.regions[RegionIndex(Region::Face)].layers.size();
			const auto vaginal = state.regions[RegionIndex(Region::BodyVaginal)].layers.size();
			const auto anal = state.regions[RegionIndex(Region::BodyAnal)].layers.size();
			const auto level = [&](std::size_t count) -> std::uint8_t {
				if (count >= settings_.spellLevel2Threshold) {
					return 2;
				}
				if (count >= settings_.spellLevel1Threshold) {
					return 1;
				}
				return 0;
			};
			target.desired.combinationIndex = static_cast<std::uint8_t>(
				level(anal) * 9 + level(vaginal) * 3 + level(oral));
			target.desired.enabled = settings_.effectsEnabled;
			const auto queued = dirtyEffects_.find(a_actor);
			if (a_resetRetry || queued == dirtyEffects_.end() || queued->second.revision != target.revision) {
				dirtyEffectRetries_.clear(a_actor);
			}
			dirtyEffects_[a_actor] = target;
		}
		background_.request();
	}

	void Runtime::processMenuAction(const MenuAction& a_action, PumpReport& a_report)
	{
		std::visit(
			[&](const auto& action) {
				using T = std::decay_t<decltype(action)>;
				if (action.epoch != epoch()) {
					addStale(a_report, RuntimeModule::Menu, action.epoch);
					return;
				}
				const auto now = currentGameTime();
				if constexpr (std::is_same_v<T, RefreshActorCandidates>) {
					refreshActorCandidates(false);
				} else if constexpr (std::is_same_v<T, SelectCrosshairActor>) {
					refreshActorCandidates(true);
				} else if constexpr (std::is_same_v<T, ApplySettings>) {
					auto validation = Settings::validate(action.settings);
					if (!validation.valid) {
						menuFeedback_.apply = SettingsApplyStatus::Rejected;
						menuFeedback_.write = SettingsWriteStatus::NotRequested;
						menuFeedback_.issues = validation.issues;
						RuntimeDiagnostic diagnostic{};
						diagnostic.module = RuntimeModule::Settings;
						diagnostic.code = RuntimeDiagnosticCode::SettingsRejected;
						diagnostic.epoch = action.epoch;
						a_report.diagnostics.push_back(diagnostic);
						logs::warn("Settings rejected with {} issue(s)", validation.issues.size());
						return;
					}

					const bool changed = !(action.settings == settings_);
					SettingsWriteStatus writeStatus = SettingsWriteStatus::NotRequested;
					if (changed) {
						// Settle every actor with the settings that were in force when
						// the elapsed hours were accumulated, then replace and rebuild.
						settleAllWithOldSettings(now, a_report);
						settings_ = action.settings;
						dirtyVisualRetries_.clear();
						dirtyEffectRetries_.clear();
						const auto change = Core::apply(coreState_, CoreOp{ SettingsChanged{} }, settings_, now);
						applyChangeSet(change, a_report);
					}

					if (action.saveToDisk) {
						const auto result = Settings::save(
							dataPath() / "SKSE" / "Plugins" / "SLACS2NG.json",
							settings_);
						if (result.status == SettingsSaveStatus::Saved) {
							writeStatus = SettingsWriteStatus::Saved;
							logs::info("Settings written to SLACS2NG.json");
						} else {
							writeStatus = SettingsWriteStatus::Failed;
							RuntimeDiagnostic diagnostic{};
							diagnostic.module = RuntimeModule::Settings;
							diagnostic.code = RuntimeDiagnosticCode::PersistenceFailure;
							diagnostic.epoch = action.epoch;
							a_report.diagnostics.push_back(diagnostic);
							logs::error("Settings write failed status={}; applied settings remain active",
								static_cast<int>(result.status));
						}
					}

					menuFeedback_.apply = changed ? SettingsApplyStatus::Applied : SettingsApplyStatus::Unchanged;
					menuFeedback_.write = writeStatus;
					menuFeedback_.issues = validation.issues;
					logs::info("Settings applied epoch={} changed={} write={}",
						action.epoch,
						changed,
						static_cast<int>(writeStatus));
				} else if constexpr (std::is_same_v<T, ClearActor>) {
					if (!coreState_.actors.contains(action.actor)) {
						RuntimeDiagnostic diagnostic{};
						diagnostic.module = RuntimeModule::Menu;
						diagnostic.code = RuntimeDiagnosticCode::ActorUnavailable;
						diagnostic.epoch = action.epoch;
						diagnostic.actor = action.actor;
						a_report.diagnostics.push_back(diagnostic);
						return;
					}
					std::vector<Region> regions(kRegions.begin(), kRegions.end());
					const auto change = Core::apply(
						coreState_,
						CoreOp{ ClearRegions{ action.actor, std::move(regions) } },
						settings_,
						now);
					applyChangeSet(change, a_report);
					logs::info("Menu clear actor={}", action.actor);
				} else if constexpr (std::is_same_v<T, RefreshActor>) {
					if (!coreState_.actors.contains(action.actor)) {
						RuntimeDiagnostic diagnostic{};
						diagnostic.module = RuntimeModule::Menu;
						diagnostic.code = RuntimeDiagnosticCode::ActorUnavailable;
						diagnostic.epoch = action.epoch;
						diagnostic.actor = action.actor;
						a_report.diagnostics.push_back(diagnostic);
						return;
					}
					invalidateActorVisuals(action.actor, VisualInvalidation::Actor3DRebuilt, true);
					logs::info("Menu refresh actor={}", action.actor);
				} else if constexpr (std::is_same_v<T, TestAdd>) {
					if (!actorRegistry_.resolve(action.actor, ResolveNeed::ActorOnly)) {
						RuntimeDiagnostic diagnostic{};
						diagnostic.module = RuntimeModule::Menu;
						diagnostic.code = RuntimeDiagnosticCode::ActorUnavailable;
						diagnostic.epoch = action.epoch;
						diagnostic.actor = action.actor;
						a_report.diagnostics.push_back(diagnostic);
						return;
					}
					const auto total = std::clamp<std::uint8_t>(action.count, 1, 5);
					auto regions = action.regions;
					if (regions.empty()) {
						regions.assign(kRegions.begin(), kRegions.end());
					}
					for (std::uint8_t i = 0; i < total; ++i) {
						const auto sequence = nextTestSequence_++;
						AddLayers op{};
						op.tid = -1;
						op.sequence = sequence;
						op.selectionSeed = sequence * 0x9E3779B97F4A7C15ull ^ action.actor;
						op.targets.push_back(InteractionTarget{ action.actor, regions, action.variant });
						const auto change = Core::apply(coreState_, CoreOp{ std::move(op) }, settings_, now);
						applyChangeSet(change, a_report);
					}
					logs::info("Menu test add actor={} regions={} layers={}", action.actor, regions.size(), total);
				} else if constexpr (std::is_same_v<T, TestClear>) {
					if (!coreState_.actors.contains(action.actor)) {
						RuntimeDiagnostic diagnostic{};
						diagnostic.module = RuntimeModule::Menu;
						diagnostic.code = RuntimeDiagnosticCode::ActorUnavailable;
						diagnostic.epoch = action.epoch;
						diagnostic.actor = action.actor;
						a_report.diagnostics.push_back(diagnostic);
						return;
					}
					auto regions = action.regions;
					if (regions.empty()) {
						regions.assign(kRegions.begin(), kRegions.end());
					}
					const auto change = Core::apply(
						coreState_,
						CoreOp{ ClearRegions{ action.actor, std::move(regions) } },
						settings_,
						now);
					applyChangeSet(change, a_report);
					logs::info("Menu test clear actor={}", action.actor);
				}
			},
			a_action);
	}

	void Runtime::processQuestRetry()
	{
		if (!questRetryPending_ || !sessionReady_.load(std::memory_order_acquire)) {
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		if (now < nextQuestRetry_) {
			scheduler_.upsertSteady(kQuestRetryDeadlineId, nextQuestRetry_);
			return;
		}
		if (questBridge_.trySubscribe()) {
			questRetryPending_ = false;
			logs::info("Quest events subscribed");
			return;
		}
		++questRetryAttempts_;
		nextQuestRetry_ = now + std::chrono::milliseconds{ 500 };
		if (questRetryAttempts_ >= 40) {
			questRetryPending_ = false;
			logs::error("Quest subscription unavailable after {} attempts", questRetryAttempts_);
		} else if (questRetryAttempts_ == 1 || questRetryAttempts_ % 10 == 0) {
			logs::warn("Quest subscription pending attempt={}", questRetryAttempts_);
		}
		if (questRetryPending_) {
			scheduler_.upsertSteady(kQuestRetryDeadlineId, nextQuestRetry_);
		}
	}

	void Runtime::scheduleQuestRetry()
	{
		questRetryAttempts_ = 0;
		questRetryPending_ = true;
		nextQuestRetry_ = std::chrono::steady_clock::now();
		scheduler_.upsertSteady(kQuestRetryDeadlineId, nextQuestRetry_);
		background_.request();
	}

	void Runtime::addStale(PumpReport& a_report, RuntimeModule a_module, SessionEpoch a_epoch)
	{
		++a_report.rejectedStale;
		RuntimeDiagnostic diagnostic{};
		diagnostic.module = a_module;
		diagnostic.code = RuntimeDiagnosticCode::StaleEpoch;
		diagnostic.epoch = a_epoch;
		a_report.diagnostics.push_back(diagnostic);
	}

	void Runtime::refreshActorCandidates(bool a_crosshairOnly)
	{
		auto candidates = a_crosshairOnly ? std::vector<ActorCandidate>{} : GetLoadedActorCandidates();
		if (a_crosshairOnly) {
			const auto target = GetCrosshairActorCandidate();
			if (!target) {
				actorFeedback_ = "准星未指向已加载的成年 NPC。";
				return;
			}
			candidates.push_back(*target);
		} else {
			actorCandidates_.clear();
		}
		for (const auto& candidate : candidates) {
			const auto actor = candidate.actor.get();
			if (!actor) {
				continue;
			}
			const auto id = actorRegistry_.bind(actor->GetFormID());
			if (!id) {
				continue;
			}
			TrackedActorRow row{};
			row.actor = *id;
			row.formId = actor->GetFormID();
			row.displayName = actorDisplayName(row.formId);
			row.has3D = actor->Is3DLoaded();
			row.tracked = false;
			row.isFemale = candidate.isFemale;
			row.isPlayer = candidate.isPlayer;
			actorCandidates_.push_back(std::move(row));
			if (a_crosshairOnly) {
				selectedCandidate_ = *id;
				++selectionRevision_;
			}
		}
		std::sort(actorCandidates_.begin(), actorCandidates_.end(), [](const auto& a_left, const auto& a_right) {
			return a_left.actor < a_right.actor;
		});
		actorCandidates_.erase(std::unique(actorCandidates_.begin(), actorCandidates_.end(), [](const auto& a_left, const auto& a_right) {
			return a_left.actor == a_right.actor;
		}), actorCandidates_.end());
		actorFeedback_ = a_crosshairOnly ? "已选中准星角色。" :
			"已刷新已加载 NPC：" + std::to_string(actorCandidates_.size()) + " 个候选。";
	}

	void Runtime::publishMenuSnapshot()
	{
		MenuSnapshot snapshot{};
		snapshot.epoch = epoch();
		snapshot.revision = ++menuRevision_;
		snapshot.sessionReady = sessionReady_.load(std::memory_order_acquire);
		snapshot.overlayCapacities = overlay_.capacities();
		snapshot.settings = settings_;
		snapshot.settingsFeedback = menuFeedback_;
		snapshot.selectedCandidate = selectedCandidate_;
		snapshot.selectionRevision = selectionRevision_;
		snapshot.actorFeedback = actorFeedback_;
		snapshot.actors.reserve(coreState_.actors.size() + actorCandidates_.size() + 1);
		for (const auto& [actorId, state] : coreState_.actors) {
			TrackedActorRow row{};
			row.actor = actorId;
			row.formId = state.formId;
			if (row.formId == kInvalidFormId) {
				if (const auto form = actorRegistry_.formId(actorId)) {
					row.formId = *form;
				}
			}
			row.displayName = actorDisplayName(row.formId);
			for (std::size_t i = 0; i < kRegionCount; ++i) {
				row.layers[i] = static_cast<std::uint8_t>(state.regions[i].layers.size());
				const auto key = (actorId << 3) | static_cast<std::uint64_t>(i);
				if (dirtyVisuals_.find(key) != dirtyVisuals_.end()) {
					row.visualDegraded = true;
				}
			}
			const auto ready = visual3DReady_.find(actorId);
			row.has3D = ready != visual3DReady_.end() && ready->second;
			snapshot.actors.push_back(std::move(row));
		}
		if (snapshot.sessionReady) {
			for (const auto& row : actorCandidates_) {
				if (!coreState_.actors.contains(row.actor)) {
					snapshot.actors.push_back(row);
				}
			}
			if (const auto player = RE::PlayerCharacter::GetSingleton()) {
				const auto id = actorRegistry_.bind(player->GetFormID());
				const bool cached = id && std::binary_search(actorCandidates_.begin(), actorCandidates_.end(),
					TrackedActorRow{ .actor = *id }, [](const auto& a_left, const auto& a_right) {
						return a_left.actor < a_right.actor;
					});
				if (id && !cached && !coreState_.actors.contains(*id)) {
					TrackedActorRow row{};
					row.actor = *id;
					row.formId = player->GetFormID();
					row.displayName = actorDisplayName(row.formId);
					row.has3D = player->Is3DLoaded();
					row.tracked = false;
					row.isPlayer = true;
					snapshot.actors.push_back(std::move(row));
				}
			}
		}
		snapshot.diagnostics.reserve(menuDiagnostics_.size());
		for (const auto& diagnostic : menuDiagnostics_) {
			snapshot.diagnostics.push_back(DiagnosticRow{ diagnostic, FormatDiagnostic(diagnostic) });
		}
		Menu::publish(std::move(snapshot));
	}

	void Runtime::appendMenuDiagnostics(const PumpReport& a_report)
	{
		for (const auto& diagnostic : a_report.diagnostics) {
			const bool duplicate = !menuDiagnostics_.empty() &&
				menuDiagnostics_.back().module == diagnostic.module &&
				menuDiagnostics_.back().code == diagnostic.code &&
				menuDiagnostics_.back().actor == diagnostic.actor &&
				menuDiagnostics_.back().form == diagnostic.form;
			if (!duplicate) {
				menuDiagnostics_.push_back(diagnostic);
				while (menuDiagnostics_.size() > 32) {
					menuDiagnostics_.pop_front();
				}
			}
		}
	}

	void Runtime::recordPumpTiming(SteadyTime a_started, const PumpReport& a_report)
	{
		const auto now = std::chrono::steady_clock::now();
		const auto micros = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(now - a_started).count());
		++perfPumpCount_;
		perfPumpMicrosTotal_ += micros;
		perfPumpMicrosMax_ = std::max(perfPumpMicrosMax_, micros);
		(void)a_report;

		if (perfWindowStart_ == SteadyTime{}) {
			perfWindowStart_ = now;
			return;
		}
		if (now - perfWindowStart_ < std::chrono::seconds{ 60 }) {
			return;
		}
		perfWindowStart_ = now;
		const auto average = perfPumpCount_ > 0 ? perfPumpMicrosTotal_ / perfPumpCount_ : 0;
		const auto requestAverage = perfRequestCount_ > 0 ?
			perfRequestLatencyMicrosTotal_ / perfRequestCount_ :
			0;
		logs::info(
			"Perf actors={} envSamples={} overlaySubmits={} overlaySlots={} pumps={} pumpAvgUs={} pumpMaxUs={} queueMax={} requests={} reqAvgUs={} reqMaxUs={}",
			coreState_.actors.size(),
			perfEnvironmentSamples_,
			perfOverlaySubmits_,
			perfOverlaySlots_,
			perfPumpCount_,
			average,
			perfPumpMicrosMax_,
			perfMaxQueueDepth_,
			perfRequestCount_,
			requestAverage,
			perfRequestLatencyMicrosMax_);
	}

	void Runtime::recordRequestLatency(RequestId a_requestId)
	{
		const auto found = requestStarted_.find(a_requestId);
		if (found == requestStarted_.end()) {
			return;
		}
		const auto micros = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - found->second)
				.count());
		requestStarted_.erase(found);
		++perfRequestCount_;
		perfRequestLatencyMicrosTotal_ += micros;
		perfRequestLatencyMicrosMax_ = std::max(perfRequestLatencyMicrosMax_, micros);
	}

	std::string Runtime::actorDisplayName(FormId a_formId)
	{
		if (a_formId == kInvalidFormId) {
			return "未知角色";
		}
		if (const auto found = displayNames_.find(a_formId); found != displayNames_.end()) {
			return found->second;
		}
		auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_formId);
		if (!actor) {
			return "未知角色";
		}
		std::string name = actor->GetDisplayFullName();
		if (name.empty()) {
			name = "未命名角色";
		}
		displayNames_[a_formId] = name;
		return name;
	}

	void Runtime::publishSaveSnapshot()
	{
		auto snapshot = Serialization::makeSnapshot(coreState_, currentGameTime());
		snapshot.epoch = epoch();
		saveSnapshot_.store(std::make_shared<const SaveSnapshot>(std::move(snapshot)), std::memory_order_release);
	}

	void Runtime::saveTo(SKSE::SerializationInterface& a_intfc) const
	{
		auto snapshot = saveSnapshot_.load(std::memory_order_acquire);
		if (!snapshot || snapshot->epoch != epoch()) {
			SaveSnapshot empty{};
			empty.epoch = epoch();
			snapshot = std::make_shared<const SaveSnapshot>(std::move(empty));
		}
		const auto status = Serialization::save(a_intfc, *snapshot);
		if (!status.success) {
			logs::error("Serialization save failed error={}", static_cast<int>(status.error));
		}
	}

	void Runtime::onMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (!a_message) {
			return;
		}
		auto& runtime = instance();
		try {
			switch (a_message->type) {
			case SKSE::MessagingInterface::kPostPostLoad:
				Menu::install();
				break;
			case SKSE::MessagingInterface::kDataLoaded:
				// PostPostLoad normally registers the pages; retry here in case
				// the framework DLL became available only after our first call.
				Menu::install();
				runtime.initialize();
				break;
			case SKSE::MessagingInterface::kNewGame:
				runtime.beginSession(SessionReason::NewGame);
				runtime.onSessionReady();
				break;
			case SKSE::MessagingInterface::kPreLoadGame:
				runtime.beginSession(SessionReason::PreLoadGame);
				break;
			case SKSE::MessagingInterface::kPostLoadGame:
				// SKSE dispatches the bool result as (void*)true/false with dataLen 1.
				// It is an integer-cast value, not a pointer to bool.
				if (a_message->dataLen >= 1 && a_message->data != nullptr) {
					runtime.requestLoadCommit();
				} else {
					runtime.handleLoadFailure();
				}
				break;
			default:
				break;
			}
		} catch (const std::exception& e) {
			logs::error("Messaging callback exception: {}", e.what());
		} catch (...) {
			logs::error("Messaging callback unknown exception");
		}
	}

	void Runtime::onSave(SKSE::SerializationInterface* a_intfc)
	{
		try {
			if (a_intfc) {
				instance().saveTo(*a_intfc);
			}
		} catch (const std::exception& e) {
			logs::error("Serialization save exception: {}", e.what());
		} catch (...) {
			logs::error("Serialization save unknown exception");
		}
	}

	void Runtime::onLoad(SKSE::SerializationInterface* a_intfc)
	{
		try {
			if (a_intfc) {
				instance().onLoadStaging(Serialization::load(*a_intfc));
			}
		} catch (const std::exception& e) {
			logs::error("Serialization load exception: {}", e.what());
		} catch (...) {
			logs::error("Serialization load unknown exception");
		}
	}

	void Runtime::onRevert(SKSE::SerializationInterface*)
	{
		try {
			auto& runtime = instance();
			runtime.beginSession(SessionReason::Revert);
			if (!runtime.loadExpected_.load(std::memory_order_acquire)) {
				runtime.onSessionReady();
			}
		} catch (const std::exception& e) {
			logs::error("Serialization revert exception: {}", e.what());
		} catch (...) {
			logs::error("Serialization revert unknown exception");
		}
	}

	void Runtime::onFormDelete(RE::VMHandle a_handle)
	{
		try {
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (!policy || a_handle == policy->EmptyHandle()) {
				return;
			}
			auto* form = policy->GetObjectForHandle(RE::FormType::ActorCharacter, a_handle);
			if (!form) {
				// The delete callback carries only a VM handle; the policy needs
				// the concrete VM type to resolve the object.
				form = policy->GetObjectForHandle(RE::FormType::Quest, a_handle);
			}
			if (!form) {
				return;
			}
			if (form->GetFormType() == RE::FormType::Quest) {
				instance().questBridge_.invalidate();
				return;
			}
			if (form->GetFormType() == RE::FormType::ActorCharacter) {
				auto& runtime = instance();
				std::lock_guard lock(runtime.pumpMutex_);
				runtime.removeActorForm(static_cast<FormId>(form->GetFormID()));
			}
		} catch (const std::exception& e) {
			logs::error("Form delete exception: {}", e.what());
		} catch (...) {
			logs::error("Form delete unknown exception");
		}
	}
}
