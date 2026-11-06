#include "runtime/BackgroundPump.h"

#include <SKSE/SKSE.h>

#include <exception>
#include <utility>

namespace slacs
{
	BackgroundPump::BackgroundPump(Tick a_tick) :
		tick_(std::move(a_tick))
	{}

	BackgroundPump::~BackgroundPump()
	{
		stop();
	}

	void BackgroundPump::start()
	{
		if (thread_.joinable()) {
			return;
		}
		stopping_.store(false, std::memory_order_release);
		thread_ = std::jthread([this] { run(); });
	}

	void BackgroundPump::stop()
	{
		stopping_.store(true, std::memory_order_release);
		condition_.notify_all();
		if (thread_.joinable()) {
			thread_.join();
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
		{
			std::lock_guard lock(mutex_);
			if (!wakeRequested_ || due < due_) {
				due_ = due;
			}
			wakeRequested_ = true;
		}
		condition_.notify_one();
	}

	void BackgroundPump::run()
	{
		std::unique_lock lock(mutex_);
		while (!stopping_.load(std::memory_order_acquire)) {
			condition_.wait(lock, [this] {
				return stopping_.load(std::memory_order_acquire) || wakeRequested_;
			});
			if (stopping_.load(std::memory_order_acquire)) {
				break;
			}

			while (wakeRequested_ && !stopping_.load(std::memory_order_acquire)) {
				const auto now = std::chrono::steady_clock::now();
				if (now < due_) {
					condition_.wait_until(lock, due_);
					continue;
				}
				if (taskPending_.load(std::memory_order_acquire)) {
					due_ = std::chrono::steady_clock::now() + std::chrono::milliseconds{ 10 };
					condition_.wait_until(lock, due_);
					continue;
				}
				wakeRequested_ = false;
				break;
			}
			if (stopping_.load(std::memory_order_acquire)) {
				break;
			}

			taskPending_.store(true, std::memory_order_release);
			lock.unlock();
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([this] {
					struct PendingReset
					{
						std::atomic<bool>& pending;
						~PendingReset() { pending.store(false, std::memory_order_release); }
					} reset{ taskPending_ };
					try {
						if (!stopping_.load(std::memory_order_acquire)) {
							tick_();
						}
					} catch (const std::exception& e) {
						logs::error("Pump task exception: {}", e.what());
					} catch (...) {
						logs::error("Pump task unknown exception");
					}
				});
			} else {
				taskPending_.store(false, std::memory_order_release);
				requestAfter(std::chrono::milliseconds{ 100 });
			}
			lock.lock();
		}
	}
}
