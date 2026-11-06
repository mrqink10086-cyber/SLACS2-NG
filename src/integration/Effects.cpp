#include "integration/Effects.h"

#include <RE/Skyrim.h>

#include <array>
#include <cstdint>

namespace slacs
{
	namespace
	{
		constexpr const char* kPluginName = "SLACS_2_Plus_SCO.esp";

		// Local FormIDs of spell_oral1..spell_anal2_vaginal2_oral2 in
		// SLACS_2_Plus_SCO.esp. The array index is the public combination
		// index: (analLevel * 9) + (vaginalLevel * 3) + oralLevel.
		constexpr std::array<FormId, 27> kSpellLocalIds{
			0,
			0x002DB1, 0x002DB2, 0x002DB4, 0x002DB6, 0x002DB8, 0x002DBE, 0x002DBA, 0x002DBC,
			0x002DC0, 0x002DC4, 0x002DC6, 0x002DCC, 0x002DD4, 0x002DD6, 0x002DCE, 0x002DD8, 0x002DDA,
			0x002DC2, 0x002DC8, 0x002DCA, 0x002DD0, 0x002DDC, 0x002DDE, 0x002DD2, 0x002DE0, 0x002DE2,
		};

		bool ValidateSpell(RE::SpellItem* a_spell)
		{
			if (!a_spell || a_spell->effects.empty()) {
				return false;
			}
			const auto* first = a_spell->effects[0];
			return first && first->baseEffect && first->baseEffect->data.effectShader;
		}
	}

	bool Effects::initialize()
	{
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			return false;
		}

		std::array<RE::SpellItem*, 27> spells{};
		for (std::size_t index = 1; index < kSpellLocalIds.size(); ++index) {
			auto* spell = dataHandler->LookupForm<RE::SpellItem>(kSpellLocalIds[index], kPluginName);
			if (!ValidateSpell(spell)) {
				logs::error("Effects: missing or invalid spell at combination {}", index);
				return false;
			}
			spells[index] = spell;
		}

		spells_ = spells;
		applied_.clear();
		initialized_ = true;
		return true;
	}

	EffectResult Effects::reconcile(const ActorLease& a_actor, const SpellTarget& a_target)
	{
		EffectResult result{};
		result.actor = a_target.actor;
		result.desired = a_target.desired;
		result.revision = a_target.revision;

		if (!a_actor.actor) {
			result.status = EffectStatus::DeferredActorUnavailable;
			return result;
		}
		if (!initialized_) {
			result.status = EffectStatus::ApiUnavailable;
			result.error = EffectErrorCode::ApiFailure;
			return result;
		}
		if (a_target.desired.combinationIndex >= spells_.size()) {
			result.status = EffectStatus::Failed;
			result.error = EffectErrorCode::MissingForm;
			return result;
		}

		const bool wantSpell = a_target.desired.enabled && a_target.desired.combinationIndex != 0;
		RE::SpellItem* desiredSpell = wantSpell ? spells_[a_target.desired.combinationIndex] : nullptr;
		if (wantSpell && !desiredSpell) {
			result.status = EffectStatus::Failed;
			result.error = EffectErrorCode::MissingForm;
			return result;
		}

		const auto cached = applied_.find(a_target.actor);
		if (cached != applied_.end() && a_target.revision < cached->second.revision) {
			result.status = EffectStatus::StaleRevision;
			result.error = EffectErrorCode::StaleRevision;
			return result;
		}

		auto* actor = a_actor.actor.get();
		bool changed = false;
		for (std::size_t index = 1; index < spells_.size(); ++index) {
			auto* spell = spells_[index];
			if (!spell || spell == desiredSpell) {
				continue;
			}
			if (actor->HasSpell(spell) && actor->RemoveSpell(spell)) {
				changed = true;
			}
		}
		if (desiredSpell && !actor->HasSpell(desiredSpell)) {
			if (actor->AddSpell(desiredSpell)) {
				changed = true;
			}
		}

		applied_[a_target.actor] = Applied{
			static_cast<std::uint8_t>(a_target.desired.combinationIndex),
			a_target.revision,
		};

		if (changed) {
			result.status = EffectStatus::Applied;
		} else {
			result.status = EffectStatus::NoChange;
		}
		return result;
	}

	void Effects::invalidate(ActorId a_actor)
	{
		applied_.erase(a_actor);
	}

	void Effects::clearSession(SessionEpoch)
	{
		applied_.clear();
	}
}
