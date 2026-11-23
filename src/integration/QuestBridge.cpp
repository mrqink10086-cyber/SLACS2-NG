#include "integration/QuestBridge.h"

#include <utility>

namespace slacs
{
	namespace
	{
		constexpr RE::FormID kQuestLocalFormId = 0x000D62;
		constexpr auto kQuestPlugin = "SLACS_2_Plus_SCO.esp";
		constexpr auto kQuestClass = "SLACS_Main";
	}

	struct QuestBridge::RegistrationCallback final : RE::BSScript::IStackCallbackFunctor
	{
		explicit RegistrationCallback(std::shared_ptr<std::atomic<Status>> a_registration) :
			registration(std::move(a_registration))
		{}

		void operator()(RE::BSScript::Variable a_result) override
		{
			registration->store(a_result.IsBool() && a_result.GetBool() ?
				Status::Subscribed : Status::ConfirmationFailed, std::memory_order_release);
		}

		bool CanSave() const override { return false; }
		void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

		std::shared_ptr<std::atomic<Status>> registration;
	};

	QuestBridge::Status QuestBridge::dispatchLocked(const char* a_function,
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>& a_callback)
	{
		auto* skyrimVM = RE::SkyrimVM::GetSingleton();
		auto* vm = skyrimVM ? skyrimVM->impl.get() : nullptr;
		auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
		if (!policy) {
			return Status::VmUnavailable;
		}
		const auto handle = policy->GetHandleForObject(RE::FormType::Quest, quest_);
		RE::BSTSmartPointer<RE::BSScript::Object> script;
		if (handle == policy->EmptyHandle() || !vm->FindBoundObject(handle, kQuestClass, script) || !script) {
			return Status::ScriptNotBound;
		}

		auto* arguments = RE::MakeFunctionArguments();
		if (!vm->DispatchMethodCall1(script, RE::BSFixedString(a_function), arguments, a_callback)) {
			delete arguments;
			return Status::DispatchRejected;
		}
		return Status::AwaitingConfirmation;
	}

	QuestBridge::Status QuestBridge::trySubscribe()
	{
		std::lock_guard lock(mutex_);
		if (registration_) {
			const auto status = registration_->load(std::memory_order_acquire);
			if (status == Status::ConfirmationFailed) {
				registration_.reset();
			}
			return status;
		}
		if (!quest_ || quest_->IsDeleted()) {
			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			quest_ = dataHandler ? dataHandler->LookupForm<RE::TESQuest>(kQuestLocalFormId, kQuestPlugin) : nullptr;
			if (!quest_ || quest_->IsDeleted()) {
				return Status::QuestUnavailable;
			}
		}

		auto registration = std::make_shared<std::atomic<Status>>(Status::AwaitingConfirmation);
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new RegistrationCallback(registration));
		const auto status = dispatchLocked("RegisterEvents", callback);
		if (status != Status::AwaitingConfirmation) {
			return status;
		}
		registration_ = std::move(registration);
		return registration_->load(std::memory_order_acquire);
	}

	void QuestBridge::invalidate()
	{
		std::lock_guard lock(mutex_);
		if (registration_ && quest_ && !quest_->IsDeleted()) {
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
			(void)dispatchLocked("UnregisterEvents", callback);
		}
		registration_.reset();
		quest_ = nullptr;
	}

	void QuestBridge::reset()
	{
		invalidate();
	}
}
