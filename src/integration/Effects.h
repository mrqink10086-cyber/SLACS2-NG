#pragma once

#include "core/Types.h"
#include "runtime/ActorRegistry.h"

#include <array>
#include <unordered_map>

namespace RE
{
	class SpellItem;
}

namespace slacs
{
	enum class EffectStatus : std::uint8_t
	{
		Applied,
		NoChange,
		DeferredActorUnavailable,
		ApiUnavailable,
		StaleRevision,
		Failed,
	};

	enum class EffectErrorCode : std::uint8_t
	{
		None,
		MissingForm,
		StaleRevision,
		ApiFailure,
	};

	struct EffectResult
	{
		EffectStatus status{ EffectStatus::Failed };
		ActorId actor{};
		DesiredSpell desired{};
		Revision revision{};
		EffectErrorCode error{ EffectErrorCode::None };
	};

	class Effects
	{
	public:
		bool initialize();

		EffectResult reconcile(const ActorLease& a_actor, const SpellTarget& a_target);
		void invalidate(ActorId a_actor);
		void clearSession(SessionEpoch a_epoch);

	private:
		struct Applied
		{
			std::uint8_t combinationIndex{};
			Revision revision{};
		};

		std::array<RE::SpellItem*, 27> spells_{};
		std::unordered_map<ActorId, Applied> applied_;
		bool initialized_{};
	};
}
