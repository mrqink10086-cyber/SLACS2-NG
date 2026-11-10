#include "runtime/Scheduler.h"

#include <algorithm>
#include <cmath>

namespace slacs
{
	namespace
	{
		bool ValidRegionIndex(const std::optional<Region>& a_region)
		{
			return !a_region || RegionIndex(*a_region) < kRegionCount;
		}
	}

	bool Scheduler::Valid(const Deadline& a_deadline)
	{
		if (a_deadline.id == 0 || a_deadline.actor == kInvalidActorId || a_deadline.epoch == 0) {
			return false;
		}
		if (!std::isfinite(a_deadline.due) || !ValidRegionIndex(a_deadline.region)) {
			return false;
		}

		switch (a_deadline.kind) {
		case DeadlineKind::NaturalDecay:
			return a_deadline.clock == ClockDomain::CalendarGameHours && a_deadline.region.has_value();
		case DeadlineKind::TimedClear:
		case DeadlineKind::SwimSample:
			return a_deadline.clock == ClockDomain::ActiveSeconds && !a_deadline.region.has_value();
		case DeadlineKind::OverlayRetry:
			return a_deadline.clock == ClockDomain::ActiveSeconds;
		default:
			return false;
		}
	}

	double Scheduler::ClockValue(const GameTime& a_now, ClockDomain a_clock)
	{
		return a_clock == ClockDomain::CalendarGameHours ? a_now.calendarHours : a_now.activeSeconds;
	}

	DeadlineUpdateStatus Scheduler::upsert(const Deadline& a_deadline)
	{
		if (!Valid(a_deadline)) {
			return DeadlineUpdateStatus::RejectedInvalid;
		}

		const auto found = entries_.find(a_deadline.id);
		if (found == entries_.end()) {
			auto& queue = a_deadline.clock == ClockDomain::CalendarGameHours ? calendarQueue_ : activeQueue_;
			queue.emplace(a_deadline.due, a_deadline.id);
			try {
				entries_.emplace(a_deadline.id, Entry{ a_deadline });
			} catch (...) {
				queue.erase(DeadlineKey{ a_deadline.due, a_deadline.id });
				throw;
			}
			return DeadlineUpdateStatus::Updated;
		}

		const auto& oldDeadline = found->second.deadline;
		if (oldDeadline.clock == a_deadline.clock && oldDeadline.due == a_deadline.due) {
			found->second.deadline = a_deadline;
			return DeadlineUpdateStatus::Updated;
		}

		auto& newQueue = a_deadline.clock == ClockDomain::CalendarGameHours ? calendarQueue_ : activeQueue_;
		newQueue.emplace(a_deadline.due, a_deadline.id);
		EraseIndex(oldDeadline);
		found->second.deadline = a_deadline;
		return DeadlineUpdateStatus::Updated;
	}

	bool Scheduler::updateRevision(DeadlineId a_id, Revision a_revision, SessionEpoch a_epoch)
	{
		const auto found = entries_.find(a_id);
		if (found == entries_.end() || found->second.deadline.epoch != a_epoch) {
			return false;
		}
		found->second.deadline.ownerRevision = a_revision;
		return true;
	}

	bool Scheduler::cancel(DeadlineId a_id)
	{
		const auto found = entries_.find(a_id);
		if (found == entries_.end()) {
			return false;
		}
		EraseIndex(found->second.deadline);
		entries_.erase(found);
		return true;
	}

	void Scheduler::cancelEpoch(SessionEpoch a_epoch)
	{
		for (auto it = entries_.begin(); it != entries_.end();) {
			if (it->second.deadline.epoch == a_epoch) {
				EraseIndex(it->second.deadline);
				it = entries_.erase(it);
			} else {
				++it;
			}
		}
	}

	void Scheduler::EraseIndex(const Deadline& a_deadline) noexcept
	{
		auto& queue = a_deadline.clock == ClockDomain::CalendarGameHours ? calendarQueue_ : activeQueue_;
		queue.erase(DeadlineKey{ a_deadline.due, a_deadline.id });
	}

	std::optional<DeadlineElapsed> Scheduler::popDue(const GameTime& a_now)
	{
		while (true) {
			const bool calendarReady = !calendarQueue_.empty() &&
				calendarQueue_.begin()->first <= ClockValue(a_now, ClockDomain::CalendarGameHours);
			const bool activeReady = !activeQueue_.empty() &&
				activeQueue_.begin()->first <= ClockValue(a_now, ClockDomain::ActiveSeconds);
			if (!calendarReady && !activeReady) {
				return std::nullopt;
			}

			const bool takeCalendar = calendarReady &&
				(!activeReady || !hasPoppedClock_ || lastPoppedClock_ != ClockDomain::CalendarGameHours);
			auto& queue = takeCalendar ? calendarQueue_ : activeQueue_;
			const auto item = *queue.begin();
			queue.erase(queue.begin());

			const auto found = entries_.find(item.second);
			if (found == entries_.end()) {
				continue;
			}
			const auto deadline = found->second.deadline;
			entries_.erase(found);
			lastPoppedClock_ = deadline.clock;
			hasPoppedClock_ = true;
			return DeadlineElapsed{
				deadline.id,
				deadline.actor,
				deadline.kind,
				deadline.epoch,
				deadline.region,
				deadline.ownerRevision };
		}
	}

	std::vector<DeadlineElapsed> Scheduler::takeDue(const GameTime& a_now, std::size_t a_maxItems)
	{
		std::vector<DeadlineElapsed> elapsed;
		if (a_maxItems == 0) {
			return elapsed;
		}
		elapsed.reserve(std::min<std::size_t>(a_maxItems, 64));
		while (elapsed.size() < a_maxItems) {
			auto due = popDue(a_now);
			if (!due) {
				break;
			}
			elapsed.push_back(std::move(*due));
		}
		return elapsed;
	}

	DeadlineUpdateStatus Scheduler::upsertSteady(DeadlineId a_id, SteadyTime a_due)
	{
		if (a_id == 0) {
			return DeadlineUpdateStatus::RejectedInvalid;
		}
		const auto found = steadyEntries_.find(a_id);
		if (found == steadyEntries_.end()) {
			steadyQueue_.emplace(a_due, a_id);
			try {
				steadyEntries_.emplace(a_id, SteadyEntry{ a_due });
			} catch (...) {
				steadyQueue_.erase({ a_due, a_id });
				throw;
			}
			return DeadlineUpdateStatus::Updated;
		}
		if (found->second.due != a_due) {
			steadyQueue_.emplace(a_due, a_id);
			steadyQueue_.erase({ found->second.due, a_id });
			found->second.due = a_due;
		}
		return DeadlineUpdateStatus::Updated;
	}

	bool Scheduler::cancelSteady(DeadlineId a_id)
	{
		const auto found = steadyEntries_.find(a_id);
		if (found == steadyEntries_.end()) {
			return false;
		}
		steadyQueue_.erase({ found->second.due, a_id });
		steadyEntries_.erase(found);
		return true;
	}

	std::optional<DeadlineId> Scheduler::popDueSteady(SteadyTime a_now)
	{
		if (steadyQueue_.empty() || steadyQueue_.begin()->first > a_now) {
			return std::nullopt;
		}
		const auto item = *steadyQueue_.begin();
		steadyQueue_.erase(steadyQueue_.begin());
		steadyEntries_.erase(item.second);
		return item.second;
	}

	std::vector<DeadlineId> Scheduler::takeDueSteady(SteadyTime a_now, std::size_t a_maxItems)
	{
		std::vector<DeadlineId> ids;
		if (a_maxItems == 0) {
			return ids;
		}
		while (ids.size() < a_maxItems) {
			auto due = popDueSteady(a_now);
			if (!due) {
				break;
			}
			ids.push_back(*due);
		}
		return ids;
	}

	bool Scheduler::hasDue(const GameTime& a_now)
	{
		const bool calendarDue = !calendarQueue_.empty() &&
			calendarQueue_.begin()->first <= ClockValue(a_now, ClockDomain::CalendarGameHours);
		const bool activeDue = !activeQueue_.empty() &&
			activeQueue_.begin()->first <= ClockValue(a_now, ClockDomain::ActiveSeconds);
		return calendarDue || activeDue;
	}

	bool Scheduler::hasDueSteady(SteadyTime a_now)
	{
		return !steadyQueue_.empty() && steadyQueue_.begin()->first <= a_now;
	}

	std::size_t Scheduler::size() const noexcept
	{
		return entries_.size() + steadyEntries_.size();
	}

	std::size_t Scheduler::indexSize() const noexcept
	{
		return calendarQueue_.size() + activeQueue_.size() + steadyQueue_.size();
	}

	void Scheduler::clear() noexcept
	{
		entries_.clear();
		steadyEntries_.clear();
		calendarQueue_.clear();
		activeQueue_.clear();
		steadyQueue_.clear();
		hasPoppedClock_ = false;
	}
}
