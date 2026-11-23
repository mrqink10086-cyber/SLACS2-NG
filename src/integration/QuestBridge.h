#pragma once

#include <RE/Skyrim.h>

#include <atomic>
#include <memory>
#include <mutex>

namespace slacs
{
	class QuestBridge
	{
	public:
		enum class Status
		{
			QuestUnavailable,
			VmUnavailable,
			ScriptNotBound,
			DispatchRejected,
			AwaitingConfirmation,
			ConfirmationFailed,
			Subscribed,
		};

		Status trySubscribe();
		void invalidate();
		void reset();

	private:
		struct RegistrationCallback;
		Status dispatchLocked(const char* a_function,
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>& a_callback);

		std::mutex mutex_;
		RE::TESQuest* quest_{};
		std::shared_ptr<std::atomic<Status>> registration_;
	};
}
