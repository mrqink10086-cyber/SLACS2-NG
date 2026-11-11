#include "integration/Effects.h"

#include <RE/Skyrim.h>

#include <array>
#include <cstdint>

namespace slacs
{
	namespace
	{
		constexpr const char* kPluginName = "SLACS_2_Plus_SCO.esp";

		constexpr std::array<FormId, 27> kSpellLocalIds{
			0,
			0x002DB1, 0x002DB2, 0x002DB4, 0x002DB6, 0x002DB8, 0x002DBE, 0x002DBA, 0x002DBC,
			0x002DC0, 0x002DC4, 0x002DC6, 0x002DCC, 0x002DD4, 0x002DD6, 0x002DCE, 0x002DD8, 0x002DDA,
			0x002DC2, 0x002DC8, 0x002DCA, 0x002DD0, 0x002DDC, 0x002DDE, 0x002DD2, 0x002DE0, 0x002DE2,
		};

		bool ResolveSpell(RE::SpellItem* a_spell, RE::TESEffectShader*& a_shader)
		{
			if (!a_spell || a_spell->effects.empty()) {
				return false;
			}
			const auto* first = a_spell->effects[0];
			if (!first || !first->baseEffect || !first->baseEffect->data.effectShader) {
				return false;
			}
			a_shader = first->baseEffect->data.effectShader;
			return true;
		}

		bool IsPluginShader(const std::array<RE::TESEffectShader*, 27>& a_shaders, RE::TESEffectShader* a_shader)
		{
			for (std::size_t index = 1; index < a_shaders.size(); ++index) {
				if (a_shaders[index] == a_shader) {
					return true;
				}
			}
			return false;
		}
	}

	bool Effects::initialize()
	{
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			return false;
		}

		std::array<RE::SpellItem*, 27> legacySpells{};
		std::array<RE::TESEffectShader*, 27> shaders{};
		for (std::size_t index = 1; index < kSpellLocalIds.size(); ++index) {
			auto* spell = dataHandler->LookupForm<RE::SpellItem>(kSpellLocalIds[index], kPluginName);
			RE::TESEffectShader* shader = nullptr;
			if (!ResolveSpell(spell, shader)) {
				logs::error("Effects: missing or invalid spell at combination {}", index);
				return false;
			}
			legacySpells[index] = spell;
			shaders[index] = shader;
		}

		legacySpells_ = legacySpells;
		shaders_ = shaders;
		applied_.clear();
		invalidated_.clear();
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
		if (a_target.desired.combinationIndex >= shaders_.size()) {
			result.status = EffectStatus::Failed;
			result.error = EffectErrorCode::MissingForm;
			return result;
		}

		const bool wantShader = a_target.desired.enabled && a_target.desired.combinationIndex != 0;
		RE::TESEffectShader* desiredShader = wantShader ? shaders_[a_target.desired.combinationIndex] : nullptr;
		if (wantShader && !desiredShader) {
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
		auto* processLists = RE::ProcessLists::GetSingleton();
		if (!processLists) {
			result.status = EffectStatus::ApiUnavailable;
			result.error = EffectErrorCode::ApiFailure;
			return result;
		}

		for (std::size_t index = 1; index < legacySpells_.size(); ++index) {
			auto* spell = legacySpells_[index];
			if (spell && actor->HasSpell(spell) && !actor->RemoveSpell(spell) && actor->HasSpell(spell)) {
				result.status = EffectStatus::Failed;
				result.error = EffectErrorCode::ApiFailure;
				return result;
			}
		}

		const bool forceRebind = invalidated_.contains(a_target.actor);
		const auto actorHandle = actor->GetHandle();
		RE::ShaderReferenceEffect* onlyDesired = nullptr;
		std::size_t livePluginEffects = 0;
		std::size_t liveDesiredEffects = 0;
		bool changed = false;
		processLists->ForEachShaderEffect([&](RE::ShaderReferenceEffect* a_effect) {
			if (!a_effect || a_effect->finished || a_effect->target != actorHandle || !IsPluginShader(shaders_, a_effect->effectData)) {
				return RE::BSContainer::ForEachResult::kContinue;
			}

			++livePluginEffects;
			if (!wantShader || forceRebind || a_effect->effectData != desiredShader) {
				a_effect->finished = true;
				if (wantShader && !forceRebind && a_effect->effectData != desiredShader && onlyDesired) {
					onlyDesired->finished = true;
				}
				changed = true;
				return RE::BSContainer::ForEachResult::kContinue;
			}

			++liveDesiredEffects;
			if (!onlyDesired) {
				onlyDesired = a_effect;
				if (livePluginEffects > 1) {
					a_effect->finished = true;
					changed = true;
				}
			} else {
				onlyDesired->finished = true;
				a_effect->finished = true;
				changed = true;
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});

		const bool hasSingleDesired = wantShader && !forceRebind && livePluginEffects == 1 && liveDesiredEffects == 1;
		if (hasSingleDesired) {
			if (!actor->Is3DLoaded()) {
				result.status = EffectStatus::DeferredActorUnavailable;
				return result;
			}
			applied_[a_target.actor] = Applied{ a_target.desired.combinationIndex, a_target.revision };
			invalidated_.erase(a_target.actor);
			result.status = EffectStatus::NoChange;
			return result;
		}

		if (!wantShader) {
			changed = changed || (cached != applied_.end() && cached->second.combinationIndex != 0);
			applied_[a_target.actor] = Applied{ 0, a_target.revision };
			invalidated_.erase(a_target.actor);
			result.status = changed ? EffectStatus::Applied : EffectStatus::NoChange;
			return result;
		}

		if (!actor->Is3DLoaded()) {
			result.status = EffectStatus::DeferredActorUnavailable;
			return result;
		}

		if (!actor->ApplyEffectShader(desiredShader)) {
			result.status = EffectStatus::Failed;
			result.error = EffectErrorCode::ApiFailure;
			return result;
		}

		applied_[a_target.actor] = Applied{ a_target.desired.combinationIndex, a_target.revision };
		invalidated_.erase(a_target.actor);
		result.status = EffectStatus::Applied;
		return result;
	}

	void Effects::invalidate(ActorId a_actor)
	{
		invalidated_.insert(a_actor);
	}

	void Effects::clearSession(SessionEpoch)
	{
		applied_.clear();
		invalidated_.clear();
	}
}
