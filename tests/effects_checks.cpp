#include "integration/Effects.h"
#include <RE/Skyrim.h>

#include <iostream>
#include <string_view>

namespace
{
	using namespace slacs;
	int failures{};
	void Expect(bool a_condition, std::string_view a_message)
	{
		if (!a_condition) {
			++failures;
			std::cerr << "FAIL: " << a_message << '\n';
		}
	}

	SpellTarget Target(std::uint8_t a_index, Revision a_revision)
	{
		SpellTarget target{};
		target.actor = 1;
		target.desired.enabled = a_index != 0;
		target.desired.combinationIndex = a_index;
		target.revision = a_revision;
		return target;
	}

	void CheckFailedAdd()
	{
		Effects effects;
		Expect(effects.initialize(), "initialize effects");
		ActorLease lease{ std::make_shared<RE::Actor>() };
		lease.actor->failAdd = true;
		const auto failed = effects.reconcile(lease, Target(1, 2));
		Expect(failed.status == EffectStatus::Failed && failed.error == EffectErrorCode::ApiFailure,
			"failed add must remain dirty");
		lease.actor->failAdd = false;
		const auto retry = effects.reconcile(lease, Target(1, 1));
		Expect(retry.status == EffectStatus::Applied && lease.actor->spells.size() == 1,
			"failed add does not cache a successful revision");
		Expect(effects.reconcile(lease, Target(1, 1)).status == EffectStatus::NoChange,
			"already correct projection needs no change");
	}

	void CheckFailedRemove()
	{
		Effects effects;
		Expect(effects.initialize(), "initialize effects");
		ActorLease lease{ std::make_shared<RE::Actor>() };
		effects.reconcile(lease, Target(1, 1));
		lease.actor->failRemove = true;
		const auto failed = effects.reconcile(lease, Target(2, 2));
		Expect(failed.status == EffectStatus::Failed && failed.error == EffectErrorCode::ApiFailure,
			"failed replacement must remain dirty");
		Expect(lease.actor->spells.size() == 1, "failed removal does not stack another owned effect");
		lease.actor->failRemove = false;
		Expect(effects.reconcile(lease, Target(2, 2)).status == EffectStatus::Applied && lease.actor->spells.size() == 1,
			"replacement converges after removal recovers");
		lease.actor->failRemove = true;
		Expect(effects.reconcile(lease, Target(0, 3)).status == EffectStatus::Failed,
			"failed clear must remain dirty");
		lease.actor->failRemove = false;
		Expect(effects.reconcile(lease, Target(0, 3)).status == EffectStatus::Applied && lease.actor->spells.empty(),
			"clear converges after removal recovers");
	}
}

int main()
{
	CheckFailedAdd();
	CheckFailedRemove();
	if (failures == 0) std::cout << "SLACS2 NG effects checks passed\n";
	return failures == 0 ? 0 : 1;
}
