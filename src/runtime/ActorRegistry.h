#pragma once

#include "core/Types.h"

#include <RE/Skyrim.h>

#include <mutex>
#include <optional>
#include <unordered_map>

namespace slacs
{
	enum class ResolveNeed : std::uint8_t
	{
		ActorOnly,
		ActorWith3D,
	};

	struct ActorLease
	{
		RE::ActorHandle handle;
		RE::NiPointer<RE::Actor> actor;
		RE::NiPointer<RE::NiAVObject> root;
	};

	class ActorRegistry
	{
	public:
		std::optional<ActorId> bind(FormId a_formId);
		std::optional<ActorId> find(FormId a_formId) const;
		std::optional<ActorId> remove(FormId a_formId);
		std::optional<ActorLease> resolve(ActorId a_actor, ResolveNeed a_need);
		std::optional<FormId> formId(ActorId a_actor) const;
		void reset(SessionEpoch a_epoch);

	private:
		mutable std::mutex mutex_;
		std::unordered_map<FormId, ActorId> formToActor_;
		std::unordered_map<ActorId, FormId> actorToForm_;
		std::unordered_map<ActorId, RE::ActorHandle> handles_;
		ActorId nextActorId_{ 1 };
		SessionEpoch epoch_{};
	};
}
