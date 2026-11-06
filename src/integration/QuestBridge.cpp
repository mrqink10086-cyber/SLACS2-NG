#include "integration/QuestBridge.h"

#include <RE/I/IVirtualMachine.h>
#include <RE/V/VirtualMachine.h>
#include <RE/Z/ZeroFunctionArguments.h>

namespace slacs
{
	namespace
	{
		constexpr RE::FormID kQuestLocalFormId = 0x000D62;
		constexpr std::string_view kQuestPlugin = "SLACS_2_Plus_SCO.esp";
		constexpr std::string_view kQuestClass = "SLACS_Main";
	}

	bool QuestBridge::ensureQuestLocked()
	{
		if (quest_ && !quest_->IsDeleted() && quest_->HasVMAD()) {
			return true;
		}
		quest_ = nullptr;
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			return false;
		}
		quest_ = dataHandler->LookupForm<RE::TESQuest>(kQuestLocalFormId, kQuestPlugin);
		return quest_ && !quest_->IsDeleted() && quest_->HasVMAD();
	}

	bool QuestBridge::dispatchLocked(const char* a_function)
	{
		if (!ensureQuestLocked()) {
			return false;
		}
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm) {
			return false;
		}
		auto* policy = vm->GetObjectHandlePolicy();
		if (!policy) {
			return false;
		}
		const auto handle = policy->GetHandleForObject(RE::FormType::Quest, quest_);
		if (handle == policy->EmptyHandle()) {
			return false;
		}

		RE::BSScript::ZeroFunctionArguments arguments;
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> result;
		return vm->DispatchMethodCall2(
			handle,
			RE::BSFixedString(kQuestClass),
			RE::BSFixedString(a_function),
			std::addressof(arguments),
			result);
	}

	bool QuestBridge::trySubscribe()
	{
		std::lock_guard lock(mutex_);
		if (!ensureQuestLocked()) {
			subscribed_ = false;
			return false;
		}
		if (!dispatchLocked("UnregisterEvents")) {
			subscribed_ = false;
			return false;
		}
		if (!dispatchLocked("RegisterEvents")) {
			subscribed_ = false;
			return false;
		}
		subscribed_ = true;
		return true;
	}

	void QuestBridge::unsubscribe()
	{
		std::lock_guard lock(mutex_);
		if (quest_ && !quest_->IsDeleted()) {
			(void)dispatchLocked("UnregisterEvents");
		}
		subscribed_ = false;
	}

	void QuestBridge::invalidate()
	{
		std::lock_guard lock(mutex_);
		if (subscribed_ && quest_ && !quest_->IsDeleted()) {
			(void)dispatchLocked("UnregisterEvents");
		}
		subscribed_ = false;
		quest_ = nullptr;
	}

	void QuestBridge::reset()
	{
		std::lock_guard lock(mutex_);
		if (subscribed_ && quest_ && !quest_->IsDeleted()) {
			(void)dispatchLocked("UnregisterEvents");
		}
		subscribed_ = false;
		quest_ = nullptr;
	}

	bool QuestBridge::subscribed() const
	{
		std::lock_guard lock(mutex_);
		return subscribed_;
	}
}
