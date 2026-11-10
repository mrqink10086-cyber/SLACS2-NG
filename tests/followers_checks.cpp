#include "integration/Followers.h"

#include <cassert>

namespace
{
	void CheckFollowerClassification()
	{
		RE::TESRace npcRace;
		npcRace.keywords.emplace_back("ActorTypeNPC");
		RE::TESRace creatureRace;
		creatureRace.keywords.emplace_back("ActorTypeCreature");
		RE::TESRace dwarvenRace;
		dwarvenRace.keywords.emplace_back("ActorTypeDwarven");
		RE::TESRace animalRace;
		animalRace.keywords.emplace_back("ActorTypeAnimal");

		RE::Actor vanillaFollower;
		vanillaFollower.playerTeammate = true;
		vanillaFollower.race = &npcRace;
		assert(slacs::IsFollower(&vanillaFollower));

		RE::Actor modFollower = vanillaFollower;
		assert(slacs::IsFollower(&modFollower));

		RE::Actor untrackedNonFollower = vanillaFollower;
		untrackedNonFollower.playerTeammate = false;
		assert(!slacs::IsFollower(&untrackedNonFollower));

		RE::Actor commandedFollower = vanillaFollower;
		commandedFollower.commandedActor = true;
		assert(!slacs::IsFollower(&commandedFollower));

		RE::Actor childFollower = vanillaFollower;
		childFollower.child = true;
		assert(!slacs::IsFollower(&childFollower));

		for (auto* race : { &creatureRace, &dwarvenRace, &animalRace }) {
			RE::Actor creatureFollower = vanillaFollower;
			creatureFollower.race = race;
			assert(!slacs::IsFollower(&creatureFollower));
		}

		RE::Actor raceLessFollower = vanillaFollower;
		raceLessFollower.race = nullptr;
		assert(!slacs::IsFollower(&raceLessFollower));
		assert(!slacs::IsFollower(nullptr));
	}

	void CheckFollowerEnumeration()
	{
		RE::TESRace npcRace;
		npcRace.keywords.emplace_back("ActorTypeNPC");
		RE::Actor follower;
		follower.playerTeammate = true;
		follower.race = &npcRace;
		RE::Actor unloadedFollower = follower;
		unloadedFollower.loaded3D = false;
		RE::Actor deadFollower = follower;
		deadFollower.dead = true;
		RE::Actor nonFollower = follower;
		nonFollower.playerTeammate = false;

		RE::ProcessLists processLists;
		processLists.aliveActorList.emplace_back(&follower);
		processLists.aliveActorList.emplace_back(&unloadedFollower);
		processLists.aliveActorList.emplace_back(&deadFollower);
		processLists.aliveActorList.emplace_back(&nonFollower);
		RE::ProcessLists::SetSingleton(&processLists);

		const auto followers = slacs::GetFollowers();
		assert(followers.size() == 1);
		assert(followers.front().get().get() == &follower);

		RE::ProcessLists::SetSingleton(nullptr);
		assert(slacs::GetFollowers().empty());
	}
}

int main()
{
	CheckFollowerClassification();
	CheckFollowerEnumeration();
}
