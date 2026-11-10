#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace RE
{
	class TESRace
	{
	public:
		std::vector<std::string> keywords;

		[[nodiscard]] bool HasKeywordString(std::string_view a_editorID) const
		{
			return std::ranges::any_of(keywords, [a_editorID](const auto& keyword) { return keyword == a_editorID; });
		}
	};

	class Actor
	{
	public:
		[[nodiscard]] bool IsPlayerTeammate() const { return playerTeammate; }
		[[nodiscard]] bool IsCommandedActor() const { return commandedActor; }
		[[nodiscard]] bool IsChild() const { return child; }
		[[nodiscard]] bool Is3DLoaded() const { return loaded3D; }
		[[nodiscard]] bool IsDead() const { return dead; }
		[[nodiscard]] TESRace* GetRace() const { return race; }

		bool playerTeammate{};
		bool commandedActor{};
		bool child{};
		bool loaded3D{ true };
		bool dead{};
		TESRace* race{};
	};

	template <class T>
	class NiPointer
	{
	public:
		explicit NiPointer(T* a_value = nullptr) : value_(a_value) {}
		[[nodiscard]] T* get() const { return value_; }

	private:
		T* value_{};
	};

	class ActorHandle
	{
	public:
		explicit ActorHandle(Actor* a_actor = nullptr) : actor_(a_actor) {}
		[[nodiscard]] NiPointer<Actor> get() const { return NiPointer<Actor>(actor_); }

	private:
		Actor* actor_{};
	};

	class ProcessLists
	{
	public:
		[[nodiscard]] static ProcessLists* GetSingleton() { return singleton_; }
		static void SetSingleton(ProcessLists* a_instance) { singleton_ = a_instance; }

		std::vector<ActorHandle> aliveActorList;

	private:
		inline static ProcessLists* singleton_{};
	};
}
