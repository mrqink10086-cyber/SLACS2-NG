#include "config/Settings.h"
#include "core/Core.h"
#include "runtime/Scheduler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	int g_failures = 0;

	void Expect(bool a_condition, const char* a_expression, int a_line)
	{
		if (!a_condition) {
			std::cerr << "FAIL line " << a_line << ": " << a_expression << '\n';
			++g_failures;
		}
	}

	slacs::ChangeSet Add(
		slacs::CoreState& a_state,
		slacs::ActorId a_target,
		std::vector<slacs::Region> a_regions,
		const slacs::Settings& a_settings,
		slacs::GameTime a_now)
	{
		slacs::AddLayers op{};
		op.source = 100;
		op.tid = 7;
		op.sequence = 1;
		op.selectionSeed = 0x5A17;
		op.targets.push_back(slacs::InteractionTarget{ a_target, std::move(a_regions), slacs::TextureVariant::Normal });
		return slacs::Core::apply(a_state, slacs::CoreOp{ std::move(op) }, a_settings, a_now);
	}

	slacs::ChangeSet Advance(
		slacs::CoreState& a_state,
		slacs::ActorId a_actor,
		const slacs::Settings& a_settings,
		slacs::GameTime a_now,
		std::optional<slacs::EnvironmentSample> a_sample = std::nullopt)
	{
		slacs::Advance op{};
		op.actor = a_actor;
		op.now = a_now;
		op.environment = std::move(a_sample);
		return slacs::Core::apply(a_state, slacs::CoreOp{ std::move(op) }, a_settings, a_now);
	}

	const slacs::DeadlineIntent* FindDeadline(
		const slacs::ChangeSet& a_change,
		slacs::DeadlineKind a_kind,
		std::optional<slacs::Region> a_region = std::nullopt)
	{
		const auto found = std::find_if(
			a_change.deadlines.begin(),
			a_change.deadlines.end(),
			[a_kind, a_region](const slacs::DeadlineIntent& a_intent) {
				return a_intent.kind == a_kind && a_intent.region == a_region;
			});
		return found == a_change.deadlines.end() ? nullptr : &*found;
	}

	std::vector<std::uint64_t> LayerIds(const slacs::CoreState& a_state, slacs::ActorId a_actor, slacs::Region a_region)
	{
		std::vector<std::uint64_t> ids;
		const auto found = a_state.actors.find(a_actor);
		if (found == a_state.actors.end()) {
			return ids;
		}
		for (const auto& layer : found->second.regions[slacs::RegionIndex(a_region)].layers) {
			ids.push_back(layer.id);
		}
		return ids;
	}

	slacs::Deadline MakeDeadline(
		slacs::DeadlineId a_id,
		slacs::DeadlineKind a_kind,
		slacs::ClockDomain a_clock,
		double a_due,
		std::optional<slacs::Region> a_region = std::nullopt,
		slacs::SessionEpoch a_epoch = 1)
	{
		slacs::Deadline deadline{};
		deadline.id = a_id;
		deadline.actor = 42;
		deadline.kind = a_kind;
		deadline.clock = a_clock;
		deadline.due = a_due;
		deadline.epoch = a_epoch;
		deadline.region = a_region;
		deadline.ownerRevision = 3;
		return deadline;
	}

	void CheckScheduler()
	{
		using namespace slacs;

		Scheduler scheduler;
		Expect(scheduler.upsert(MakeDeadline(1, DeadlineKind::NaturalDecay, ClockDomain::CalendarGameHours, 10.0, Region::BodyVaginal)) ==
			DeadlineUpdateStatus::Updated, "scheduler accepts natural", __LINE__);
		Expect(scheduler.takeDue(GameTime{ 9.0, 0.0 }, 8).empty(), "not due before deadline", __LINE__);
		Expect(scheduler.upsert(MakeDeadline(1, DeadlineKind::NaturalDecay, ClockDomain::CalendarGameHours, 5.0, Region::BodyVaginal)) ==
			DeadlineUpdateStatus::Updated, "scheduler updates stable id", __LINE__);
		auto due = scheduler.takeDue(GameTime{ 5.0, 0.0 }, 8);
		Expect(due.size() == 1 && due.front().id == 1 && due.front().ownerRevision == 3, "updated due replaces old", __LINE__);
		Expect(scheduler.takeDue(GameTime{ 20.0, 0.0 }, 8).empty(), "old heap entry invalidated", __LINE__);
		Expect(scheduler.indexSize() == scheduler.size(), "update leaves one calendar index entry", __LINE__);

		Scheduler sameDue;
		auto latestPayload = MakeDeadline(30, DeadlineKind::NaturalDecay, ClockDomain::CalendarGameHours, 12.0, Region::BodyVaginal);
		Expect(sameDue.upsert(latestPayload) == DeadlineUpdateStatus::Updated, "same-due payload inserted", __LINE__);
		for (std::uint32_t i = 0; i < 100; ++i) {
			latestPayload.actor = 100 + i;
			latestPayload.epoch = 2 + i;
			latestPayload.ownerRevision = 20 + i;
			Expect(sameDue.upsert(latestPayload) == DeadlineUpdateStatus::Updated &&
				sameDue.size() == 1 && sameDue.indexSize() == 1,
				"same-due update replaces payload without another index entry", __LINE__);
		}
		const auto latestDue = sameDue.takeDue(GameTime{ 12.0, 0.0 }, 1);
		Expect(latestDue.size() == 1 && latestDue.front().actor == latestPayload.actor &&
			latestDue.front().ownerRevision == latestPayload.ownerRevision && latestDue.front().epoch == latestPayload.epoch,
			"same-due deadline returns latest payload", __LINE__);
		latestPayload.epoch = 500;
		Expect(sameDue.upsert(latestPayload) == DeadlineUpdateStatus::Updated, "deadline reinserted for epoch cancellation", __LINE__);
		sameDue.cancelEpoch(latestPayload.epoch);
		Expect(sameDue.size() == 0 && sameDue.indexSize() == 0, "cancelEpoch releases same-due index entry", __LINE__);

		Scheduler pinned;
		Expect(pinned.upsert(MakeDeadline(40, DeadlineKind::NaturalDecay, ClockDomain::CalendarGameHours, 1.0, Region::BodyVaginal)) ==
			DeadlineUpdateStatus::Updated, "earlier live deadline inserted", __LINE__);
		for (double dueAt = 5.0; dueAt < 1005.0; dueAt += 1.0) {
			Expect(pinned.upsert(MakeDeadline(41, DeadlineKind::NaturalDecay, ClockDomain::CalendarGameHours, dueAt, Region::BodyAnal)) ==
				DeadlineUpdateStatus::Updated, "later stable id updated behind earlier deadline", __LINE__);
		}
		Expect(pinned.size() == 2 && pinned.indexSize() == 2,
			"earlier live deadline cannot pin stale update nodes", __LINE__);
		Expect(pinned.cancel(41) && pinned.indexSize() == 1,
			"cancellation immediately erases deadline index entry", __LINE__);
		const auto pinnedDue = pinned.takeDue(GameTime{ 1.0, 0.0 }, 4);
		Expect(pinnedDue.size() == 1 && pinnedDue.front().id == 40 && pinned.indexSize() == 0,
			"taking due deadline releases final index entry", __LINE__);

		Scheduler switched;
		Expect(switched.upsert(MakeDeadline(50, DeadlineKind::NaturalDecay, ClockDomain::CalendarGameHours, 1.0, Region::BodyVaginal)) ==
			DeadlineUpdateStatus::Updated, "calendar deadline inserted before clock switch", __LINE__);
		Expect(switched.upsert(MakeDeadline(50, DeadlineKind::TimedClear, ClockDomain::ActiveSeconds, 10.0)) ==
			DeadlineUpdateStatus::Updated, "stable id switches calendar to active domain", __LINE__);
		Expect(switched.indexSize() == 1 && switched.takeDue(GameTime{ 100.0, 0.0 }, 4).empty(),
			"clock switch erases old domain index", __LINE__);
		const auto activeDeadline = switched.takeDue(GameTime{ 0.0, 10.0 }, 4);
		Expect(activeDeadline.size() == 1 && activeDeadline.front().id == 50 && switched.indexSize() == 0,
			"switched deadline fires on active clock", __LINE__);

		Scheduler cancelled;
		const auto steadyDue = std::chrono::steady_clock::now() + std::chrono::hours{ 1 };
		Expect(cancelled.upsertSteady(60, steadyDue) == DeadlineUpdateStatus::Updated,
			"steady deadline inserted", __LINE__);
		for (int i = 0; i < 1000; ++i) {
			cancelled.upsertSteady(60, steadyDue + std::chrono::seconds{ i });
		}
		Expect(cancelled.indexSize() == 1 && cancelled.cancelSteady(60) && cancelled.indexSize() == 0,
			"steady updates stay bounded and cancellation releases index", __LINE__);

		Expect(scheduler.upsert(MakeDeadline(2, DeadlineKind::TimedClear, ClockDomain::ActiveSeconds, 2.0)) ==
			DeadlineUpdateStatus::Updated, "scheduler accepts timed", __LINE__);
		Expect(scheduler.cancel(2), "cancel live id", __LINE__);
		Expect(!scheduler.cancel(2), "cancel idempotent", __LINE__);
		Expect(scheduler.takeDue(GameTime{ 0.0, 5.0 }, 8).empty(), "cancelled id not returned", __LINE__);

		Expect(scheduler.upsert(MakeDeadline(3, DeadlineKind::TimedClear, ClockDomain::ActiveSeconds, 1.0, std::nullopt, 7)) ==
			DeadlineUpdateStatus::Updated, "epoch 7 deadline", __LINE__);
		Expect(scheduler.upsert(MakeDeadline(4, DeadlineKind::TimedClear, ClockDomain::ActiveSeconds, 1.0, std::nullopt, 8)) ==
			DeadlineUpdateStatus::Updated, "epoch 8 deadline", __LINE__);
		scheduler.cancelEpoch(7);
		const auto epochDue = scheduler.takeDue(GameTime{ 0.0, 2.0 }, 8);
		Expect(epochDue.size() == 1 && epochDue.front().id == 4, "cancelEpoch only cancels target epoch", __LINE__);

		for (DeadlineId id = 10; id < 15; ++id) {
			Expect(scheduler.upsert(MakeDeadline(id, DeadlineKind::TimedClear, ClockDomain::ActiveSeconds, 5.0)) ==
				DeadlineUpdateStatus::Updated, "budget deadline inserted", __LINE__);
		}
		const auto first = scheduler.takeDue(GameTime{ 0.0, 6.0 }, 2);
		const auto second = scheduler.takeDue(GameTime{ 0.0, 6.0 }, 2);
		const auto third = scheduler.takeDue(GameTime{ 0.0, 6.0 }, 2);
		Expect(first.size() == 2 && second.size() == 2 && third.size() == 1, "takeDue respects item budget", __LINE__);
		Expect(scheduler.size() == 0, "scheduler empty after budget drain", __LINE__);

		Expect(scheduler.upsert(MakeDeadline(20, DeadlineKind::TimedClear, ClockDomain::CalendarGameHours, 1.0)) ==
			DeadlineUpdateStatus::RejectedInvalid, "kind/clock mismatch rejected", __LINE__);
		Expect(scheduler.upsert(MakeDeadline(21, DeadlineKind::NaturalDecay, ClockDomain::CalendarGameHours, 1.0)) ==
			DeadlineUpdateStatus::RejectedInvalid, "natural without region rejected", __LINE__);
		Expect(scheduler.upsert(MakeDeadline(22, DeadlineKind::TimedClear, ClockDomain::ActiveSeconds, std::nan(""))) ==
			DeadlineUpdateStatus::RejectedInvalid, "non-finite due rejected", __LINE__);

		const auto steadyStart = std::chrono::steady_clock::now();
		Expect(scheduler.upsertSteady(1, steadyStart + std::chrono::milliseconds{ 10 }) ==
			DeadlineUpdateStatus::Updated, "steady inserted", __LINE__);
		Expect(scheduler.takeDueSteady(steadyStart, 4).empty(), "steady not due", __LINE__);
		Expect(scheduler.upsertSteady(1, steadyStart + std::chrono::milliseconds{ 5 }) ==
			DeadlineUpdateStatus::Updated, "steady updated", __LINE__);
		const auto steady = scheduler.takeDueSteady(steadyStart + std::chrono::milliseconds{ 5 }, 4);
		Expect(steady.size() == 1 && steady.front() == 1, "steady stable id fires once", __LINE__);
		Expect(scheduler.takeDueSteady(steadyStart + std::chrono::seconds{ 1 }, 4).empty(), "steady old entry dropped", __LINE__);
	}

	void CheckDecay()
	{
		using namespace slacs;
		const Settings settings{};

		CoreState state{};
		const auto added = Add(state, 200, { Region::BodyVaginal }, settings, GameTime{});
		Expect(LayerIds(state, 200, Region::BodyVaginal).size() == 1, "initial layer", __LINE__);
		Expect(state.actors.at(200).timedClearAt.has_value(), "add arms timed clear", __LINE__);
		const auto natural = FindDeadline(added, DeadlineKind::NaturalDecay, Region::BodyVaginal);
		Expect(natural && natural->action == DeadlineIntent::Action::Upsert, "add emits natural deadline", __LINE__);
		Expect(natural && std::abs(*natural->due - 1.0) < 1e-9, "natural due one hour", __LINE__);
		const auto timed = FindDeadline(added, DeadlineKind::TimedClear);
		Expect(timed && timed->action == DeadlineIntent::Action::Upsert &&
			std::abs(*timed->due - settings.timedClearSeconds) < 1e-9, "add emits timed deadline", __LINE__);

		for (int i = 0; i < 4; ++i) {
			Add(state, 200, { Region::BodyVaginal }, settings, GameTime{});
		}
		Expect(LayerIds(state, 200, Region::BodyVaginal).size() == 5, "five layers before decay", __LINE__);

		const auto decay = Advance(state, 200, settings, GameTime{ 1.0, 0.0 });
		Expect(LayerIds(state, 200, Region::BodyVaginal) == std::vector<std::uint64_t>({ 1, 2, 3, 4 }), "LIFO removes newest", __LINE__);
		const auto nextNatural = FindDeadline(decay, DeadlineKind::NaturalDecay, Region::BodyVaginal);
		Expect(nextNatural && std::abs(*nextNatural->due - 2.0) < 1e-9, "natural reschedules one hour", __LINE__);

		CoreState frequent{};
		CoreState jump{};
		for (int i = 0; i < 5; ++i) {
			Add(frequent, 200, { Region::BodyVaginal }, settings, GameTime{});
			Add(jump, 200, { Region::BodyVaginal }, settings, GameTime{});
		}
		for (int hour = 1; hour <= 4; ++hour) {
			Advance(frequent, 200, settings, GameTime{ static_cast<double>(hour), 0.0 });
		}
		const auto jumpChange = Advance(jump, 200, settings, GameTime{ 4.0, 0.0 });
		Expect(LayerIds(frequent, 200, Region::BodyVaginal) == LayerIds(jump, 200, Region::BodyVaginal),
			"frequency-independent removal", __LINE__);
		const auto frequentRegion = frequent.actors.at(200).regions[RegionIndex(Region::BodyVaginal)];
		const auto jumpRegion = jump.actors.at(200).regions[RegionIndex(Region::BodyVaginal)];
		Expect(std::abs(frequentRegion.decayRemainderGameHours - jumpRegion.decayRemainderGameHours) < 1e-9,
			"frequency-independent remainder", __LINE__);
		const auto jumpNatural = FindDeadline(jumpChange, DeadlineKind::NaturalDecay, Region::BodyVaginal);
		Expect(jumpNatural && std::abs(*jumpNatural->due - 5.0) < 1e-9, "jump reschedules remaining layer", __LINE__);

		const auto longJump = Advance(jump, 200, settings, GameTime{ 20.0, 0.0 });
		Expect(LayerIds(jump, 200, Region::BodyVaginal).empty(), "long jump clears layers", __LINE__);
		Expect(std::abs(jump.actors.at(200).regions[RegionIndex(Region::BodyVaginal)].decayRemainderGameHours) < 1e-9,
			"empty region remainder zero", __LINE__);
		const auto clearedNatural = FindDeadline(longJump, DeadlineKind::NaturalDecay, Region::BodyVaginal);
		Expect(clearedNatural && clearedNatural->action == DeadlineIntent::Action::Cancel, "empty region cancels decay", __LINE__);

		const auto emptyJump = Advance(jump, 200, settings, GameTime{ 50.0, 0.0 });
		Expect(LayerIds(jump, 200, Region::BodyVaginal).empty(), "empty region stays empty", __LINE__);
		Expect(FindDeadline(emptyJump, DeadlineKind::NaturalDecay, Region::BodyVaginal)->action == DeadlineIntent::Action::Cancel,
			"empty region accrues no future deadline", __LINE__);
	}

	void CheckRainAndSwim()
	{
		using namespace slacs;
		const Settings settings{};

		CoreState dry{};
		CoreState wet{};
		for (int i = 0; i < 5; ++i) {
			Add(dry, 200, { Region::BodyVaginal }, settings, GameTime{});
			Add(wet, 200, { Region::BodyVaginal }, settings, GameTime{});
		}

		EnvironmentSample rain{};
		rain.actor = 200;
		rain.interior = false;
		rain.raining = true;
		rain.observedRainHours = 0.4;
		rain.at = GameTime{ 0.6, 0.0 };
		Advance(wet, 200, settings, rain.at, rain);
		Advance(dry, 200, settings, GameTime{ 0.6, 0.0 });
		Expect(LayerIds(wet, 200, Region::BodyVaginal).size() == 4, "observed rain adds decay progress", __LINE__);
		Expect(LayerIds(dry, 200, Region::BodyVaginal).size() == 5, "no rain keeps layers", __LINE__);

		EnvironmentSample interior = rain;
		interior.interior = true;
		CoreState indoors{};
		for (int i = 0; i < 5; ++i) {
			Add(indoors, 200, { Region::BodyVaginal }, settings, GameTime{});
		}
		Advance(indoors, 200, settings, GameTime{ 0.6, 0.0 }, interior);
		Expect(LayerIds(indoors, 200, Region::BodyVaginal).size() == 5, "interior rain ignored", __LINE__);

		CoreState swimming{};
		for (int i = 0; i < 5; ++i) {
			Add(swimming, 200, { Region::BodyVaginal, Region::BodyAnal }, settings, GameTime{});
		}
		EnvironmentSample swim{};
		swim.actor = 200;
		swim.swimming = true;
		swim.at = GameTime{ 0.1, 0.0 };
		Advance(swimming, 200, settings, swim.at, swim);
		Expect(LayerIds(swimming, 200, Region::BodyVaginal).empty(), "swimming clears vaginal", __LINE__);
		Expect(LayerIds(swimming, 200, Region::BodyAnal).empty(), "swimming clears all regions", __LINE__);
	}

	void CheckTimedClear()
	{
		using namespace slacs;
		const Settings settings{};

		CoreState state{};
		Add(state, 200, { Region::BodyVaginal, Region::BodyAnal }, settings, GameTime{});
		Expect(std::abs(*state.actors.at(200).timedClearAt - 120.0) < 1e-9, "timed clear armed at 120", __LINE__);

		Advance(state, 200, settings, GameTime{ 0.0, 119.0 });
		Expect(!LayerIds(state, 200, Region::BodyVaginal).empty(), "timed clear survives before deadline", __LINE__);

		const auto cleared = Advance(state, 200, settings, GameTime{ 0.0, 120.0 });
		Expect(LayerIds(state, 200, Region::BodyVaginal).empty(), "timed clear fires at 120 active seconds", __LINE__);
		Expect(LayerIds(state, 200, Region::BodyAnal).empty(), "timed clear clears all regions", __LINE__);
		Expect(!state.actors.at(200).timedClearAt.has_value(), "timed clear disarms", __LINE__);
		const auto timed = FindDeadline(cleared, DeadlineKind::TimedClear);
		Expect(timed && timed->action == DeadlineIntent::Action::Cancel, "timed clear emits cancel", __LINE__);

		CoreState reset{};
		Add(reset, 200, { Region::BodyVaginal }, settings, GameTime{ 0.0, 10.0 });
		Add(reset, 200, { Region::BodyVaginal }, settings, GameTime{ 0.0, 50.0 });
		Expect(std::abs(*reset.actors.at(200).timedClearAt - 170.0) < 1e-9, "successful add resets timed clear", __LINE__);
		const auto resetChange = Advance(reset, 200, settings, GameTime{ 0.0, 60.0 });
		const auto resetTimed = FindDeadline(resetChange, DeadlineKind::TimedClear);
		Expect(resetTimed && std::abs(*resetTimed->due - 170.0) < 1e-9, "other advance preserves timed due", __LINE__);

		CoreState clearedByMenu{};
		Add(clearedByMenu, 200, { Region::BodyVaginal }, settings, GameTime{});
		const auto clearChange = Core::apply(
			clearedByMenu,
			CoreOp{ ClearRegions{ 200, { Region::BodyVaginal } } },
			settings,
			GameTime{ 0.0, 30.0 });
		Expect(clearedByMenu.actors.at(200).timedClearAt.has_value(), "manual clear preserves timed clear", __LINE__);
		const auto clearTimed = FindDeadline(clearChange, DeadlineKind::TimedClear);
		Expect(clearTimed && std::abs(*clearTimed->due - 120.0) < 1e-9, "manual clear keeps original due", __LINE__);

		Settings disabled = settings;
		disabled.timedClear = TimedClearMode::Disabled;
		CoreState noTimer{};
		const auto noTimerChange = Add(noTimer, 200, { Region::BodyVaginal }, disabled, GameTime{});
		Expect(!noTimer.actors.at(200).timedClearAt.has_value(), "disabled timed clear not armed", __LINE__);
		const auto disabledTimed = FindDeadline(noTimerChange, DeadlineKind::TimedClear);
		Expect(disabledTimed && disabledTimed->action == DeadlineIntent::Action::Cancel, "disabled timed clear cancels", __LINE__);
	}

	void CheckSettleBeforeAddAndConfig()
	{
		using namespace slacs;
		const Settings settings{};

		CoreState state{};
		for (int i = 0; i < 5; ++i) {
			Add(state, 200, { Region::BodyVaginal }, settings, GameTime{});
		}
		const auto added = Add(state, 200, { Region::BodyVaginal, Region::BodyAnal }, settings, GameTime{ 1.0, 60.0 });
		Expect(LayerIds(state, 200, Region::BodyVaginal) == std::vector<std::uint64_t>({ 1, 2, 3, 4, 6 }),
			"add settles old state before capacity check", __LINE__);
		Expect(LayerIds(state, 200, Region::BodyAnal) == std::vector<std::uint64_t>({ 7 }), "second region gets new layer", __LINE__);
		Expect(std::abs(*state.actors.at(200).timedClearAt - 180.0) < 1e-9, "add re-arms timed clear at now+120", __LINE__);
		const auto natural = FindDeadline(added, DeadlineKind::NaturalDecay, Region::BodyVaginal);
		Expect(natural && natural->action == DeadlineIntent::Action::Upsert, "add emits settled natural deadline", __LINE__);

		CoreState config{};
		for (int i = 0; i < 5; ++i) {
			Add(config, 200, { Region::BodyVaginal }, settings, GameTime{});
		}
		Advance(config, 200, settings, GameTime{ 2.0, 0.0 });
		Settings slower = settings;
		slower.decayIntervalGameHours = 2.0;
		const auto configChange = Core::apply(config, CoreOp{ SettingsChanged{} }, slower, GameTime{ 2.0, 0.0 });
		Expect(LayerIds(config, 200, Region::BodyVaginal) == std::vector<std::uint64_t>({ 1, 2, 3 }),
			"old parameters settled before config change", __LINE__);
		const auto slowerNatural = FindDeadline(configChange, DeadlineKind::NaturalDecay, Region::BodyVaginal);
		Expect(slowerNatural && std::abs(*slowerNatural->due - 4.0) < 1e-9, "new interval schedules next decay", __LINE__);

		Settings noDecay = settings;
		noDecay.naturalDecay = false;
		CoreState frozen{};
		Add(frozen, 200, { Region::BodyVaginal }, noDecay, GameTime{});
		const auto frozenChange = Advance(frozen, 200, noDecay, GameTime{ 8.0, 0.0 });
		Expect(LayerIds(frozen, 200, Region::BodyVaginal).size() == 1, "disabled natural decay keeps layers", __LINE__);
		const auto frozenNatural = FindDeadline(frozenChange, DeadlineKind::NaturalDecay, Region::BodyVaginal);
		Expect(frozenNatural && frozenNatural->action == DeadlineIntent::Action::Cancel, "disabled natural decay cancels", __LINE__);
	}

	void CheckAuditRegressions()
	{
		using namespace slacs;
		const Settings settings{};

		CoreState backwards{};
		for (int i = 0; i < 5; ++i) {
			Add(backwards, 200, { Region::BodyVaginal }, settings, GameTime{});
		}
		Advance(backwards, 200, settings, GameTime{ 2.0, 0.0 });
		Advance(backwards, 200, settings, GameTime{ 1.0, 0.0 });
		Expect(LayerIds(backwards, 200, Region::BodyVaginal).size() == 3, "backwards clock does not double settle", __LINE__);
		Expect(std::abs(backwards.actors.at(200).lastSettledGameHours - 2.0) < 1e-9,
			"backwards clock keeps monotonic baseline", __LINE__);
		Advance(backwards, 200, settings, GameTime{ 3.0, 0.0 });
		Expect(LayerIds(backwards, 200, Region::BodyVaginal).size() == 2, "forward clock resumes from baseline", __LINE__);

		Settings tiny = settings;
		tiny.decayIntervalGameHours = 1.0e-12;
		CoreState huge{};
		for (int i = 0; i < 5; ++i) {
			Add(huge, 200, { Region::BodyVaginal }, tiny, GameTime{});
		}
		Advance(huge, 200, tiny, GameTime{ 1.0e9, 0.0 });
		Expect(LayerIds(huge, 200, Region::BodyVaginal).empty(), "huge decay ratio removes all layers safely", __LINE__);

		Settings disabled = settings;
		disabled.timedClear = TimedClearMode::Disabled;
		CoreState enable{};
		Add(enable, 200, { Region::BodyVaginal }, disabled, GameTime{ 0.0, 10.0 });
		const auto enableChange = Core::apply(enable, CoreOp{ SettingsChanged{} }, settings, GameTime{ 0.0, 10.0 });
		Expect(enable.actors.at(200).timedClearAt.has_value(), "enabling timed clear arms existing layers", __LINE__);
		Expect(std::abs(*enable.actors.at(200).timedClearAt - 130.0) < 1e-9, "enabled timer uses configured seconds", __LINE__);
		const auto enabledTimed = FindDeadline(enableChange, DeadlineKind::TimedClear);
		Expect(enabledTimed && enabledTimed->action == DeadlineIntent::Action::Upsert &&
			std::abs(*enabledTimed->due - 130.0) < 1e-9, "enabled timer emits deadline", __LINE__);

		ActorState dirty{};
		dirty.actor = 300;
		auto& region = dirty.regions[RegionIndex(Region::BodyVaginal)];
		region.layers.push_back(LayerRecord{ 1, 1, TextureVariant::Normal });
		region.layers.push_back(LayerRecord{ 0, 1, TextureVariant::Normal });
		region.layers.push_back(LayerRecord{ 1, 2, TextureVariant::Normal });
		region.layers.push_back(LayerRecord{ 4, 99, TextureVariant::Normal });
		region.layers.push_back(LayerRecord{ 5, 1, static_cast<TextureVariant>(9) });
		region.revision = 3;
		std::vector<ActorState> actors{ dirty };
		CoreState sanitized{};
		Core::replaceState(sanitized, std::move(actors), settings, GameTime{});
		Expect(LayerIds(sanitized, 300, Region::BodyVaginal) == std::vector<std::uint64_t>({ 1 }),
			"sanitize removes invalid and duplicate layers", __LINE__);
		Expect(sanitized.actors.at(300).regions[RegionIndex(Region::BodyVaginal)].revision > 3,
			"sanitize bumps revision after repair", __LINE__);

		Scheduler scheduler;
		Expect(scheduler.upsert(MakeDeadline(30, DeadlineKind::TimedClear, ClockDomain::ActiveSeconds, 5.0)) ==
			DeadlineUpdateStatus::Updated, "revision deadline inserted", __LINE__);
		Expect(scheduler.updateRevision(30, 9, 1), "revision update accepted", __LINE__);
		Expect(!scheduler.updateRevision(30, 9, 2), "revision update rejects stale epoch", __LINE__);
		const auto revised = scheduler.takeDue(GameTime{ 0.0, 6.0 }, 4);
		Expect(revised.size() == 1 && revised.front().ownerRevision == 9, "revision update keeps due", __LINE__);
	}
}

int main()
{
	CheckScheduler();
	CheckDecay();
	CheckRainAndSwim();
	CheckTimedClear();
	CheckSettleBeforeAddAndConfig();

	if (g_failures != 0) {
		std::cerr << g_failures << " S5/S6 check(s) failed\n";
		return 1;
	}
	std::cout << "SLACS2 NG S5/S6 checks passed\n";
	return 0;
}
