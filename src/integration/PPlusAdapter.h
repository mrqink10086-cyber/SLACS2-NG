#pragma once

#include "config/Settings.h"
#include "core/Types.h"
#include "runtime/ActorRegistry.h"
#include "runtime/Ingress.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace slacs
{
	enum class QueryStart : std::uint8_t
	{
		Accepted,
		RejectedNotRunning,
		RejectedUnavailable,
		RejectedCapacity,
	};

	struct OrgasmQuery
	{
		RequestId requestId{};
		FormId sourceFormId{};
		ActorId source{};
		Tid tid{};
		IngressSequence sequence{};
		SessionEpoch epoch{};
	};

	struct PPlusDependencies
	{
		ActorRegistry* registry{};
		std::function<const Settings&()> settings;
		std::function<SessionEpoch()> epoch;
		std::function<bool()> reserveResult;
		std::function<void()> releaseResult;
		std::function<bool(PPlusCompleted&&)> postResult;
		std::function<void()> wake;
	};

	class PPlusAdapter
	{
	public:
		PPlusAdapter();
		~PPlusAdapter();

		PPlusAdapter(const PPlusAdapter&) = delete;
		PPlusAdapter& operator=(const PPlusAdapter&) = delete;

		void setDependencies(PPlusDependencies a_dependencies);
		void shutdown();

		[[nodiscard]] QueryStart begin(const OrgasmQuery& a_request);
		bool cancel(RequestId a_requestId, SessionEpoch a_epoch) noexcept;
		void cancelTid(Tid a_tid, SessionEpoch a_epoch) noexcept;
		void cancelEpoch(SessionEpoch a_epoch) noexcept;
		std::uint32_t poll(SteadyTime a_now, std::size_t a_maxCallbacks);

		[[nodiscard]] std::size_t pending() const;

	private:
		struct RequestState;
		struct QueryCallback;
		struct CallbackGate;

		bool dispatchStep(const std::shared_ptr<RequestState>& a_state);
		void handleResult(
			const std::shared_ptr<RequestState>& a_state,
			int a_step,
			std::uint64_t a_token,
			const RE::BSScript::Variable& a_result);
		void fail(const std::shared_ptr<RequestState>& a_state, PPlusFailureCode a_code);
		void finalize(const std::shared_ptr<RequestState>& a_state);
		bool complete(const std::shared_ptr<RequestState>& a_state);
		void requestPump() noexcept;

		mutable std::mutex mutex_;
		std::shared_ptr<CallbackGate> callbackGate_;
		PPlusDependencies dependencies_;
		bool closed_{};
		std::unordered_map<RequestId, std::shared_ptr<RequestState>> requests_;
		std::uint64_t nextRequestId_{ 1 };
	};
}
