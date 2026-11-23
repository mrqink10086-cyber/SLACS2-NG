#pragma once

#include "runtime/Ingress.h"

#include <map>
#include <set>
#include <unordered_map>
#include <utility>

namespace slacs
{
	class PPlusScenes
	{
	public:
		struct StartResult
		{
			bool admitted;
			bool replaced;
		};

		StartResult start(Tid a_tid, SessionEpoch a_epoch)
		{
			const bool wasActive = active_.contains(a_tid);
			const bool hadQueries = queries_.erase(a_tid) != 0;
			if (!wasActive && active_.size() >= 256) {
				return { false, hadQueries };
			}
			active_.insert_or_assign(a_tid, a_epoch);
			return { true, wasActive || hadQueries };
		}

		bool end(Tid a_tid) { return active_.erase(a_tid) != 0; }

		[[nodiscard]] bool isActive(Tid a_tid, SessionEpoch a_epoch) const
		{
			const auto scene = active_.find(a_tid);
			return scene != active_.end() && scene->second == a_epoch;
		}

		void trackAccepted(Tid a_tid, IngressSequence a_sequence)
		{
			queries_[a_tid].outstanding.insert(a_sequence);
		}

		template <class Emit>
		bool complete(Tid a_tid, IngressSequence a_sequence, const PPlusCompleted& a_completion, Emit&& a_emit)
		{
			auto found = queries_.find(a_tid);
			if (found == queries_.end() || !found->second.outstanding.contains(a_sequence)) {
				return false;
			}
			auto& order = found->second;
			order.completed.emplace(a_sequence, a_completion);
			while (!order.outstanding.empty()) {
				auto ready = order.completed.find(*order.outstanding.begin());
				if (ready == order.completed.end()) {
					break;
				}
				PPlusCompleted completion = std::move(ready->second);
				order.completed.erase(ready);
				order.outstanding.erase(order.outstanding.begin());
				a_emit(completion);
			}
			if (order.outstanding.empty()) {
				queries_.erase(found);
			}
			return true;
		}

		void clear()
		{
			active_.clear();
			queries_.clear();
		}

	private:
		struct QueryOrder
		{
			std::set<IngressSequence> outstanding;
			std::map<IngressSequence, PPlusCompleted> completed;
		};

		std::unordered_map<Tid, SessionEpoch> active_;
		std::unordered_map<Tid, QueryOrder> queries_;
	};
}
