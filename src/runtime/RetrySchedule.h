#pragma once

#include <chrono>
#include <map>
#include <optional>

namespace slacs
{
	template <class Key>
	class RetrySchedule
	{
	public:
		using TimePoint = std::chrono::steady_clock::time_point;

		void defer(const Key& a_key, TimePoint a_due)
		{
			due_[a_key] = a_due;
		}

		void clear(const Key& a_key)
		{
			due_.erase(a_key);
		}

		void clear() noexcept
		{
			due_.clear();
		}

		[[nodiscard]] bool ready(const Key& a_key, TimePoint a_now) const
		{
			const auto found = due_.find(a_key);
			return found == due_.end() || a_now >= found->second;
		}

		template <class Items>
		[[nodiscard]] bool hasReady(const Items& a_items, TimePoint a_now) const
		{
			for (const auto& [key, value] : a_items) {
				(void)value;
				if (ready(key, a_now)) {
					return true;
				}
			}
			return false;
		}

		template <class Items>
		[[nodiscard]] std::optional<TimePoint> nextReadyAt(const Items& a_items, TimePoint a_now) const
		{
			std::optional<TimePoint> next;
			for (const auto& [key, value] : a_items) {
				(void)value;
				const auto found = due_.find(key);
				const auto candidate = found == due_.end() ? a_now : found->second;
				if (!next || candidate < *next) {
					next = candidate;
				}
			}
			return next;
		}

	private:
		std::map<Key, TimePoint> due_;
	};
}
