#pragma once

#include <RE/Skyrim.h>

#include <vector>

namespace slacs
{
	[[nodiscard]] bool IsFollower(RE::Actor* a_actor) noexcept;
	[[nodiscard]] std::vector<RE::ActorHandle> GetFollowers();
}
