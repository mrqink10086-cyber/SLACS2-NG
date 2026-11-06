#include "runtime/ActorRegistry.h"

namespace slacs
{
	std::optional<ActorId> ActorRegistry::bind(FormId a_formId)
	{
		if (a_formId == kInvalidFormId) {
			return std::nullopt;
		}

		auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_formId);
		if (!actor || actor->IsDeleted()) {
			return std::nullopt;
		}
		const auto handle = actor->GetHandle();
		if (!handle) {
			return std::nullopt;
		}

		std::lock_guard lock(mutex_);
		const auto found = formToActor_.find(a_formId);
		if (found != formToActor_.end()) {
			return found->second;
		}

		const auto actorId = nextActorId_++;
		formToActor_.emplace(a_formId, actorId);
		actorToForm_.emplace(actorId, a_formId);
		handles_.emplace(actorId, handle);
		return actorId;
	}

	std::optional<ActorLease> ActorRegistry::resolve(ActorId a_actor, ResolveNeed a_need)
	{
		FormId formId = kInvalidFormId;
		RE::ActorHandle storedHandle;
		{
			std::lock_guard lock(mutex_);
			const auto found = actorToForm_.find(a_actor);
			if (found == actorToForm_.end()) {
				return std::nullopt;
			}
			formId = found->second;
			if (const auto handleIt = handles_.find(a_actor); handleIt != handles_.end()) {
				storedHandle = handleIt->second;
			}
		}

		auto actorPtr = storedHandle.get();
		auto* actor = actorPtr.get();
		if (actor && actor->GetFormID() != formId) {
			// The stored handle was recycled for a different form. Never mix
			// that actor's data with the FormID-bound business state.
			actor = nullptr;
			actorPtr.reset();
			storedHandle = {};
		}
		if (!actor) {
			actor = RE::TESForm::LookupByID<RE::Actor>(formId);
			actorPtr.reset(actor);
		}
		if (!actor || actor->IsDeleted() || actor->GetFormID() != formId) {
			return std::nullopt;
		}

		const auto handle = actor->GetHandle();
		if (!handle) {
			return std::nullopt;
		}

		ActorLease lease{};
		lease.handle = handle;
		lease.actor = std::move(actorPtr);
		if (a_need == ResolveNeed::ActorWith3D) {
			if (!actor->Is3DLoaded()) {
				return std::nullopt;
			}
			auto* root = actor->Get3D();
			if (!root) {
				return std::nullopt;
			}
			lease.root.reset(root);
		}
		return lease;
	}

	std::optional<ActorId> ActorRegistry::find(FormId a_formId) const
	{
		std::lock_guard lock(mutex_);
		const auto found = formToActor_.find(a_formId);
		if (found == formToActor_.end()) {
			return std::nullopt;
		}
		return found->second;
	}

	std::optional<ActorId> ActorRegistry::remove(FormId a_formId)
	{
		std::lock_guard lock(mutex_);
		const auto found = formToActor_.find(a_formId);
		if (found == formToActor_.end()) {
			return std::nullopt;
		}
		const auto actorId = found->second;
		formToActor_.erase(found);
		actorToForm_.erase(actorId);
		handles_.erase(actorId);
		return actorId;
	}

	std::optional<FormId> ActorRegistry::formId(ActorId a_actor) const
	{
		std::lock_guard lock(mutex_);
		const auto found = actorToForm_.find(a_actor);
		if (found == actorToForm_.end()) {
			return std::nullopt;
		}
		return found->second;
	}

	void ActorRegistry::reset(SessionEpoch a_epoch)
	{
		std::lock_guard lock(mutex_);
		formToActor_.clear();
		actorToForm_.clear();
		handles_.clear();
		nextActorId_ = 1;
		epoch_ = a_epoch;
	}
}
