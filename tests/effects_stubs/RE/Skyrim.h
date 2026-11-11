#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace RE
{
	namespace BSContainer
	{
		enum class ForEachResult
		{
			kContinue,
			kStop,
		};
	}

	struct ObjectRefHandle
	{
		std::uint64_t value{};
		friend bool operator==(const ObjectRefHandle&, const ObjectRefHandle&) = default;
	};

	class TESEffectShader
	{
	public:
		std::uint32_t localFormId{};
	};

	struct EffectSetting
	{
		struct Data
		{
			TESEffectShader* effectShader{};
		} data;
	};

	struct Effect
	{
		EffectSetting* baseEffect{};
	};

	class SpellItem
	{
	public:
		SpellItem() : setting{ { &shader } }, effect{ &setting }, effects{ &effect } {}

		TESEffectShader shader{};
		EffectSetting setting{};
		Effect effect{};
		std::vector<Effect*> effects;
	};

	class ShaderReferenceEffect
	{
	public:
		ObjectRefHandle target{};
		TESEffectShader* effectData{};
		bool finished{};
	};

	class ProcessLists;

	class TESObjectREFR
	{
	public:
		virtual ~TESObjectREFR() = default;
		virtual ObjectRefHandle GetHandle() const { return handle; }
		bool Is3DLoaded() const { return loaded3D; }
		ShaderReferenceEffect* ApplyEffectShader(TESEffectShader* a_shader, float = -1.0f, TESObjectREFR* = nullptr, bool = false, bool = false, void* = nullptr, bool = false);

		ObjectRefHandle handle{};
		bool loaded3D{ true };
		bool failApply{};
	};

	class Actor : public TESObjectREFR
	{
	public:
		bool HasSpell(SpellItem* a_spell) const { return spells.contains(a_spell); }
		bool AddSpell(SpellItem* a_spell)
		{
			++addCalls;
			return !failAdd && spells.insert(a_spell).second;
		}
		bool RemoveSpell(SpellItem* a_spell)
		{
			++removeCalls;
			return !failRemove && spells.erase(a_spell) != 0;
		}

		std::set<SpellItem*> spells;
		bool failAdd{};
		bool failRemove{};
		std::uint32_t addCalls{};
		std::uint32_t removeCalls{};
	};

	class ProcessLists
	{
	public:
		static ProcessLists* GetSingleton()
		{
			static ProcessLists value;
			return &value;
		}

		void ForEachShaderEffect(const std::function<BSContainer::ForEachResult(ShaderReferenceEffect*)>& a_callback)
		{
			++enumerationCount;
			for (const auto& effect : effects) {
				if (a_callback(effect.get()) == BSContainer::ForEachResult::kStop) {
					break;
				}
			}
		}
		ShaderReferenceEffect* AddEffect(ObjectRefHandle a_target, TESEffectShader* a_shader)
		{
			auto effect = std::make_unique<ShaderReferenceEffect>();
			effect->target = a_target;
			effect->effectData = a_shader;
			auto* result = effect.get();
			effects.push_back(std::move(effect));
			return result;
		}
		void Reset() { effects.clear(); enumerationCount = 0; }
		std::vector<std::unique_ptr<ShaderReferenceEffect>> effects;
		std::uint32_t enumerationCount{};
	};

	class TESDataHandler
	{
	public:
		static TESDataHandler* GetSingleton()
		{
			static TESDataHandler value;
			return &value;
		}
		template<class T>
		T* LookupForm(std::uint32_t a_id, const char*)
		{
			auto& spell = spells[a_id];
			spell.shader.localFormId = a_id;
			return reinterpret_cast<T*>(&spell);
		}
		std::map<std::uint32_t, SpellItem> spells;
	};

	inline ShaderReferenceEffect* TESObjectREFR::ApplyEffectShader(TESEffectShader* a_shader, float, TESObjectREFR*, bool, bool, void*, bool)
	{
		if (failApply || !a_shader) {
			return nullptr;
		}
		return ProcessLists::GetSingleton()->AddEffect(GetHandle(), a_shader);
	}
}
