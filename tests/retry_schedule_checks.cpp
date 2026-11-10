#include "runtime/RetrySchedule.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>

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

#define EXPECT(a_expression) Expect((a_expression), #a_expression, __LINE__)
}

int main()
{
	using namespace std::chrono;
	using Schedule = slacs::RetrySchedule<std::uint64_t>;
	const auto now = Schedule::TimePoint{} + seconds{ 10 };
	Schedule schedule;
	std::map<std::uint64_t, int> dirty;
	for (std::uint64_t key = 1; key <= 65; ++key) {
		dirty.emplace(key, 0);
		schedule.defer(key, now + seconds{ 2 });
	}

	EXPECT(!schedule.hasReady(dirty, now));
	EXPECT(schedule.nextReadyAt(dirty, now) == now + seconds{ 2 });
	EXPECT(!schedule.hasReady(dirty, now + milliseconds{ 1999 }));
	EXPECT(schedule.hasReady(dirty, now + seconds{ 2 }));

	dirty.emplace(66, 0);
	EXPECT(schedule.hasReady(dirty, now));
	EXPECT(schedule.nextReadyAt(dirty, now) == now);
	schedule.defer(66, now + seconds{ 4 });
	EXPECT(!schedule.ready(66, now));
	schedule.clear(66);
	EXPECT(schedule.ready(66, now));
	dirty.erase(66);
	schedule.clear();
	EXPECT(schedule.hasReady(dirty, now));

	if (g_failures != 0) {
		std::cerr << g_failures << " retry schedule check(s) failed\n";
		return 1;
	}
	std::cout << "retry schedule checks passed\n";
	return 0;
}
