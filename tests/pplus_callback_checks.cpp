#include "integration/PPlusAdapter.h"

#include <RE/Skyrim.h>

#include <cstdlib>
#include <chrono>
#include <iostream>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

namespace
{
	using namespace slacs;
	using Callback = RE::BSScript::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>;

	void Check(bool a_condition, std::source_location a_location = std::source_location::current())
	{
		if (!a_condition) {
			std::cerr << "FAIL: PPlus callback check at line " << a_location.line() << '\n';
			std::exit(EXIT_FAILURE);
		}
	}

	void Check(bool a_condition, std::string_view a_message,
		std::source_location a_location = std::source_location::current())
	{
		if (!a_condition) {
			std::cerr << "FAIL: " << a_message << " at line " << a_location.line() << '\n';
			std::exit(EXIT_FAILURE);
		}
	}

	class FakeVM final : public RE::BSScript::IVirtualMachine
	{
	public:
		RE::BSScript::ObjectHandlePolicy* GetObjectHandlePolicy() override { return &policy; }

		bool DispatchMethodCall2(
			RE::VMHandle,
			const RE::BSFixedString&,
			const RE::BSFixedString&,
			RE::BSScript::IFunctionArguments* a_args,
			Callback& a_callback) override
		{
			delete a_args;
			callbacks.push_back(a_callback);
			return true;
		}

		Callback takeCallback()
		{
			Check(!callbacks.empty());
			auto result = std::move(callbacks.front());
			callbacks.erase(callbacks.begin());
			return result;
		}

		RE::BSScript::ObjectHandlePolicy policy;
		std::vector<Callback> callbacks;
	};

	struct Fixture
	{
		FakeVM vm;
		RE::SkyrimVM skyrimVM{ &vm };
		RE::TESQuest frameworkQuest;
		RE::TESDataHandler dataHandler{ &frameworkQuest };
		RE::Actor source;
		ActorRegistry registry;
		Settings settings;
		std::uint32_t reservations{};
		std::uint32_t releases{};
		std::uint32_t wakeCalls{};
		std::vector<PPlusCompleted> posted;
		PPlusAdapter adapter;

		Fixture()
		{
			RE::SkyrimVM::SetSingleton(&skyrimVM);
			RE::TESDataHandler::SetSingleton(&dataHandler);
			source.formID = 0x100;
			RE::TESForm::Register(&source);
			PPlusDependencies dependencies{};
			dependencies.registry = &registry;
			dependencies.settings = [this]() -> const Settings& { return settings; };
			dependencies.epoch = [] { return SessionEpoch{ 7 }; };
			dependencies.reserveResult = [this] { ++reservations; return true; };
			dependencies.releaseResult = [this] { ++releases; };
			dependencies.postResult = [this](PPlusCompleted&& a_result) {
				posted.push_back(std::move(a_result));
				return true;
			};
			dependencies.wake = [this] {
				++wakeCalls;
				throw std::runtime_error("test wake failure");
			};
			adapter.setDependencies(std::move(dependencies));
		}

		~Fixture()
		{
			adapter.shutdown();
			RE::SkyrimVM::SetSingleton(nullptr);
			RE::TESDataHandler::SetSingleton(nullptr);
		}

		[[nodiscard]] OrgasmQuery request() const
		{
			return OrgasmQuery{ 11, source.formID, 5, 88, 9, 7 };
		}

		void deliverThreadResult()
		{
			auto callback = vm.takeCallback();
			(*callback)(RE::BSScript::Variable::ObjectResult(0x1234));
		}

		void dispatchPositions()
		{
			deliverThreadResult();
			adapter.poll(std::chrono::steady_clock::now(), 8);
			Check(vm.callbacks.size() == 1);
		}
	};

	void CheckCallbackCopiesVariableAndDefersActorAccessToPoll()
	{
		RE::TestHooks::reset();
		Fixture fixture;
		Check(fixture.adapter.begin(fixture.request()) == QueryStart::Accepted);
		fixture.dispatchPositions();

		auto array = std::make_shared<RE::BSScript::Array>();
		array->push_back(RE::BSScript::Variable::ActorResult(&fixture.source));
		std::weak_ptr<RE::BSScript::Array> retainedArray = array;
		auto result = RE::BSScript::Variable::ArrayResult(array);
		array.reset();
		auto callback = fixture.vm.takeCallback();
		std::thread::id callbackThread;
		std::thread worker([callback = std::move(callback), result = std::move(result), &callbackThread]() mutable {
			callbackThread = std::this_thread::get_id();
			(*callback)(std::move(result));
		});
		worker.join();

		Check(fixture.wakeCalls == 2, "wake callback is invoked even when it throws");
		Check(RE::TestHooks::unpackCalls == 0, "VM callback must not unpack actor handles");
		Check(RE::TestHooks::actorHandleCalls == 0, "VM callback must not call Actor::GetHandle");
		Check(RE::TestHooks::formIdCalls == 0, "VM callback must not call Actor::GetFormID");
		Check(!retainedArray.expired(), "copied VM Variable must retain positions array until poll");

		fixture.adapter.poll(std::chrono::steady_clock::now(), 8);
		Check(RE::TestHooks::unpackCalls == 1);
		Check(RE::TestHooks::actorHandleCalls == 1);
		Check(RE::TestHooks::formIdCalls == 1);
		Check(RE::TestHooks::formIdCallsWithNiPointer == 1, "GetFormID runs while a NiPointer lease is live");
		{
			std::lock_guard lock(RE::TestHooks::mutex);
			Check(callbackThread != std::this_thread::get_id(), "callback is exercised from a background thread");
			Check(RE::TestHooks::unpackThread == std::this_thread::get_id(), "actor Unpack runs in poll thread");
			Check(RE::TestHooks::actorHandleThread == std::this_thread::get_id());
			Check(RE::TestHooks::formIdThread == std::this_thread::get_id());
		}
		Check(retainedArray.expired(), "positions array is released after poll resolves it");
		Check(RE::TestHooks::liveNiPointers == 0, "poll releases its NiPointer lease after resolving the actor");
		Check(fixture.posted.size() == 1);
		Check(std::holds_alternative<ResolvedOrgasm>(fixture.posted.front().outcome),
			"throwing wake must not turn a valid callback into failure");
	}

	void CheckUnavailableParticipants()
	{
		for (const bool a_deleted : { false, true }) {
			RE::TestHooks::reset();
			Fixture fixture;
			Check(fixture.adapter.begin(fixture.request()) == QueryStart::Accepted);
			fixture.dispatchPositions();

			if (a_deleted) {
				fixture.source.deleted = true;
			}
			auto array = std::make_shared<RE::BSScript::Array>();
			array->push_back(RE::BSScript::Variable::ActorResult(a_deleted ? &fixture.source : nullptr));
			auto callback = fixture.vm.takeCallback();
			(*callback)(RE::BSScript::Variable::ArrayResult(std::move(array)));
			fixture.adapter.poll(std::chrono::steady_clock::now(), 8);
			Check(fixture.posted.size() == 1);
			Check(std::holds_alternative<PPlusFailure>(fixture.posted.front().outcome));
			const auto& failure = std::get<PPlusFailure>(fixture.posted.front().outcome);
			Check(failure.code == PPlusFailureCode::ParticipantUnavailable,
				a_deleted ? "deleted participant reports unavailable" : "null participant reports unavailable");
		}
	}

	void CheckShutdownMakesLateCallbackNoOpAndReleasesOnce()
	{
		Fixture fixture;
		Check(fixture.adapter.begin(fixture.request()) == QueryStart::Accepted);
		Check(fixture.reservations == 1);
		auto callback = fixture.vm.takeCallback();
		fixture.adapter.shutdown();
		Check(fixture.adapter.pending() == 0);
		Check(fixture.releases == 1);
		(*callback)(RE::BSScript::Variable::ObjectResult(0x1234));
		Check(fixture.wakeCalls == 0);
		Check(fixture.posted.empty());
		Check(fixture.releases == 1, "late callback cannot release reservation twice");
	}
}

namespace slacs
{
	// The adapter's finalization path is not entered with an eligible target in
	// these tests; provide only the linked predicate required by this source.
	bool IsAdultNpc(const RE::Actor*) noexcept { return false; }
}

int main()
{
	CheckCallbackCopiesVariableAndDefersActorAccessToPoll();
	CheckUnavailableParticipants();
	CheckShutdownMakesLateCallbackNoOpAndReleasesOnce();
	std::cout << "SLACS2 NG PPlus callback checks passed\n";
}
