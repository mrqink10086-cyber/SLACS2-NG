#pragma once

#include "core/Types.h"
#include <memory>

namespace RE
{
	enum class SEX { kMale, kFemale };
	struct TESNPC { SEX GetSex() const { return SEX::kFemale; } };
	struct TESObjectREFR { std::uint32_t GetFormID() const { return 0x14; } };
	struct Actor : TESObjectREFR
	{
		TESNPC base;
		bool Is3DLoaded() const { return true; }
		TESNPC* GetActorBase() { return &base; }
	};
	struct NiAVObject {};
	struct BGSTextureSet {};
}

using BGSTextureSet = RE::BGSTextureSet;
namespace slacs
{
	struct ActorLease
	{
		std::shared_ptr<RE::Actor> actor;
		std::shared_ptr<RE::NiAVObject> root;
	};
}
namespace logs
{
	template<class... T> void warn(T&&...) {}
	template<class... T> void info(T&&...) {}
	template<class... T> void debug(T&&...) {}
}
