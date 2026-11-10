#include "integration/ActorCandidates.h"

#include <cstdlib>
#include <iostream>
#include <source_location>

namespace
{
	void Check(bool a_condition, std::source_location a_location = std::source_location::current())
	{
		if (!a_condition) {
			std::cerr << "FAIL: actor candidate check at line " << a_location.line() << '\n';
			std::exit(EXIT_FAILURE);
		}
	}

	void CheckLoadedCandidates()
	{
		RE::TESRace npcRace;
		npcRace.keywords.emplace_back("ActorTypeNPC");
		RE::TESRace creatureRace;
		creatureRace.keywords.emplace_back("ActorTypeCreature");
		RE::TESNPC femaleBase;
		femaleBase.sex = RE::SEX::kFemale;

		RE::PlayerCharacter player;
		player.formID = 0x14;
		RE::PlayerCharacter::SetSingleton(&player);
		RE::Actor femaleNPC;
		femaleNPC.race = &npcRace;
		femaleNPC.actorBase = &femaleBase;
		femaleNPC.formID = 0x100;
		RE::Actor maleNPC;
		maleNPC.race = &npcRace;
		maleNPC.formID = 0x101;
		RE::Actor creature;
		creature.race = &creatureRace;
		creature.formID = 0x102;
		RE::Actor deadNPC = maleNPC;
		deadNPC.dead = true;
		deadNPC.formID = 0x103;
		RE::Actor unloadedNPC = maleNPC;
		unloadedNPC.loaded3D = false;
		unloadedNPC.formID = 0x104;
		RE::Actor childNPC = maleNPC;
		childNPC.child = true;
		childNPC.formID = 0x105;
		RE::Actor raceLess;
		RE::Actor nonNPC;
		RE::TESRace otherRace;
		nonNPC.race = &otherRace;
		nonNPC.formID = 0x106;

		RE::ProcessLists processLists;
		processLists.aliveActorList.emplace_back(&femaleNPC);
		processLists.aliveActorList.emplace_back(&maleNPC);
		processLists.aliveActorList.emplace_back(&maleNPC);
		processLists.aliveActorList.emplace_back(&player);
		processLists.aliveActorList.emplace_back(&creature);
		processLists.aliveActorList.emplace_back(&deadNPC);
		processLists.aliveActorList.emplace_back(&unloadedNPC);
		processLists.aliveActorList.emplace_back(&childNPC);
		processLists.aliveActorList.emplace_back(&raceLess);
		processLists.aliveActorList.emplace_back(&nonNPC);
		RE::ProcessLists::SetSingleton(&processLists);

		const auto candidates = slacs::GetLoadedActorCandidates();
		Check(candidates.size() == 4);
		std::size_t players = 0;
		std::size_t females = 0;
		std::size_t loadedNPCs = 0;
		for (const auto& candidate : candidates) {
			auto actorPtr = candidate.actor.get();
			auto* const actor = actorPtr.get();
			Check(actor);
			if (actor == &player) {
				++players;
				Check(candidate.isPlayer);
				Check(!candidate.isLoadedNPC);
			} else {
				++loadedNPCs;
				Check(candidate.isLoadedNPC);
			}
			if (candidate.isFemale) {
				++females;
				Check(actor == &femaleNPC);
			}
			Check(!candidate.isCrosshair);
		}
		Check(players == 1);
		Check(females == 1);
		Check(loadedNPCs == 3);
		Check(slacs::IsAdultNpc(&nonNPC));
		Check(!slacs::IsAdultNpc(&childNPC));
		Check(!slacs::IsAdultNpc(&creature));
		Check(!slacs::IsAdultNpc(&raceLess));

		RE::ProcessLists::SetSingleton(nullptr);
		const auto onlyPlayer = slacs::GetLoadedActorCandidates();
		Check(onlyPlayer.size() == 1);
		Check(onlyPlayer.front().isPlayer);
		RE::PlayerCharacter::SetSingleton(nullptr);
	}

	void CheckCrosshairCandidate()
	{
		RE::TESRace npcRace;
		npcRace.keywords.emplace_back("ActorTypeNPC");
		RE::TESNPC femaleBase;
		femaleBase.sex = RE::SEX::kFemale;
		RE::Actor npc;
		npc.race = &npcRace;
		npc.actorBase = &femaleBase;
		RE::PlayerCharacter player;
		RE::PlayerCharacter::SetSingleton(&player);

		RE::CrosshairPickData crosshair;
		RE::CrosshairPickData::SetSingleton(&crosshair);
		Check(!slacs::GetCrosshairActorCandidate());

		crosshair.target = RE::ObjectRefHandle(&npc);
		const auto candidate = slacs::GetCrosshairActorCandidate();
		Check(candidate.has_value());
		auto actorPtr = candidate->actor.get();
		Check(actorPtr.get() == &npc);
		Check(candidate->isCrosshair);
		Check(!candidate->isPlayer);
		Check(!candidate->isLoadedNPC);
		Check(candidate->isFemale);

		crosshair.target = RE::ObjectRefHandle(&player);
		const auto playerCandidate = slacs::GetCrosshairActorCandidate();
		Check(playerCandidate.has_value());
		Check(playerCandidate->isPlayer);
		Check(playerCandidate->isCrosshair);

		RE::TESRace creatureRace;
		creatureRace.keywords.emplace_back("ActorTypeCreature");
		RE::Actor creature;
		creature.race = &creatureRace;
		crosshair.target = RE::ObjectRefHandle(&creature);
		Check(!slacs::GetCrosshairActorCandidate());

		npc.dead = true;
		crosshair.target = RE::ObjectRefHandle(&npc);
		Check(!slacs::GetCrosshairActorCandidate());

		RE::CrosshairPickData::SetSingleton(nullptr);
		RE::PlayerCharacter::SetSingleton(nullptr);
	}
}

int main()
{
	CheckLoadedCandidates();
	CheckCrosshairCandidate();
}
