#include "integration/ActorCandidates.h"

#include <algorithm>
#include <utility>

namespace slacs
{
	namespace
	{
		[[nodiscard]] bool IsSelectableActor(RE::Actor* a_actor, RE::Actor* a_player) noexcept
		{
			if (!a_actor || !a_actor->Is3DLoaded() || a_actor->IsDead() || a_actor->IsChild()) {
				return false;
			}

			if (a_actor == a_player) {
				return true;
			}

			return IsAdultNpc(a_actor);
		}

		[[nodiscard]] ActorCandidate MakeCandidate(RE::Actor* a_actor, bool a_isPlayer, bool a_isCrosshair, bool a_isLoadedNPC)
		{
			const auto actorBase = a_actor->GetActorBase();
			return {
				RE::ActorHandle(a_actor),
				a_isPlayer,
				a_isCrosshair,
				a_isLoadedNPC,
				actorBase && actorBase->GetSex() == RE::SEX::kFemale
			};
		}

		void MergeSources(ActorCandidate& a_target, const ActorCandidate& a_source) noexcept
		{
			a_target.isPlayer = a_target.isPlayer || a_source.isPlayer;
			a_target.isCrosshair = a_target.isCrosshair || a_source.isCrosshair;
			a_target.isLoadedNPC = a_target.isLoadedNPC || a_source.isLoadedNPC;
			a_target.isFemale = a_target.isFemale || a_source.isFemale;
		}
	}

	bool IsAdultNpc(const RE::Actor* a_actor) noexcept
	{
		if (!a_actor || a_actor->IsChild()) {
			return false;
		}
		const auto race = a_actor->GetRace();
		if (!race) {
			return false;
		}
		return race->HasKeywordString("ActorTypeNPC") ||
			(!race->HasKeywordString("ActorTypeCreature") &&
			 !race->HasKeywordString("ActorTypeDwarven") &&
			 !race->HasKeywordString("ActorTypeAnimal"));
	}

	std::vector<ActorCandidate> GetLoadedActorCandidates()
	{
		auto* const player = RE::PlayerCharacter::GetSingleton();
		const auto processLists = RE::ProcessLists::GetSingleton();

		std::vector<ActorCandidate> candidates;
		candidates.reserve((processLists ? processLists->aliveActorList.size() : 0) + 1);

		if (IsSelectableActor(player, player)) {
			candidates.push_back(MakeCandidate(player, true, false, false));
		}

		if (processLists) {
			for (const auto& handle : processLists->aliveActorList) {
				auto actorPtr = handle.get();
				auto* const actor = actorPtr.get();
				if (IsSelectableActor(actor, player)) {
					const bool isPlayer = actor == player;
					candidates.push_back(MakeCandidate(actor, isPlayer, false, !isPlayer));
				}
			}
		}

		std::sort(candidates.begin(), candidates.end(), [](const ActorCandidate& a_lhs, const ActorCandidate& a_rhs) {
			return a_lhs.actor.native_handle() < a_rhs.actor.native_handle();
		});

		auto uniqueEnd = candidates.begin();
		for (auto current = candidates.begin(); current != candidates.end(); ++current) {
			if (uniqueEnd != candidates.begin() && (uniqueEnd - 1)->actor == current->actor) {
				MergeSources(*(uniqueEnd - 1), *current);
			} else {
				if (uniqueEnd != current) {
					*uniqueEnd = std::move(*current);
				}
				++uniqueEnd;
			}
		}
		candidates.erase(uniqueEnd, candidates.end());
		return candidates;
	}

	std::optional<ActorCandidate> GetCrosshairActorCandidate()
	{
		const auto crosshair = RE::CrosshairPickData::GetSingleton();
		if (!crosshair) {
			return std::nullopt;
		}

		auto targetPtr = crosshair->GetActiveTarget().get();
		auto* const reference = targetPtr.get();
		auto* const actor = reference ? reference->As<RE::Actor>() : nullptr;
		auto* const player = RE::PlayerCharacter::GetSingleton();
		if (!IsSelectableActor(actor, player)) {
			return std::nullopt;
		}

		const bool isPlayer = actor == player;
		return MakeCandidate(actor, isPlayer, true, false);
	}
}
