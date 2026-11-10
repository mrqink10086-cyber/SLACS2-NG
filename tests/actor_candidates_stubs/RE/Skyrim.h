#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
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

	struct SEXES
	{
		enum SEX : std::uint32_t
		{
			kNone = static_cast<std::underlying_type_t<SEX>>(-1),
			kMale = 0,
			kFemale = 1,
			kTotal = 2
		};
	};
	using SEX = SEXES::SEX;

	class TESNPC
	{
	public:
		[[nodiscard]] SEX GetSex() const { return sex; }
		SEX sex{ SEX::kMale };
	};

	class TESObjectREFR
	{
	public:
		virtual ~TESObjectREFR() = default;

		template <class T>
		[[nodiscard]] T* As() noexcept
		{
			return dynamic_cast<T*>(this);
		}
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

	class Actor : public TESObjectREFR
	{
	public:
		[[nodiscard]] bool IsChild() const { return child; }
		[[nodiscard]] bool Is3DLoaded() const { return loaded3D; }
		[[nodiscard]] bool IsDead() const { return dead; }
		[[nodiscard]] TESRace* GetRace() const { return race; }
		[[nodiscard]] TESNPC* GetActorBase() const { return actorBase; }
		[[nodiscard]] std::uint32_t GetFormID() const { return formID; }

		bool child{};
		bool loaded3D{ true };
		bool dead{};
		TESRace* race{};
		TESNPC* actorBase{};
		std::uint32_t formID{};
	};

	class PlayerCharacter : public Actor
	{
	public:
		[[nodiscard]] static PlayerCharacter* GetSingleton() { return singleton_; }
		static void SetSingleton(PlayerCharacter* a_instance) { singleton_ = a_instance; }

	private:
		inline static PlayerCharacter* singleton_{};
	};

	class ActorHandle
	{
	public:
		explicit ActorHandle(Actor* a_actor = nullptr) : actor_(a_actor) {}
		[[nodiscard]] NiPointer<Actor> get() const { return NiPointer<Actor>(actor_); }
		[[nodiscard]] std::uintptr_t native_handle() const { return reinterpret_cast<std::uintptr_t>(actor_); }
		[[nodiscard]] friend bool operator==(const ActorHandle& a_lhs, const ActorHandle& a_rhs) { return a_lhs.actor_ == a_rhs.actor_; }

	private:
		Actor* actor_{};
	};

	class ObjectRefHandle
	{
	public:
		explicit ObjectRefHandle(TESObjectREFR* a_reference = nullptr) : reference_(a_reference) {}
		[[nodiscard]] NiPointer<TESObjectREFR> get() const { return NiPointer<TESObjectREFR>(reference_); }

	private:
		TESObjectREFR* reference_{};
	};

	class CrosshairPickData
	{
	public:
		[[nodiscard]] static CrosshairPickData* GetSingleton() { return singleton_; }
		static void SetSingleton(CrosshairPickData* a_instance) { singleton_ = a_instance; }
		[[nodiscard]] ObjectRefHandle GetActiveTarget() const { return target; }

		ObjectRefHandle target;

	private:
		inline static CrosshairPickData* singleton_{};
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
