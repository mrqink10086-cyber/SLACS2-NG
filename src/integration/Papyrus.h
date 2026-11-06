#pragma once

#include <RE/Skyrim.h>

#include <cstdint>

namespace slacs::Native
{
	bool Register(RE::BSScript::IVirtualMachine* a_vm);
	void NotifyOrgasm(RE::StaticFunctionTag*, RE::TESForm* a_sourceActor, std::int32_t a_tid);
	void NotifySceneStart(RE::StaticFunctionTag*, std::int32_t a_tid, bool a_hasPlayer);
	void NotifySceneEnd(RE::StaticFunctionTag*, std::int32_t a_tid, bool a_hasPlayer);
}
