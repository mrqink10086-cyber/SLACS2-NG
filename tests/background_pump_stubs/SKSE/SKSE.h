#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

namespace fake_skse
{
	inline std::mutex mutex;
	inline std::condition_variable condition;
	inline std::deque<std::function<void()>> queuedTasks;

	inline bool waitForTasks(std::size_t a_count, std::chrono::milliseconds a_timeout)
	{
		std::unique_lock lock(mutex);
		return condition.wait_for(lock, a_timeout, [a_count] { return queuedTasks.size() >= a_count; });
	}

	inline std::size_t taskCount()
	{
		std::lock_guard lock(mutex);
		return queuedTasks.size();
	}

	inline bool runNext()
	{
		std::function<void()> task;
		{
			std::lock_guard lock(mutex);
			if (queuedTasks.empty()) {
				return false;
			}
			task = std::move(queuedTasks.front());
			queuedTasks.pop_front();
		}
		task();
		return true;
	}
}

namespace SKSE
{
	struct TaskInterface
	{
		void AddTask(std::function<void()> a_task)
		{
			{
				std::lock_guard lock(fake_skse::mutex);
				fake_skse::queuedTasks.push_back(std::move(a_task));
			}
			fake_skse::condition.notify_all();
		}
	};

	inline TaskInterface* GetTaskInterface()
	{
		static TaskInterface interface;
		return &interface;
	}

	namespace log
	{
		template <class... Args>
		void error(Args&&...)
		{}
	}
}

namespace logs = SKSE::log;
