#pragma once

#include "core/Types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <utility>
#include <unordered_map>
#include <vector>

namespace slacs
{
	// Shared deadline container for the Runtime pump. Each live DeadlineId has
	// exactly one ordered index entry in its clock domain.
	class Scheduler
	{
	public:
		DeadlineUpdateStatus upsert(const Deadline& a_deadline);
		bool updateRevision(DeadlineId a_id, Revision a_revision, SessionEpoch a_epoch);
		bool cancel(DeadlineId a_id);
		void cancelEpoch(SessionEpoch a_epoch);
		std::optional<DeadlineElapsed> popDue(const GameTime& a_now);
		std::vector<DeadlineElapsed> takeDue(const GameTime& a_now, std::size_t a_maxItems);

		DeadlineUpdateStatus upsertSteady(DeadlineId a_id, SteadyTime a_due);
		bool cancelSteady(DeadlineId a_id);
		std::optional<DeadlineId> popDueSteady(SteadyTime a_now);
		std::vector<DeadlineId> takeDueSteady(SteadyTime a_now, std::size_t a_maxItems);

		bool hasDue(const GameTime& a_now);
		bool hasDueSteady(SteadyTime a_now);
		std::size_t size() const noexcept;
		std::size_t indexSize() const noexcept;
		void clear() noexcept;

	private:
		struct Entry
		{
			Deadline deadline;
		};

		static bool Valid(const Deadline& a_deadline);
		static double ClockValue(const GameTime& a_now, ClockDomain a_clock);
		void EraseIndex(const Deadline& a_deadline) noexcept;

		std::unordered_map<DeadlineId, Entry> entries_;
		using DeadlineKey = std::pair<double, DeadlineId>;
		std::set<DeadlineKey> calendarQueue_;
		std::set<DeadlineKey> activeQueue_;
		ClockDomain lastPoppedClock_{ ClockDomain::CalendarGameHours };
		bool hasPoppedClock_{};

		struct SteadyEntry
		{
			SteadyTime due{};
		};

		std::unordered_map<DeadlineId, SteadyEntry> steadyEntries_;
		std::set<std::pair<SteadyTime, DeadlineId>> steadyQueue_;
	};
}
