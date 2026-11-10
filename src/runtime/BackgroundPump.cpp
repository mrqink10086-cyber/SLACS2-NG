#include "runtime/BackgroundPump.h"

#include <SKSE/SKSE.h>

#include <exception>
#include <utility>

namespace slacs
{
	struct BackgroundPump::Control
	{
		explicit Control(Tick a_tick) : tick(std::move(a_tick)) {}

		Tick tick;
		std::mutex mutex;
		std::condition_variable condition;
		bool stopping{};
		bool wakeRequested{};
		bool taskPending{};
		bool inFlight{};
		std::uint64_t generation{};
		std::uint64_t inFlightGeneration{};
		std::thread::id inFlightThread{};
		std::chrono::steady_clock::time_point due{};
	};

	BackgroundPump::BackgroundPump(Tick a_tick) :
		control_(std::make_shared<Control>(std::move(a_tick)))
	{}

	BackgroundPump::~BackgroundPump()
	{
		stop();
	}

	void BackgroundPump::start()
	{
		const auto control = control_;
		for (;;) {
			std::unique_lock lifecycleLock(lifecycleMutex_);
			if (thread_.joinable()) {
				return;
			}

			std::unique_lock controlLock(control->mutex);
			if (control->inFlight) {
				if (control->inFlightThread == std::this_thread::get_id()) {
					return;
				}
				lifecycleLock.unlock();
				control->condition.wait(controlLock, [&] { return !control->inFlight; });
				continue;
			}

			++control->generation;
			const auto generation = control->generation;
			control->stopping = false;
			control->taskPending = false;
			if (!control->wakeRequested) {
				control->due = {};
			}
			controlLock.unlock();
			thread_ = std::jthread([control, generation] { run(control, generation); });
			return;
		}
	}

	void BackgroundPump::stop()
	{
		const auto control = control_;
		std::jthread worker;
		std::uint64_t stoppedGeneration{};
		{
			std::lock_guard lifecycleLock(lifecycleMutex_);
			{
				std::lock_guard controlLock(control->mutex);
				if (!control->stopping) {
					stoppedGeneration = control->generation;
					control->stopping = true;
					++control->generation;
					control->wakeRequested = false;
					control->taskPending = false;
				} else {
					stoppedGeneration = control->inFlight ? control->inFlightGeneration : control->generation;
				}
			}
			if (thread_.joinable()) {
				worker = std::move(thread_);
			}
		}
		control->condition.notify_all();

		if (worker.joinable()) {
			worker.join();
		}

		std::unique_lock controlLock(control->mutex);
		if (control->inFlight &&
			control->inFlightGeneration == stoppedGeneration &&
			control->inFlightThread != std::this_thread::get_id()) {
			control->condition.wait(controlLock, [&] {
				return !control->inFlight || control->inFlightGeneration != stoppedGeneration;
			});
		}
	}

	void BackgroundPump::request()
	{
		requestAfter(std::chrono::milliseconds{ 0 });
	}

	void BackgroundPump::requestAfter(std::chrono::milliseconds a_delay)
	{
		if (a_delay.count() < 0) {
			a_delay = std::chrono::milliseconds{ 0 };
		}
		const auto due = std::chrono::steady_clock::now() + a_delay;
		const auto control = control_;
		{
			std::lock_guard lock(control->mutex);
			if (!control->wakeRequested || due < control->due) {
				control->due = due;
			}
			control->wakeRequested = true;
		}
		control->condition.notify_one();
	}

	void BackgroundPump::run(std::shared_ptr<Control> a_control, std::uint64_t a_generation)
	{
		std::unique_lock lock(a_control->mutex);
		const auto stopped = [&] {
			return a_control->stopping || a_control->generation != a_generation;
		};

		while (!stopped()) {
			a_control->condition.wait(lock, [&] { return stopped() || a_control->wakeRequested; });
			if (stopped()) {
				break;
			}

			while (a_control->wakeRequested && !stopped()) {
				const auto now = std::chrono::steady_clock::now();
				if (now < a_control->due) {
					a_control->condition.wait_until(lock, a_control->due);
					continue;
				}
				if (a_control->taskPending) {
					a_control->due = std::chrono::steady_clock::now() + std::chrono::milliseconds{ 10 };
					a_control->condition.wait_until(lock, a_control->due);
					continue;
				}
				a_control->wakeRequested = false;
				a_control->taskPending = true;
				break;
			}
			if (stopped()) {
				break;
			}

			lock.unlock();
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([a_control, a_generation] { runQueuedTask(a_control, a_generation); });
			} else {
				{
					std::lock_guard controlLock(a_control->mutex);
					if (!a_control->stopping && a_control->generation == a_generation) {
						a_control->taskPending = false;
						a_control->due = std::chrono::steady_clock::now() + std::chrono::milliseconds{ 100 };
						a_control->wakeRequested = true;
					}
				}
				a_control->condition.notify_one();
			}
			lock.lock();
		}
	}

	void BackgroundPump::runQueuedTask(std::shared_ptr<Control> a_control, std::uint64_t a_generation)
	{
		{
			std::lock_guard lock(a_control->mutex);
			if (a_control->stopping || a_control->generation != a_generation || !a_control->taskPending) {
				return;
			}
			a_control->inFlight = true;
			a_control->inFlightGeneration = a_generation;
			a_control->inFlightThread = std::this_thread::get_id();
		}

		try {
			a_control->tick();
		} catch (const std::exception& e) {
			logs::error("Pump task exception: {}", e.what());
		} catch (...) {
			logs::error("Pump task unknown exception");
		}

		{
			std::lock_guard lock(a_control->mutex);
			a_control->inFlight = false;
			a_control->inFlightThread = {};
			if (a_control->generation == a_generation) {
				a_control->taskPending = false;
			}
		}
		a_control->condition.notify_all();
	}
}
