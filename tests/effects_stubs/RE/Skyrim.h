#pragma once

#include "runtime/ActorRegistry.h"
#include <map>
#include <vector>

namespace RE
{
	struct EffectShader {};
	struct EffectSetting { struct Data { EffectShader* effectShader; } data; };
	struct Effect { EffectSetting* baseEffect; };
	inline EffectShader fakeShader;
	inline EffectSetting fakeSetting{ { &fakeShader } };
	inline Effect fakeEffect{ &fakeSetting };
	class SpellItem
	{
	public:
		std::vector<Effect*> effects{ &fakeEffect };
	};
	class TESDataHandler
	{
	public:
		std::map<std::uint32_t, SpellItem> spells;
		static TESDataHandler* GetSingleton() { static TESDataHandler value; return &value; }
		template<class T> T* LookupForm(std::uint32_t a_id, const char*) { return &spells[a_id]; }
	};
}
