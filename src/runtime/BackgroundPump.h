#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
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
		struct Control;

		static void run(std::shared_ptr<Control> a_control, std::uint64_t a_generation);
		static void runQueuedTask(std::shared_ptr<Control> a_control, std::uint64_t a_generation);

		std::shared_ptr<Control> control_;
		std::mutex lifecycleMutex_;
		std::jthread thread_;
	};
}
