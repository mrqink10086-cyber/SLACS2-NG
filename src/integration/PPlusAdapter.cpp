#include "integration/PPlusAdapter.h"
#include "integration/Followers.h"
#include "integration/PPlusMapping.h"

#include <RE/Skyrim.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <string_view>

#undef GetObject
#include <utility>

namespace slacs
{
	namespace
	{
		constexpr std::string_view kFrameworkPlugin = "SexLab.esm";
		constexpr RE::FormID kFrameworkLocalFormId = 0x000D62;
		constexpr std::string_view kOurPlugin = "SLACS_2_Plus_SCO.esp";
		constexpr RE::FormID kMegaRaceListLocalFormId = 0x00004E66;
		constexpr auto kQueryTimeout = std::chrono::seconds{ 5 };
		constexpr std::size_t kMaxPending = 256;

		enum class QueryStep : std::uint8_t
		{
			GetThread,
			Positions,
			Sex,
			InteractionRegistered,
			Interactions,
			Tags,
			Finalize,
		};

		enum TagIndex : std::size_t
		{
			TagVaginal,
			TagPussy,
			TagOral,
			TagBlowjob,
			TagAnal,
			TagHandjob,
			TagFootjob,
			TagBoobjob,
			TagBreast,
			TagTribadism,
			TagRubbing,
			TagCount,
		};

		constexpr std::array<std::string_view, TagCount> kTagNames{
			"Vaginal", "Pussy", "Oral", "Blowjob", "Anal", "Handjob",
			"Footjob", "Boobjob", "Breast", "Tribadism", "Rubbing"
		};

		RE::BSScript::IVirtualMachine* GetVM()
		{
			auto* skyrimVM = RE::SkyrimVM::GetSingleton();
			return skyrimVM ? skyrimVM->impl.get() : nullptr;
		}

		RE::VMHandle FrameworkHandle(RE::BSScript::IVirtualMachine* a_vm)
		{
			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			auto* quest = dataHandler ?
				dataHandler->LookupForm<RE::TESQuest>(kFrameworkLocalFormId, kFrameworkPlugin.data()) :
				nullptr;
			if (!quest || !a_vm) {
				return 0;
			}
			auto* policy = a_vm->GetObjectHandlePolicy();
			return policy ? policy->GetHandleForObject(RE::FormType::Quest, quest) : 0;
		}

		RE::Actor* LookupActor(FormId a_formId)
		{
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_formId);
			return actor && !actor->IsDeleted() ? actor : nullptr;
		}

		bool IsMegaRace(const Settings& a_settings, RE::Actor* a_source)
		{
			if (a_settings.megaCumForAll) {
				return true;
			}
			if (!a_source) {
				return false;
			}
			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			auto* list = dataHandler ?
				dataHandler->LookupForm<RE::BGSListForm>(kMegaRaceListLocalFormId, kOurPlugin.data()) :
				nullptr;
			return list && list->HasForm(a_source->GetRace());
		}
	}

	struct PPlusAdapter::RequestState
	{
		RequestId id{};
		FormId sourceFormId{};
		ActorId source{};
		Tid tid{};
		IngressSequence sequence{};
		SessionEpoch epoch{};
		SteadyTime started{};
		std::recursive_mutex mutex;

		QueryStep step{ QueryStep::GetThread };
		std::size_t cursor{};
		bool waiting{};
		std::uint64_t dispatchToken{};
		std::atomic<bool> cancelRequested{ false };
		std::atomic<bool> terminal{ false };
		std::atomic<bool> posted{ false };
		std::atomic<bool> posting{ false };
		bool failed{};
		PPlusFailureCode failure{ PPlusFailureCode::InvalidQueryData };

		RE::VMHandle thread{};
		std::vector<FormId> positions;
		std::vector<std::int32_t> sexes;
		std::size_t sourcePosition{ static_cast<std::size_t>(-1) };
		std::vector<std::size_t> pairs;
		bool interactionRegistered{};
		std::vector<std::vector<std::int32_t>> interactions;
		std::array<bool, TagCount> tags{};

		ResolvedOrgasm resolved;
	};

	struct PPlusAdapter::CallbackGate
	{
		std::mutex mutex;
		PPlusAdapter* owner{};
		bool closed{};
	};

	struct PPlusAdapter::QueryCallback final : RE::BSScript::IStackCallbackFunctor
	{
		QueryCallback(
			std::shared_ptr<CallbackGate> a_gate,
			std::shared_ptr<RequestState> a_state,
			QueryStep a_step,
			std::uint64_t a_token) :
			gate(std::move(a_gate)),
			state(std::move(a_state)),
			step(a_step),
			token(a_token)
		{}

		void operator()(RE::BSScript::Variable a_result) override
		{
			try {
				std::lock_guard lock(gate->mutex);
				if (gate->closed || !gate->owner) {
					return;
				}
				gate->owner->handleResult(state, static_cast<int>(step), token, a_result);
			} catch (const std::exception& e) {
				logs::error("P+ query callback exception: {}", e.what());
				std::lock_guard lock(gate->mutex);
				if (!gate->closed && gate->owner) {
					gate->owner->fail(state, PPlusFailureCode::InvalidQueryData);
				}
			} catch (...) {
				logs::error("P+ query callback unknown exception");
				std::lock_guard lock(gate->mutex);
				if (!gate->closed && gate->owner) {
					gate->owner->fail(state, PPlusFailureCode::InvalidQueryData);
				}
			}
		}

		bool CanSave() const override
		{
			return false;
		}

		void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override
		{}

		std::shared_ptr<CallbackGate> gate;
		std::shared_ptr<RequestState> state;
		QueryStep step;
		std::uint64_t token;
	};

	PPlusAdapter::PPlusAdapter() :
		callbackGate_(std::make_shared<CallbackGate>())
	{
		callbackGate_->owner = this;
	}

	PPlusAdapter::~PPlusAdapter()
	{
		std::lock_guard lock(callbackGate_->mutex);
		callbackGate_->closed = true;
		callbackGate_->owner = nullptr;
	}

	void PPlusAdapter::setDependencies(PPlusDependencies a_dependencies)
	{
		std::lock_guard lock(mutex_);
		dependencies_ = std::move(a_dependencies);
	}

	QueryStart PPlusAdapter::begin(const OrgasmQuery& a_request)
	{
		PPlusDependencies dependencies;
		{
			std::lock_guard lock(mutex_);
			if (requests_.size() >= kMaxPending) {
				return QueryStart::RejectedCapacity;
			}
			dependencies = dependencies_;
		}

		if (!dependencies.registry || !dependencies.settings || !dependencies.epoch ||
			!dependencies.reserveResult || !dependencies.releaseResult || !dependencies.postResult) {
			return QueryStart::RejectedUnavailable;
		}
		if (dependencies.epoch() != a_request.epoch) {
			return QueryStart::RejectedNotRunning;
		}
		if (a_request.tid < 0 || a_request.source == kInvalidActorId || a_request.sourceFormId == kInvalidFormId) {
			return QueryStart::RejectedUnavailable;
		}

		auto* vm = GetVM();
		if (!vm || FrameworkHandle(vm) == 0) {
			return QueryStart::RejectedUnavailable;
		}
		if (!dependencies.reserveResult()) {
			return QueryStart::RejectedCapacity;
		}

		auto state = std::make_shared<RequestState>();
		state->id = a_request.requestId;
		state->sourceFormId = a_request.sourceFormId;
		state->source = a_request.source;
		state->tid = a_request.tid;
		state->sequence = a_request.sequence;
		state->epoch = a_request.epoch;
		state->started = std::chrono::steady_clock::now();
		state->interactions.clear();
		state->tags.fill(false);

		{
			std::lock_guard lock(mutex_);
			const auto [_, inserted] = requests_.emplace(state->id, state);
			if (!inserted) {
				dependencies.releaseResult();
				return QueryStart::RejectedCapacity;
			}
		}

		if (!dispatchStep(state)) {
			// The state stays pending until poll() publishes its terminal failure.
		}
		return QueryStart::Accepted;
	}

	bool PPlusAdapter::cancel(RequestId a_requestId, SessionEpoch a_epoch) noexcept
	{
		try {
			std::lock_guard lock(mutex_);
			const auto found = requests_.find(a_requestId);
			if (found == requests_.end() || found->second->epoch != a_epoch) {
				return false;
			}
			found->second->cancelRequested.store(true, std::memory_order_release);
			return true;
		} catch (...) {
			return false;
		}
	}

	void PPlusAdapter::cancelTid(Tid a_tid, SessionEpoch a_epoch) noexcept
	{
		try {
			std::lock_guard lock(mutex_);
			for (auto& [id, state] : requests_) {
				if (state->tid == a_tid && state->epoch == a_epoch) {
					state->cancelRequested.store(true, std::memory_order_release);
				}
			}
		} catch (...) {
		}
	}

	void PPlusAdapter::cancelEpoch(SessionEpoch a_epoch) noexcept
	{
		try {
			std::lock_guard lock(mutex_);
			for (auto& [id, state] : requests_) {
				if (state->epoch == a_epoch) {
					state->cancelRequested.store(true, std::memory_order_release);
				}
			}
		} catch (...) {
		}
	}

	std::size_t PPlusAdapter::pending() const
	{
		std::lock_guard lock(mutex_);
		return requests_.size();
	}

	bool PPlusAdapter::dispatchStep(const std::shared_ptr<RequestState>& a_state)
	{
		if (!a_state) {
			return false;
		}
		std::lock_guard lock(a_state->mutex);
		if (a_state->terminal.load(std::memory_order_acquire)) {
			return false;
		}
		if (a_state->cancelRequested.load(std::memory_order_acquire)) {
			fail(a_state, PPlusFailureCode::Cancelled);
			return false;
		}

		auto* vm = GetVM();
		if (!vm) {
			fail(a_state, PPlusFailureCode::VmDispatchFailed);
			return false;
		}

		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
		RE::BSScript::IFunctionArguments* args = nullptr;
		bool dispatched = false;
		++a_state->dispatchToken;
		const auto token = a_state->dispatchToken;

		switch (a_state->step) {
		case QueryStep::GetThread:
			{
				const auto handle = FrameworkHandle(vm);
				if (handle == 0) {
					fail(a_state, PPlusFailureCode::InvalidThread);
					return false;
				}
				args = RE::MakeFunctionArguments(static_cast<std::int32_t>(a_state->tid));
				callback.reset(new QueryCallback(callbackGate_, a_state, QueryStep::GetThread, token));
				a_state->waiting = true;
				dispatched = vm->DispatchMethodCall2(
					handle,
					RE::BSFixedString("SexLabFramework"),
					RE::BSFixedString("GetThread"),
					args,
					callback);
			}
			break;
		case QueryStep::Positions:
			args = RE::MakeFunctionArguments();
			callback.reset(new QueryCallback(callbackGate_, a_state, QueryStep::Positions, token));
			a_state->waiting = true;
			dispatched = vm->DispatchMethodCall2(
				a_state->thread,
				RE::BSFixedString("SexLabThread"),
				RE::BSFixedString("GetPositions"),
				args,
				callback);
			break;
		case QueryStep::Sex:
			{
				if (a_state->cursor >= a_state->positions.size()) {
					a_state->step = a_state->pairs.empty() ? QueryStep::Finalize : QueryStep::InteractionRegistered;
					return true;
				}
				auto* actor = LookupActor(a_state->positions[a_state->cursor]);
				if (!actor) {
					fail(a_state, PPlusFailureCode::ParticipantUnavailable);
					return false;
				}
				args = RE::MakeFunctionArguments(static_cast<RE::Actor*>(actor));
				callback.reset(new QueryCallback(callbackGate_, a_state, QueryStep::Sex, token));
				a_state->waiting = true;
				dispatched = vm->DispatchMethodCall2(
					a_state->thread,
					RE::BSFixedString("SexLabThread"),
					RE::BSFixedString("GetActorSex"),
					args,
					callback);
			}
			break;
		case QueryStep::InteractionRegistered:
			if (a_state->pairs.empty()) {
				a_state->step = QueryStep::Finalize;
				return true;
			}
			args = RE::MakeFunctionArguments();
			callback.reset(new QueryCallback(callbackGate_, a_state, QueryStep::InteractionRegistered, token));
			a_state->waiting = true;
			dispatched = vm->DispatchMethodCall2(
				a_state->thread,
				RE::BSFixedString("SexLabThread"),
				RE::BSFixedString("IsInteractionRegistered"),
				args,
				callback);
			break;
		case QueryStep::Interactions:
			{
				if (a_state->cursor >= a_state->pairs.size()) {
					a_state->step = QueryStep::Tags;
					a_state->cursor = 0;
					return true;
				}
				const auto targetIndex = a_state->pairs[a_state->cursor];
				auto* target = LookupActor(a_state->positions[targetIndex]);
				auto* source = LookupActor(a_state->positions[a_state->sourcePosition]);
				if (!target || !source) {
					fail(a_state, PPlusFailureCode::ParticipantUnavailable);
					return false;
				}
				args = RE::MakeFunctionArguments(static_cast<RE::Actor*>(target), static_cast<RE::Actor*>(source));
				callback.reset(new QueryCallback(callbackGate_, a_state, QueryStep::Interactions, token));
				a_state->waiting = true;
				dispatched = vm->DispatchMethodCall2(
					a_state->thread,
					RE::BSFixedString("SexLabThread"),
					RE::BSFixedString("GetInteractionTypes"),
					args,
					callback);
			}
			break;
		case QueryStep::Tags:
			{
				if (a_state->cursor >= TagCount) {
					a_state->step = QueryStep::Finalize;
					return true;
				}
				RE::BSFixedString tag{ kTagNames[a_state->cursor].data() };
				args = RE::MakeFunctionArguments(RE::BSFixedString{ tag.c_str() });
				callback.reset(new QueryCallback(callbackGate_, a_state, QueryStep::Tags, token));
				a_state->waiting = true;
				dispatched = vm->DispatchMethodCall2(
					a_state->thread,
					RE::BSFixedString("SexLabThread"),
					RE::BSFixedString("HasStageTag"),
					args,
					callback);
			}
			break;
		case QueryStep::Finalize:
			finalize(a_state);
			return false;
		}

		if (!dispatched) {
			delete args;
			a_state->waiting = false;
			fail(a_state, PPlusFailureCode::VmDispatchFailed);
			return false;
		}
		return true;
	}

	void PPlusAdapter::handleResult(
		const std::shared_ptr<RequestState>& a_state,
		int a_step,
		std::uint64_t a_token,
		const RE::BSScript::Variable& a_result)
	{
		if (!a_state) {
			return;
		}
		std::lock_guard lock(a_state->mutex);
		if (a_state->terminal.load(std::memory_order_acquire)) {
			return;
		}
		if (a_token != a_state->dispatchToken) {
			return;
		}
		if (std::chrono::steady_clock::now() - a_state->started >= kQueryTimeout) {
			fail(a_state, PPlusFailureCode::TimedOut);
			return;
		}
		a_state->waiting = false;
		if (static_cast<QueryStep>(a_step) != a_state->step) {
			return;
		}

		switch (a_state->step) {
		case QueryStep::GetThread:
			{
				if (!a_result.IsObject()) {
					fail(a_state, PPlusFailureCode::InvalidThread);
					return;
				}
				auto object = a_result.GetObject();
				const auto handle = object ? object->GetHandle() : 0;
				if (handle == 0) {
					fail(a_state, PPlusFailureCode::InvalidThread);
					return;
				}
				a_state->thread = handle;
				a_state->step = QueryStep::Positions;
			}
			break;
		case QueryStep::Positions:
			{
				if (!a_result.IsArray()) {
					fail(a_state, PPlusFailureCode::InvalidQueryData);
					return;
				}
				auto array = a_result.GetArray();
				if (!array) {
					fail(a_state, PPlusFailureCode::InvalidQueryData);
					return;
				}
				a_state->positions.clear();
				a_state->positions.reserve(array->size());
				for (std::uint32_t i = 0; i < array->size(); ++i) {
					auto* actor = (*array)[i].Unpack<RE::Actor*>();
					if (!actor) {
						fail(a_state, PPlusFailureCode::InvalidQueryData);
						return;
					}
					a_state->positions.push_back(actor->GetFormID());
				}
				const auto source = std::find(
					a_state->positions.begin(),
					a_state->positions.end(),
					a_state->sourceFormId);
				if (source == a_state->positions.end()) {
					fail(a_state, PPlusFailureCode::SourceUnavailable);
					return;
				}
				a_state->sourcePosition = static_cast<std::size_t>(std::distance(a_state->positions.begin(), source));
				a_state->pairs.clear();
				for (std::size_t i = 0; i < a_state->positions.size(); ++i) {
					if (i != a_state->sourcePosition) {
						a_state->pairs.push_back(i);
					}
				}
				a_state->sexes.assign(a_state->positions.size(), -1);
				a_state->interactions.assign(a_state->pairs.size(), {});
				a_state->cursor = 0;
				a_state->step = a_state->pairs.empty() ? QueryStep::Finalize : QueryStep::Sex;
			}
			break;
		case QueryStep::Sex:
			{
				if (!a_result.IsInt()) {
					fail(a_state, PPlusFailureCode::InvalidQueryData);
					return;
				}
				a_state->sexes[a_state->cursor] = a_result.GetSInt();
				++a_state->cursor;
				if (a_state->cursor >= a_state->positions.size()) {
					a_state->step = a_state->pairs.empty() ? QueryStep::Finalize : QueryStep::InteractionRegistered;
					a_state->cursor = 0;
				}
			}
			break;
		case QueryStep::InteractionRegistered:
			if (!a_result.IsBool()) {
				fail(a_state, PPlusFailureCode::InvalidQueryData);
				return;
			}
			a_state->interactionRegistered = a_result.GetBool();
			a_state->cursor = 0;
			a_state->step = (a_state->interactionRegistered && !a_state->pairs.empty()) ?
				QueryStep::Interactions :
				QueryStep::Tags;
			if (a_state->pairs.empty()) {
				a_state->step = QueryStep::Finalize;
			}
			break;
		case QueryStep::Interactions:
			{
				if (!a_result.IsArray()) {
					// P+ may return None when the directed pair has no interaction data.
					a_state->interactions[a_state->cursor].clear();
				} else {
					auto array = a_result.GetArray();
					auto& types = a_state->interactions[a_state->cursor];
					types.clear();
					if (array) {
						for (std::uint32_t i = 0; i < array->size(); ++i) {
							const auto& element = (*array)[i];
							if (!element.IsInt()) {
								fail(a_state, PPlusFailureCode::InvalidQueryData);
								return;
							}
							types.push_back(element.GetSInt());
						}
					}
				}
				++a_state->cursor;
				if (a_state->cursor >= a_state->pairs.size()) {
					a_state->step = QueryStep::Tags;
					a_state->cursor = 0;
				}
			}
			break;
		case QueryStep::Tags:
			if (!a_result.IsBool()) {
				fail(a_state, PPlusFailureCode::InvalidQueryData);
				return;
			}
			a_state->tags[a_state->cursor] = a_result.GetBool();
			++a_state->cursor;
			if (a_state->cursor >= TagCount) {
				a_state->step = QueryStep::Finalize;
			}
			break;
		case QueryStep::Finalize:
			finalize(a_state);
			break;
		}
	}

	void PPlusAdapter::fail(const std::shared_ptr<RequestState>& a_state, PPlusFailureCode a_code)
	{
		if (!a_state) {
			return;
		}
		// Callers may already hold the recursive state lock. Publish the
		// failure fields before releasing the terminal flag so a concurrent
		// complete() can never observe a terminal state with a success body.
		std::lock_guard lock(a_state->mutex);
		if (a_state->terminal.load(std::memory_order_acquire)) {
			return;
		}
		a_state->failed = true;
		a_state->failure = a_code;
		a_state->waiting = false;
		a_state->terminal.store(true, std::memory_order_release);
	}

	void PPlusAdapter::finalize(const std::shared_ptr<RequestState>& a_state)
	{
		if (!a_state) {
			return;
		}
		std::lock_guard lock(a_state->mutex);
		if (a_state->terminal.load(std::memory_order_acquire)) {
			return;
		}

		if (std::chrono::steady_clock::now() - a_state->started >= kQueryTimeout) {
			fail(a_state, PPlusFailureCode::TimedOut);
			return;
		}

		auto& state = *a_state;
		auto* source = LookupActor(state.sourceFormId);
		if (!source) {
			fail(a_state, PPlusFailureCode::SourceUnavailable);
			return;
		}
		if (state.positions.empty() || state.sourcePosition >= state.positions.size()) {
			fail(a_state, PPlusFailureCode::SourceUnavailable);
			return;
		}

		const auto& settings = dependencies_.settings();
		const auto sourceSex = state.sexes.empty() ? -1 : state.sexes[state.sourcePosition];
		const auto sourceClass = (sourceSex >= 0 && sourceSex <= 4) ? sourceSex : 5;
		if (sourceClass >= 0 && sourceClass < static_cast<int>(settings.enabledSourceClasses.size()) &&
			!settings.enabledSourceClasses[static_cast<std::size_t>(sourceClass)]) {
			state.resolved = ResolvedOrgasm{ state.source, state.tid, state.sequence, state.epoch, {} };
			state.terminal.store(true, std::memory_order_release);
			return;
		}
		if ((sourceSex == 1 || sourceSex == 4) && !settings.allowFemaleFluid) {
			state.resolved = ResolvedOrgasm{ state.source, state.tid, state.sequence, state.epoch, {} };
			state.terminal.store(true, std::memory_order_release);
			return;
		}
		if (sourceClass < 0 || sourceClass > 4) {
			state.resolved = ResolvedOrgasm{ state.source, state.tid, state.sequence, state.epoch, {} };
			state.terminal.store(true, std::memory_order_release);
			return;
		}

		bool sceneHasPlayerOrTeammate = false;
		for (const auto formId : state.positions) {
			if (auto* actor = LookupActor(formId)) {
				if (actor == RE::PlayerCharacter::GetSingleton() || IsFollower(actor)) {
					sceneHasPlayerOrTeammate = true;
					break;
				}
			}
		}
		if (!sceneHasPlayerOrTeammate) {
			state.resolved = ResolvedOrgasm{ state.source, state.tid, state.sequence, state.epoch, {} };
			state.terminal.store(true, std::memory_order_release);
			return;
		}

		const bool mega = IsMegaRace(settings, source);
		const auto variant = mega ? TextureVariant::Mega : TextureVariant::Normal;
		const bool maleSource = sourceClass == 0 || sourceClass == 2 || sourceClass == 3;

		std::vector<InteractionTarget> targets;
		for (std::size_t pair = 0; pair < state.pairs.size(); ++pair) {
			const auto positionIndex = state.pairs[pair];
			auto* targetActor = LookupActor(state.positions[positionIndex]);
			if (!targetActor) {
				fail(a_state, PPlusFailureCode::ParticipantUnavailable);
				return;
			}
			const auto targetSex = state.sexes.empty() ? -1 : state.sexes[positionIndex];
			const bool femaleTarget = targetSex == 1 || targetSex == 2 || targetSex == 4;
			const bool maleTarget = targetSex == 0 || targetSex == 3;
			if (!femaleTarget && !maleTarget) {
				continue;
			}
			if (!maleSource && !femaleTarget) {
				continue;
			}
			if (femaleTarget && settings.femaleTargetsRequirePlayerOrFollower &&
				targetActor != RE::PlayerCharacter::GetSingleton() && !IsFollower(targetActor)) {
				continue;
			}

			const PPlusInteractionFlags stageTags{
				.vaginal = state.tags[TagVaginal] || state.tags[TagPussy],
				.anal = state.tags[TagAnal],
				.oral = state.tags[TagOral] || state.tags[TagBlowjob],
				.trib = state.tags[TagTribadism] || state.tags[TagRubbing],
				.hand = state.tags[TagHandjob],
				.foot = state.tags[TagFootjob],
				.breast = state.tags[TagBoobjob] || state.tags[TagBreast],
			};
			const bool hasDirected = state.interactionRegistered &&
				pair < state.interactions.size() && !state.interactions[pair].empty();
			const auto [vaginal, anal, oral, trib, hand, foot, breast] = ResolvePPlusInteractions(
				stageTags,
				hasDirected ? std::span<const std::int32_t>(state.interactions[pair]) : std::span<const std::int32_t>{},
				hasDirected,
				state.positions.size() == 2);

			std::array<bool, kRegionCount> regionFlags{};
			if (maleSource) {
				if (femaleTarget) {
					regionFlags[RegionIndex(Region::BodyVaginal)] = vaginal;
					regionFlags[RegionIndex(Region::BodyAnal)] = anal;
					regionFlags[RegionIndex(Region::BodyBreast)] = breast;
				} else {
					regionFlags[RegionIndex(Region::BodyAnal)] = anal;
				}
				regionFlags[RegionIndex(Region::Face)] = oral;
				regionFlags[RegionIndex(Region::Hands)] = hand;
				regionFlags[RegionIndex(Region::Feet)] = foot;
				regionFlags[RegionIndex(Region::BodyLegs)] = foot;
			} else {
				regionFlags[RegionIndex(Region::Face)] = oral;
				if (trib || vaginal) {
					regionFlags[RegionIndex(Region::BodyVaginal)] = true;
					regionFlags[RegionIndex(Region::BodyBreast)] = true;
				}
				regionFlags[RegionIndex(Region::Hands)] = hand;
				regionFlags[RegionIndex(Region::Feet)] = foot;
				regionFlags[RegionIndex(Region::BodyLegs)] = foot;
			}

			std::vector<Region> regions;
			for (const auto region : kRegions) {
				if (regionFlags[RegionIndex(region)]) {
					regions.push_back(region);
				}
			}
			if (regions.empty()) {
				continue;
			}
			const auto actorId = dependencies_.registry->bind(targetActor->GetFormID());
			if (!actorId) {
				fail(a_state, PPlusFailureCode::ParticipantUnavailable);
				return;
			}
			targets.push_back(InteractionTarget{ *actorId, std::move(regions), variant });
		}

		state.resolved = ResolvedOrgasm{ state.source, state.tid, state.sequence, state.epoch, std::move(targets) };
		state.terminal.store(true, std::memory_order_release);
	}

	bool PPlusAdapter::complete(const std::shared_ptr<RequestState>& a_state)
	{
		if (!a_state) {
			return false;
		}

		bool expected = false;
		if (!a_state->posting.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
			return false;
		}
		if (a_state->posted.load(std::memory_order_acquire)) {
			a_state->posting.store(false, std::memory_order_release);
			return false;
		}

		PPlusCompleted command{};
		{
			std::lock_guard stateLock(a_state->mutex);
			if (!a_state->terminal.load(std::memory_order_acquire)) {
				a_state->posting.store(false, std::memory_order_release);
				return false;
			}
			command.requestId = a_state->id;
			command.epoch = a_state->epoch;
			if (a_state->failed) {
				PPlusFailure failure{};
				failure.requestId = a_state->id;
				failure.epoch = a_state->epoch;
				failure.tid = a_state->tid;
				failure.sequence = a_state->sequence;
				failure.code = a_state->failure;
				command.outcome = failure;
			} else {
				command.outcome = a_state->resolved;
			}
		}

		PPlusDependencies dependencies;
		{
			std::lock_guard lock(mutex_);
			dependencies = dependencies_;
		}
		bool posted = false;
		try {
			posted = dependencies.postResult && dependencies.postResult(std::move(command));
		} catch (const std::exception& e) {
			logs::error("P+ completion post exception: {}", e.what());
		} catch (...) {
			logs::error("P+ completion post unknown exception");
		}
		if (posted) {
			a_state->posted.store(true, std::memory_order_release);
			a_state->posting.store(false, std::memory_order_release);
			std::lock_guard lock(mutex_);
			requests_.erase(a_state->id);
			return true;
		}

		const bool staleEpoch = !dependencies.epoch || dependencies.epoch() != a_state->epoch;
		if (!dependencies.postResult || staleEpoch) {
			// Terminal results from an older session must not keep a reserved
			// ingress slot or a request state alive across a load.
			{
				std::lock_guard lock(mutex_);
				requests_.erase(a_state->id);
			}
			if (dependencies.releaseResult) {
				dependencies.releaseResult();
			}
			a_state->posting.store(false, std::memory_order_release);
			return false;
		}

		// The ingress queue may be temporarily saturated. Keep the terminal
		// result and its reservation so a later pump can deliver it.
		a_state->posting.store(false, std::memory_order_release);
		return false;
	}

	std::uint32_t PPlusAdapter::poll(SteadyTime a_now, std::size_t a_maxCallbacks)
	{
		std::vector<std::shared_ptr<RequestState>> pendingStates;
		{
			std::lock_guard lock(mutex_);
			pendingStates.reserve(requests_.size());
			for (const auto& [id, state] : requests_) {
				pendingStates.push_back(state);
			}
		}

		std::uint32_t callbacks = 0;
		for (const auto& state : pendingStates) {
			if (callbacks >= a_maxCallbacks || !state) {
				break;
			}
			if (state->posted) {
				continue;
			}
			{
				std::lock_guard stateLock(state->mutex);
				if (state->cancelRequested.load(std::memory_order_acquire)) {
					fail(state, PPlusFailureCode::Cancelled);
				} else if (!state->terminal.load(std::memory_order_acquire) &&
					a_now - state->started >= kQueryTimeout) {
					fail(state, PPlusFailureCode::TimedOut);
				}

				if (!state->terminal.load(std::memory_order_acquire)) {
					for (int step = 0; step < 64 && !state->terminal.load(std::memory_order_acquire); ++step) {
						// A dispatch is outstanding. The VM callback will clear
						// waiting; re-dispatching the same step would corrupt the
						// per-cursor result arrays.
						if (state->waiting) {
							break;
						}
						if (!dispatchStep(state)) {
							break;
						}
						++callbacks;
						if (callbacks >= a_maxCallbacks) {
							break;
						}
					}
				}
			}

			if (state->terminal.load(std::memory_order_acquire)) {
				complete(state);
				++callbacks;
			}
		}
		return callbacks;
	}
}
