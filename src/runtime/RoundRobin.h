#pragma once

#include <optional>

namespace slacs
{
	template <class Key>
	class RoundRobin
	{
	public:
		template <class Map>
		auto next(Map& a_items)
		{
			if (a_items.empty()) {
				last_.reset();
				return a_items.end();
			}
			auto next = last_ ? a_items.upper_bound(*last_) : a_items.begin();
			if (next == a_items.end()) {
				next = a_items.begin();
			}
			last_ = next->first;
			return next;
		}

		void reset() noexcept { last_.reset(); }

	private:
		std::optional<Key> last_;
	};
}
