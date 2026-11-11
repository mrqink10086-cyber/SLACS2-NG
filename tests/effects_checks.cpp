#include "integration/Effects.h"
#include <RE/Skyrim.h>

#include <array>
#include <iostream>
#include <string_view>

namespace
{
	using namespace slacs;
	int failures{};
	constexpr std::array<std::uint32_t, 27> kSpellLocalIds{
		0,
		0x002DB1, 0x002DB2, 0x002DB4, 0x002DB6, 0x002DB8, 0x002DBE, 0x002DBA, 0x002DBC,
		0x002DC0, 0x002DC4, 0x002DC6, 0x002DCC, 0x002DD4, 0x002DD6, 0x002DCE, 0x002DD8, 0x002DDA,
		0x002DC2, 0x002DC8, 0x002DCA, 0x002DD0, 0x002DDC, 0x002DDE, 0x002DD2, 0x002DE0, 0x002DE2,
	};

	void Expect(bool a_condition, std::string_view a_message)
	{
		if (!a_condition) {
			++failures;
			std::cerr << "FAIL: " << a_message << '\n';
		}
	}

	SpellTarget Target(ActorId a_actor, std::uint8_t a_index, Revision a_revision)
	{
		SpellTarget target{};
		target.actor = a_actor;
		target.desired.enabled = a_index != 0;
		target.desired.combinationIndex = a_index;
		target.revision = a_revision;
		return target;
	}

	RE::SpellItem* Spell(std::uint8_t a_index)
	{
		return RE::TESDataHandler::GetSingleton()->LookupForm<RE::SpellItem>(kSpellLocalIds[a_index], "SLACS_2_Plus_SCO.esp");
	}

	RE::TESEffectShader* Shader(std::uint8_t a_index)
	{
		return Spell(a_index)->effects.front()->baseEffect->data.effectShader;
	}

	std::shared_ptr<RE::Actor> Actor(ActorId a_id)
	{
		auto actor = std::make_shared<RE::Actor>();
		actor->handle.value = a_id;
		return actor;
	}

	std::size_t ActiveCount(ActorId a_id)
	{
		std::size_t count = 0;
		for (const auto& effect : RE::ProcessLists::GetSingleton()->effects) {
			if (!effect->finished && effect->target.value == a_id) {
				++count;
			}
		}
		return count;
	}

	void ResetEngine()
	{
		RE::ProcessLists::GetSingleton()->Reset();
	}

	void CheckAllCombinationShaders()
	{
		ResetEngine();
		Effects effects;
		Expect(effects.initialize(), "initialize all combination shader records");
		auto actor = Actor(1);
		ActorLease lease{ actor };
		for (std::uint8_t index = 1; index <= 26; ++index) {
			const auto result = effects.reconcile(lease, Target(1, index, index));
			Expect(result.status == EffectStatus::Applied, "each dense combination applies an EFSH");
			Expect(ActiveCount(1) == 1, "combination replacement leaves one live plugin EFSH");
			bool foundExpected = false;
			for (const auto& effect : RE::ProcessLists::GetSingleton()->effects) {
				if (!effect->finished && effect->target == actor->GetHandle() && effect->effectData == Shader(index)) {
					foundExpected = true;
				}
			}
			Expect(foundExpected, "dense index resolves to the corresponding SPEL MGEF EFSH record");
		}
		Expect(actor->addCalls == 0, "direct EFSH playback never calls AddSpell");
	}

	void CheckClearAndSwitch()
	{
		ResetEngine();
		Effects effects;
		Expect(effects.initialize(), "initialize clear and switch");
		auto actor = Actor(2);
		ActorLease lease{ actor };
		Expect(effects.reconcile(lease, Target(2, 1, 1)).status == EffectStatus::Applied, "first shader applies");
		Expect(effects.reconcile(lease, Target(2, 2, 2)).status == EffectStatus::Applied, "switch replaces the old shader");
		Expect(ActiveCount(2) == 1, "switch has one live effect");
		Expect(effects.reconcile(lease, Target(2, 0, 3)).status == EffectStatus::Applied, "zero clears plugin shaders");
		Expect(ActiveCount(2) == 0, "clear leaves no live plugin effect");
	}

	void CheckMissingEffectSameRevision()
	{
		ResetEngine();
		Effects effects;
		effects.initialize();
		auto actor = Actor(3);
		ActorLease lease{ actor };
		effects.reconcile(lease, Target(3, 1, 5));
		for (auto& effect : RE::ProcessLists::GetSingleton()->effects) {
			if (effect->target == actor->GetHandle() && effect->effectData == Shader(1)) {
				effect->finished = true;
			}
		}
		Expect(effects.reconcile(lease, Target(3, 1, 5)).status == EffectStatus::Applied,
			"same revision restores a missing runtime effect");
		Expect(ActiveCount(3) == 1, "missing effect recovery leaves one live instance");
	}

	void CheckDuplicateDeduplication()
	{
		ResetEngine();
		Effects effects;
		effects.initialize();
		auto actor = Actor(4);
		ActorLease lease{ actor };
		effects.reconcile(lease, Target(4, 1, 7));
		RE::ProcessLists::GetSingleton()->AddEffect(actor->GetHandle(), Shader(1));
		auto* first = RE::ProcessLists::GetSingleton()->effects.back().get();
		auto* duplicate = RE::ProcessLists::GetSingleton()->AddEffect(actor->GetHandle(), Shader(1));
		Expect(effects.reconcile(lease, Target(4, 1, 7)).status == EffectStatus::Applied,
			"duplicate plugin shaders trigger repair even at the same revision");
		Expect(first->finished && duplicate->finished, "deduplication finishes both prior exact matches");
		Expect(ActiveCount(4) == 1, "deduplication applies one replacement");
	}

	void CheckInvalidateAndSessionRecovery()
	{
		ResetEngine();
		Effects effects;
		effects.initialize();
		auto actor = Actor(5);
		ActorLease lease{ actor };
		effects.reconcile(lease, Target(5, 1, 9));
		RE::ProcessLists::GetSingleton()->AddEffect(actor->GetHandle(), Shader(1));
		auto* beforeInvalidate = RE::ProcessLists::GetSingleton()->effects.back().get();
		effects.invalidate(5);
		Expect(effects.reconcile(lease, Target(5, 1, 9)).status == EffectStatus::Applied,
			"invalidation forces stop and replay for equip or 3D rebuild");
		Expect(beforeInvalidate->finished && ActiveCount(5) == 1, "invalidation finishes old instance before replay");
		effects.clearSession(1);
		Expect(effects.reconcile(lease, Target(5, 1, 9)).status == EffectStatus::NoChange,
			"session cache clear scans and adopts the existing single instance");
		Expect(ActiveCount(5) == 1, "session recovery does not stack a duplicate");
	}

	void CheckForeignEffectsArePreserved()
	{
		ResetEngine();
		Effects effects;
		effects.initialize();
		auto actor = Actor(6);
		ActorLease lease{ actor };
		auto otherActor = Actor(7);
		auto* otherTarget = RE::ProcessLists::GetSingleton()->AddEffect(otherActor->GetHandle(), Shader(1));
		RE::TESEffectShader foreignShader{};
		auto* foreignShaderEffect = RE::ProcessLists::GetSingleton()->AddEffect(actor->GetHandle(), &foreignShader);
		effects.reconcile(lease, Target(6, 1, 1));
		Expect(!otherTarget->finished && !foreignShaderEffect->finished,
			"foreign target and non-plugin shader remain untouched");
		effects.reconcile(lease, Target(6, 0, 2));
		Expect(!otherTarget->finished && !foreignShaderEffect->finished,
			"clearing the desired state still preserves foreign effects");
	}

	void CheckApplyFailureAndStaleRevision()
	{
		ResetEngine();
		Effects effects;
		effects.initialize();
		auto actor = Actor(8);
		ActorLease lease{ actor };
		actor->failApply = true;
		Expect(effects.reconcile(lease, Target(8, 1, 10)).status == EffectStatus::Failed,
			"failed ApplyEffectShader does not advance applied revision");
		actor->failApply = false;
		Expect(effects.reconcile(lease, Target(8, 1, 10)).status == EffectStatus::Applied,
			"failed shader application retries at the same revision");
		Expect(effects.reconcile(lease, Target(8, 2, 9)).status == EffectStatus::StaleRevision,
			"older revision does not stop or replace a newer effect");
		Expect(ActiveCount(8) == 1, "stale revision preserves live effect");
	}

	void CheckOldSpellMigration()
	{
		ResetEngine();
		Effects effects;
		effects.initialize();
		auto actor = Actor(9);
		actor->spells.insert(Spell(1));
		actor->failRemove = true;
		ActorLease lease{ actor };
		const auto failed = effects.reconcile(lease, Target(9, 1, 3));
		Expect(failed.status == EffectStatus::Failed && actor->HasSpell(Spell(1)) && ActiveCount(9) == 0,
			"failed legacy spell removal leaves migration retryable and does not add a shader");
		actor->failRemove = false;
		Expect(effects.reconcile(lease, Target(9, 1, 3)).status == EffectStatus::Applied,
			"legacy spell migration retries and directly applies EFSH");
		Expect(!actor->HasSpell(Spell(1)) && actor->addCalls == 0 && actor->removeCalls > 0,
			"migration removes old plugin spell and never adds a spell");
	}

	void CheckRequires3D()
	{
		ResetEngine();
		Effects effects;
		effects.initialize();
		auto actor = Actor(10);
		actor->loaded3D = false;
		ActorLease lease{ actor };
		Expect(effects.reconcile(lease, Target(10, 1, 1)).status == EffectStatus::DeferredActorUnavailable,
			"nonzero shader waits for actor 3D");
		Expect(ActiveCount(10) == 0, "no effect is applied without 3D");
		actor->loaded3D = true;
		Expect(effects.reconcile(lease, Target(10, 1, 1)).status == EffectStatus::Applied,
			"deferred shader applies once 3D is available");
	}
}

int main()
{
	CheckAllCombinationShaders();
	CheckClearAndSwitch();
	CheckMissingEffectSameRevision();
	CheckDuplicateDeduplication();
	CheckInvalidateAndSessionRecovery();
	CheckForeignEffectsArePreserved();
	CheckApplyFailureAndStaleRevision();
	CheckOldSpellMigration();
	CheckRequires3D();
	if (failures == 0) std::cout << "SLACS2 NG effects checks passed\n";
	return failures == 0 ? 0 : 1;
}
