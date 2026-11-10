#pragma once

#include <RE/Skyrim.h>

#include <optional>
#include <vector>

namespace slacs
{
	struct ActorCandidate
	{
		RE::ActorHandle actor;
		bool isPlayer{};
		bool isCrosshair{};
		bool isLoadedNPC{};
		bool isFemale{};
	};

	[[nodiscard]] bool IsAdultNpc(const RE::Actor* a_actor) noexcept;
	[[nodiscard]] std::vector<ActorCandidate> GetLoadedActorCandidates();
	[[nodiscard]] std::optional<ActorCandidate> GetCrosshairActorCandidate();
}
