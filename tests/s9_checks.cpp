#include "runtime/ActiveClock.h"
#include "runtime/RoundRobin.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition) {
			std::fprintf(stderr, "SLACS2 NG S9 check failed: %s\n", message);
			std::exit(1);
		}
	}

	void Approximately(double actual, double expected, const char* message)
	{
		Check(std::fabs(actual - expected) < 0.0001, message);
	}
}

int main()
{
	slacs::ActiveClock clock;

	Approximately(clock.advance(1000, false), 0.0, "first sample must seed the baseline");
	Approximately(clock.advance(1500, false), 0.5, "running samples must accumulate");
	Approximately(clock.advance(6500, true), 0.5, "paused samples must not accumulate");
	Approximately(clock.advance(7000, true), 0.5, "repeated paused samples must stay frozen");
	Approximately(clock.advance(7500, false), 1.0, "resume must count only post-pause time");

	slacs::ActiveClock wrapping;
	Approximately(wrapping.advance(0xFFFFFE00u, false), 0.0, "wrap baseline must start at zero");
	Approximately(wrapping.advance(0xFFFFFF00u, false), 0.256, "pre-wrap movement must accumulate");
	Approximately(wrapping.advance(0x00000100u, false), 0.768, "32-bit wrap must count only the wrapped delta");

	std::map<unsigned, bool> pending;
	for (unsigned key = 0; key < 130; ++key) {
		pending.emplace(key, key >= 128);
	}
	slacs::RoundRobin<unsigned> cursor;
	std::set<unsigned> visited;
	for (unsigned pass = 0; pass < 3; ++pass) {
		const auto limit = std::min<std::size_t>(64, pending.size());
		for (std::size_t i = 0; i < limit; ++i) {
			auto next = cursor.next(pending);
			visited.insert(next->first);
			if (next->second) {
				pending.erase(next);
			}
		}
	}
	Check(visited.size() == 130, "unavailable targets must not starve later ready targets");
	Check(pending.size() == 128, "ready targets must drain despite unavailable predecessors");
	pending.clear();
	Check(cursor.next(pending) == pending.end(), "empty queues must not produce an iterator");
	pending.emplace(9, false);
	Check(cursor.next(pending)->first == 9, "cursor must recover after clear and refill");
	pending.erase(9);
	pending.emplace(4, false);
	pending.emplace(12, false);
	Check(cursor.next(pending)->first == 12, "erasing the last key must preserve forward progress");
	Check(cursor.next(pending)->first == 4, "cursor must wrap to new lower keys");
	cursor.reset();
	Check(cursor.next(pending)->first == 4, "session reset must restart at the first key");

	std::puts("SLACS2 NG S9 checks passed");
	return 0;
}
