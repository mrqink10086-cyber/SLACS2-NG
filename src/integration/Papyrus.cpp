#include "integration/Papyrus.h"

#include "runtime/Runtime.h"

#include <atomic>
#include <chrono>
#include <exception>

namespace slacs::Native
{
	namespace
	{
		std::atomic<std::int64_t> g_lastEmptyFormLogMs{ 0 };

		template <class F>
		void Guarded(const char* a_operation, F&& a_function) noexcept
		{
			try {
				a_function();
			} catch (const std::exception& e) {
				logs::error("Papyrus {} exception: {}", a_operation, e.what());
			} catch (...) {
				logs::error("Papyrus {} unknown exception", a_operation);
			}
		}

		void LogEmptyForm()
		{
			const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
								 std::chrono::steady_clock::now().time_since_epoch())
								 .count();
			auto last = g_lastEmptyFormLogMs.load(std::memory_order_relaxed);
			if (now - last >= 5000 &&
				g_lastEmptyFormLogMs.compare_exchange_strong(last, now, std::memory_order_relaxed)) {
				logs::warn("SLACS_Main.NotifyOrgasm ignored empty source form");
			}
		}
	}

	bool Register(RE::BSScript::IVirtualMachine* a_vm)
	{
		if (!a_vm) {
			return false;
		}
		a_vm->RegisterFunction("NotifyOrgasm", "SLACS_Main", NotifyOrgasm);
		a_vm->RegisterFunction("NotifySceneStart", "SLACS_Main", NotifySceneStart);
		a_vm->RegisterFunction("NotifySceneEnd", "SLACS_Main", NotifySceneEnd);
		logs::info("Papyrus natives registered for SLACS_Main");
		return true;
	}

	void NotifyOrgasm(RE::StaticFunctionTag*, RE::TESForm* a_sourceActor, std::int32_t a_tid)
	{
		Guarded("NotifyOrgasm", [&] {
			if (!a_sourceActor) {
				LogEmptyForm();
				return;
			}
			auto& runtime = Runtime::instance();
			const auto result = runtime.ingress().postOrgasm(
				a_sourceActor->GetFormID(),
				a_tid,
				runtime.epoch());
			if (result.status != IngressStatus::Accepted) {
				logs::warn("SLACS_Main.NotifyOrgasm rejected: status={}", static_cast<int>(result.status));
			}
		});
	}

	void NotifySceneStart(RE::StaticFunctionTag*, std::int32_t a_tid, bool a_hasPlayer)
	{
		Guarded("NotifySceneStart", [&] {
			auto& runtime = Runtime::instance();
			const auto result = runtime.ingress().post(SceneNotice{ ScenePhase::Start, a_tid, a_hasPlayer, runtime.epoch() });
			if (result.status != IngressStatus::Accepted) {
				logs::warn("SLACS_Main.NotifySceneStart rejected: status={}", static_cast<int>(result.status));
			}
		});
	}

	void NotifySceneEnd(RE::StaticFunctionTag*, std::int32_t a_tid, bool a_hasPlayer)
	{
		Guarded("NotifySceneEnd", [&] {
			auto& runtime = Runtime::instance();
			const auto result = runtime.ingress().post(SceneNotice{ ScenePhase::End, a_tid, a_hasPlayer, runtime.epoch() });
			if (result.status != IngressStatus::Accepted) {
				logs::warn("SLACS_Main.NotifySceneEnd rejected: status={}", static_cast<int>(result.status));
			}
		});
	}
}
