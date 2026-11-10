#include "integration/Followers.h"

namespace slacs
{
	bool IsFollower(RE::Actor* a_actor) noexcept
	{
		if (!a_actor || !a_actor->IsPlayerTeammate() || a_actor->IsCommandedActor() || a_actor->IsChild()) {
			return false;
		}

		const auto race = a_actor->GetRace();
		if (!race) {
			return false;
		}
		if (race->HasKeywordString("ActorTypeNPC")) {
			return true;
		}

		return !race->HasKeywordString("ActorTypeCreature") &&
		       !race->HasKeywordString("ActorTypeDwarven") &&
		       !race->HasKeywordString("ActorTypeAnimal");
	}

	std::vector<RE::ActorHandle> GetFollowers()
	{
		std::vector<RE::ActorHandle> followers;
		const auto processLists = RE::ProcessLists::GetSingleton();
		if (!processLists) {
			return followers;
		}

		for (const auto& handle : processLists->aliveActorList) {
			auto actorPtr = handle.get();
			auto* actor = actorPtr.get();
			if (actor && actor->Is3DLoaded() && !actor->IsDead() && IsFollower(actor)) {
				followers.push_back(handle);
			}
		}
		return followers;
	}
}
