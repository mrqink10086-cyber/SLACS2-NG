#include "runtime/BackgroundPump.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <latch>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>

#include <SKSE/SKSE.h>

namespace
{
	using namespace std::chrono_literals;
	int failures{};

	void Expect(bool a_condition, std::string_view a_message)
	{
		if (!a_condition) {
			++failures;
			std::cerr << "FAIL: " << a_message << '\n';
		}
	}

	bool WaitForTaskCount(std::size_t a_count)
	{
		return fake_skse::waitForTasks(a_count, 2s);
	}

	void CheckQueuedTaskStoppedBeforeExecution()
	{
		std::atomic<int> ticks{};
		slacs::BackgroundPump pump([&] { ++ticks; });
		pump.start();
		pump.request();
		Expect(WaitForTaskCount(1), "pump queues a requested task");
		pump.stop();
		Expect(fake_skse::runNext(), "stopped queued task remains safely executable");
		Expect(ticks.load() == 0, "queued task does not tick after stop");
	}

	void CheckStopWaitsForRunningTick()
	{
		std::mutex mutex;
		std::condition_variable condition;
		bool tickEntered{};
		bool releaseTick{};
		std::atomic<bool> tickExited{};
		std::atomic<int> ticks{};
		slacs::BackgroundPump pump([&] {
			++ticks;
			std::unique_lock lock(mutex);
			tickEntered = true;
			condition.notify_all();
			condition.wait(lock, [&] { return releaseTick; });
			tickExited.store(true);
		});
		pump.start();
		pump.request();
		Expect(WaitForTaskCount(1), "running-tick case queues a task");
		std::thread taskThread([] { fake_skse::runNext(); });
		{
			std::unique_lock lock(mutex);
			Expect(condition.wait_for(lock, 2s, [&] { return tickEntered; }), "tick enters before stop");
		}

		std::atomic<bool> stopReturned{};
		std::atomic<bool> stopSawTickExit{};
		std::latch stopStarted{ 1 };
		std::thread stopThread([&] {
			stopStarted.count_down();
			pump.stop();
			stopSawTickExit.store(tickExited.load());
			stopReturned.store(true);
		});
		stopStarted.wait();
		std::this_thread::sleep_for(30ms);
		Expect(!stopReturned.load(), "stop waits while a tick is in flight");
		{
			std::lock_guard lock(mutex);
			releaseTick = true;
		}
		condition.notify_all();
		taskThread.join();
		stopThread.join();
		Expect(stopReturned.load(), "stop returns after in-flight tick completes");
		Expect(stopSawTickExit.load(), "stop observes tick exit before returning");
		Expect(ticks.load() == 1, "one in-flight tick ran");
	}

	void CheckQueuedTaskAfterDestruction()
	{
		std::atomic<int> ticks{};
		auto pump = std::make_unique<slacs::BackgroundPump>([&] { ++ticks; });
		pump->start();
		pump->request();
		Expect(WaitForTaskCount(1), "destruction case queues a task");
		pump.reset();
		Expect(fake_skse::runNext(), "queued task can execute after owner destruction");
		Expect(ticks.load() == 0, "task retained control state without calling destroyed owner");
	}

	void CheckRestartRejectsOldGeneration()
	{
		std::atomic<int> ticks{};
		slacs::BackgroundPump pump([&] { ++ticks; });
		pump.start();
		pump.start();
		pump.request();
		Expect(WaitForTaskCount(1), "first generation queues once");
		pump.stop();

		pump.start();
		pump.request();
		Expect(WaitForTaskCount(2), "restarted generation queues a new task");
		Expect(fake_skse::runNext(), "old generation task is executable");
		Expect(ticks.load() == 0, "old generation task is rejected after restart");
		Expect(fake_skse::runNext(), "new generation task is executable");
		Expect(ticks.load() == 1, "new generation task ticks exactly once");
		pump.stop();
	}

	void CheckStopFromTickDoesNotWaitOnItself()
	{
		std::mutex mutex;
		std::condition_variable condition;
		bool tickEntered{};
		bool callStop{};
		std::atomic<bool> returned{};
		slacs::BackgroundPump* pumpPointer{};
		slacs::BackgroundPump pump([&] {
			{
				std::unique_lock lock(mutex);
				tickEntered = true;
				condition.notify_all();
				condition.wait(lock, [&] { return callStop; });
			}
			pumpPointer->stop();
			returned.store(true);
		});
		pumpPointer = &pump;
		pump.start();
		pump.request();
		Expect(WaitForTaskCount(1), "self-stop case queues a task");
		std::thread taskThread([] { fake_skse::runNext(); });
		{
			std::unique_lock lock(mutex);
			Expect(condition.wait_for(lock, 2s, [&] { return tickEntered; }), "self-stop tick enters");
		}
		std::atomic<bool> externalStopReturned{};
		std::latch stopStarted{ 1 };
		std::thread stopThread([&] {
			stopStarted.count_down();
			pump.stop();
			externalStopReturned.store(true);
		});
		stopStarted.wait();
		{
			std::lock_guard lock(mutex);
			callStop = true;
		}
		condition.notify_all();
		taskThread.join();
		stopThread.join();
		Expect(returned.load(), "stop called from tick returns without self-deadlock");
		Expect(externalStopReturned.load(), "external stop completes after self-stopping tick exits");
	}
}

int main()
{
	CheckQueuedTaskStoppedBeforeExecution();
	CheckStopWaitsForRunningTick();
	CheckQueuedTaskAfterDestruction();
	CheckRestartRejectsOldGeneration();
	CheckStopFromTickDoesNotWaitOnItself();
	if (failures != 0) {
		std::cerr << failures << " background pump checks failed\n";
		return 1;
	}
	std::cout << "BackgroundPump checks passed\n";
	return 0;
}
