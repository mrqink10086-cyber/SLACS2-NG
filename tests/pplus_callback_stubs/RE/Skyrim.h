#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace logs
{
	template <class... Args>
	void error(std::string_view, Args&&...) noexcept
	{}
}

namespace RE
{
	using FormID = std::uint32_t;
	using VMHandle = std::uint64_t;
	template <class T>
	using BSTSmartPointer = std::shared_ptr<T>;

	struct TestHooks
	{
		inline static std::atomic<std::uint32_t> unpackCalls{};
		inline static std::atomic<std::uint32_t> actorHandleCalls{};
		inline static std::atomic<std::uint32_t> formIdCalls{};
		inline static std::atomic<std::uint32_t> liveNiPointers{};
		inline static std::atomic<std::uint32_t> formIdCallsWithNiPointer{};
		inline static std::mutex mutex;
		inline static std::thread::id unpackThread;
		inline static std::thread::id actorHandleThread;
		inline static std::thread::id formIdThread;

		static void reset()
		{
			unpackCalls = 0;
			actorHandleCalls = 0;
			formIdCalls = 0;
			liveNiPointers = 0;
			formIdCallsWithNiPointer = 0;
			std::lock_guard lock(mutex);
			unpackThread = {};
			actorHandleThread = {};
			formIdThread = {};
		}
	};

	class TESForm
	{
	public:
		virtual ~TESForm() = default;
		[[nodiscard]] bool IsDeleted() const { return deleted; }
		[[nodiscard]] FormID GetFormID() const;

		template <class T>
		static T* LookupByID(FormID a_id);

		static void Register(TESForm* a_form)
		{
			forms[a_form->formID] = a_form;
		}

		FormID formID{};
		bool deleted{};

	private:
		inline static std::unordered_map<FormID, TESForm*> forms;
	};

	class TESRace
	{};

	template <class T>
	class NiPointer
	{
	public:
		NiPointer() = default;
		explicit NiPointer(T* a_value) : value_(a_value)
		{
			if (value_) {
				++TestHooks::liveNiPointers;
			}
		}
		NiPointer(const NiPointer& a_other) : value_(a_other.value_)
		{
			if (value_) {
				++TestHooks::liveNiPointers;
			}
		}
		NiPointer(NiPointer&& a_other) noexcept : value_(std::exchange(a_other.value_, nullptr)) {}
		NiPointer& operator=(const NiPointer& a_other)
		{
			if (value_ != a_other.value_) {
				if (value_) {
					--TestHooks::liveNiPointers;
				}
				value_ = a_other.value_;
				if (value_) {
					++TestHooks::liveNiPointers;
				}
			}
			return *this;
		}
		NiPointer& operator=(NiPointer&& a_other) noexcept
		{
			if (this != &a_other) {
				if (value_) {
					--TestHooks::liveNiPointers;
				}
				value_ = std::exchange(a_other.value_, nullptr);
			}
			return *this;
		}
		~NiPointer()
		{
			if (value_) {
				--TestHooks::liveNiPointers;
			}
		}
		[[nodiscard]] T* get() const { return value_; }
		[[nodiscard]] explicit operator bool() const { return value_ != nullptr; }
		[[nodiscard]] T* operator->() const { return value_; }
		void reset(T* a_value = nullptr)
		{
			if (value_) {
				--TestHooks::liveNiPointers;
			}
			value_ = a_value;
			if (value_) {
				++TestHooks::liveNiPointers;
			}
		}

	private:
		T* value_{};
	};

	class Actor;

	class ActorHandle
	{
	public:
		ActorHandle() = default;
		explicit ActorHandle(Actor* a_actor) : actor_(a_actor) {}
		[[nodiscard]] explicit operator bool() const { return actor_ != nullptr; }
		[[nodiscard]] NiPointer<Actor> get() const;

	private:
		Actor* actor_{};
	};

	class Actor : public TESForm
	{
	public:
		[[nodiscard]] bool IsDeleted() const { return deleted; }
		[[nodiscard]] FormID GetFormID() const;
		[[nodiscard]] ActorHandle GetHandle() const;
		[[nodiscard]] TESRace* GetRace() const { return race; }
		TESRace* race{};
	};

	inline FormID TESForm::GetFormID() const
	{
		return formID;
	}

	inline FormID Actor::GetFormID() const
	{
		++TestHooks::formIdCalls;
		if (TestHooks::liveNiPointers != 0) {
			++TestHooks::formIdCallsWithNiPointer;
		}
		std::lock_guard lock(TestHooks::mutex);
		TestHooks::formIdThread = std::this_thread::get_id();
		return formID;
	}

	inline ActorHandle Actor::GetHandle() const
	{
		++TestHooks::actorHandleCalls;
		std::lock_guard lock(TestHooks::mutex);
		TestHooks::actorHandleThread = std::this_thread::get_id();
		return ActorHandle(const_cast<Actor*>(this));
	}

	inline NiPointer<Actor> ActorHandle::get() const
	{
		return NiPointer<Actor>(actor_);
	}

	template <class T>
	T* TESForm::LookupByID(FormID a_id)
	{
		const auto found = forms.find(a_id);
		return found == forms.end() ? nullptr : dynamic_cast<T*>(found->second);
	}

	class TESQuest : public TESForm
	{};

	class BGSListForm : public TESForm
	{
	public:
		[[nodiscard]] bool HasForm(TESRace*) const { return false; }
	};

	class TESDataHandler
	{
	public:
		[[nodiscard]] static TESDataHandler* GetSingleton() { return singleton; }
		static void SetSingleton(TESDataHandler* a_handler) { singleton = a_handler; }

		template <class T>
		[[nodiscard]] T* LookupForm(FormID, const char*)
		{
			if constexpr (std::is_same_v<T, TESQuest>) {
				return quest;
			} else {
				return raceList;
			}
		}

		TESQuest* quest{};
		BGSListForm* raceList{};

	private:
		inline static TESDataHandler* singleton{};
	};

	enum class FormType : std::uint8_t
	{
		Quest,
	};

	class BSFixedString
	{
	public:
		BSFixedString() = default;
		BSFixedString(const char* a_value) : value_(a_value ? a_value : "") {}
		[[nodiscard]] const char* c_str() const { return value_.c_str(); }

	private:
		std::string value_;
	};

	namespace BSScript
	{
		class Array;
		class Object;
		class Variable
		{
		public:
			enum class Kind : std::uint8_t
			{
				None,
				Object,
				Array,
				Int,
				Bool,
				Actor,
			};

			Variable() = default;
			static Variable ObjectResult(VMHandle a_handle);
			static Variable ArrayResult(std::shared_ptr<Array> a_array);
			static Variable IntResult(std::int32_t a_value);
			static Variable BoolResult(bool a_value);
			static Variable ActorResult(Actor* a_actor);

			[[nodiscard]] bool IsObject() const { return kind_ == Kind::Object; }
			[[nodiscard]] bool IsArray() const { return kind_ == Kind::Array; }
			[[nodiscard]] bool IsInt() const { return kind_ == Kind::Int; }
			[[nodiscard]] bool IsBool() const { return kind_ == Kind::Bool; }
			[[nodiscard]] std::shared_ptr<Object> GetObject() const { return object_; }
			[[nodiscard]] std::shared_ptr<Array> GetArray() const { return array_; }
			[[nodiscard]] std::int32_t GetSInt() const { return int_; }
			[[nodiscard]] bool GetBool() const { return bool_; }

			template <class T>
			T Unpack() const
			{
				if constexpr (std::is_same_v<T, Actor*>) {
					++TestHooks::unpackCalls;
					std::lock_guard lock(TestHooks::mutex);
					TestHooks::unpackThread = std::this_thread::get_id();
					return kind_ == Kind::Actor ? actor_ : nullptr;
				} else {
					return T{};
				}
			}

		private:
			Kind kind_{ Kind::None };
			std::shared_ptr<Object> object_;
			std::shared_ptr<Array> array_;
			std::int32_t int_{};
			bool bool_{};
			Actor* actor_{};
		};

		class Array : public std::vector<Variable>
		{
		public:
			using std::vector<Variable>::vector;
		};

		class Object
		{
		public:
			explicit Object(VMHandle a_handle) : handle_(a_handle) {}
			[[nodiscard]] VMHandle GetHandle() const { return handle_; }

		private:
			VMHandle handle_{};
		};

		inline Variable Variable::ObjectResult(VMHandle a_handle)
		{
			Variable result;
			result.kind_ = Kind::Object;
			result.object_ = std::make_shared<Object>(a_handle);
			return result;
		}

		inline Variable Variable::ArrayResult(std::shared_ptr<Array> a_array)
		{
			Variable result;
			result.kind_ = Kind::Array;
			result.array_ = std::move(a_array);
			return result;
		}

		inline Variable Variable::IntResult(std::int32_t a_value)
		{
			Variable result;
			result.kind_ = Kind::Int;
			result.int_ = a_value;
			return result;
		}

		inline Variable Variable::BoolResult(bool a_value)
		{
			Variable result;
			result.kind_ = Kind::Bool;
			result.bool_ = a_value;
			return result;
		}

		inline Variable Variable::ActorResult(Actor* a_actor)
		{
			Variable result;
			result.kind_ = Kind::Actor;
			result.actor_ = a_actor;
			return result;
		}

		class IStackCallbackFunctor;
		template <class T>
		using BSTSmartPointer = ::RE::BSTSmartPointer<T>;

		class IStackCallbackFunctor
		{
		public:
			virtual ~IStackCallbackFunctor() = default;
			virtual void operator()(Variable a_result) = 0;
			[[nodiscard]] virtual bool CanSave() const = 0;
			virtual void SetObject(const BSTSmartPointer<Object>& a_object) = 0;
		};

		class IFunctionArguments
		{
		public:
			virtual ~IFunctionArguments() = default;
		};

		class ObjectHandlePolicy
		{
		public:
			[[nodiscard]] VMHandle GetHandleForObject(FormType, TESQuest*) const { return 77; }
		};

		class IVirtualMachine
		{
		public:
			virtual ~IVirtualMachine() = default;
			virtual ObjectHandlePolicy* GetObjectHandlePolicy() = 0;
			virtual bool DispatchMethodCall2(
				VMHandle,
				const BSFixedString&,
				const BSFixedString&,
				IFunctionArguments*,
				BSTSmartPointer<IStackCallbackFunctor>&) = 0;
		};
	}

	template <class... Args>
	BSScript::IFunctionArguments* MakeFunctionArguments(Args&&...)
	{
		return new BSScript::IFunctionArguments{};
	}

	class SkyrimVM
	{
	public:
		explicit SkyrimVM(BSScript::IVirtualMachine* a_vm) : impl(a_vm, [](BSScript::IVirtualMachine*) {}) {}
		[[nodiscard]] static SkyrimVM* GetSingleton() { return singleton; }
		static void SetSingleton(SkyrimVM* a_vm) { singleton = a_vm; }
		std::shared_ptr<BSScript::IVirtualMachine> impl;

	private:
		inline static SkyrimVM* singleton{};
	};
}
