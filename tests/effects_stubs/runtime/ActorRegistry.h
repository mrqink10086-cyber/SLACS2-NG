#pragma once

#include "core/Types.h"
#include <memory>
#include <set>

namespace RE
{
	class SpellItem;
	struct Actor
	{
		std::set<SpellItem*> spells;
		bool failAdd{};
		bool failRemove{};
		bool HasSpell(SpellItem* a_spell) const { return spells.contains(a_spell); }
		bool AddSpell(SpellItem* a_spell) { return !failAdd && spells.insert(a_spell).second; }
		bool RemoveSpell(SpellItem* a_spell) { return !failRemove && spells.erase(a_spell) != 0; }
	};
}

namespace slacs
{
	struct ActorLease { std::shared_ptr<RE::Actor> actor; };
}
namespace logs
{
	template<class... T> void error(T&&...) {}
}
