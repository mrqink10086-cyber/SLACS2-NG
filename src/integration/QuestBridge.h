#pragma once

#include <RE/Skyrim.h>

#include <mutex>

namespace slacs
{
	class QuestBridge
	{
	public:
		bool trySubscribe();
		void unsubscribe();
		void invalidate();
		void reset();
		[[nodiscard]] bool subscribed() const;

	private:
		bool dispatchLocked(const char* a_function);
		bool ensureQuestLocked();

		mutable std::mutex mutex_;
		RE::TESQuest* quest_{};
		bool subscribed_{};
	};
}
