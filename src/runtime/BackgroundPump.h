#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace slacs
{
	class BackgroundPump
	{
	public:
		using Tick = std::function<void()>;

		explicit BackgroundPump(Tick a_tick);
		~BackgroundPump();

		BackgroundPump(const BackgroundPump&) = delete;
		BackgroundPump& operator=(const BackgroundPump&) = delete;

		void start();
		void stop();
		void request();
		void requestAfter(std::chrono::milliseconds a_delay);

	private:
		void run();

		Tick tick_;
		std::jthread thread_;
		std::mutex mutex_;
		std::condition_variable condition_;
		std::atomic<bool> stopping_{ false };
		bool wakeRequested_{};
		std::chrono::steady_clock::time_point due_{};
		std::atomic<bool> taskPending_{ false };
	};
}
