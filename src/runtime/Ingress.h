#pragma once

#include "config/Settings.h"
#include "core/Types.h"

#include <deque>
#include <functional>
#include <mutex>
#include <optional>

namespace slacs
{
	struct NativeOrgasm
	{
		FormId sourceFormId{};
		Tid tid{};
		IngressSequence sequence{};
		SessionEpoch epoch{};
	};

	enum class ScenePhase : std::uint8_t
	{
		Start,
		End,
	};

	struct SceneNotice
	{
		ScenePhase phase{};
		Tid tid{};
		bool hasPlayer{};
		SessionEpoch epoch{};
	};

	struct PPlusCompleted
	{
		RequestId requestId{};
		SessionEpoch epoch{};
		std::variant<ResolvedOrgasm, PPlusFailure> outcome;
	};

	struct EnvironmentObserved
	{
		EnvironmentSample sample;
		SessionEpoch epoch{};
	};

	struct ApplySettings
	{
		Settings settings;
		bool saveToDisk{};
		SessionEpoch epoch{};
	};

	struct ClearActor
	{
		ActorId actor{};
		SessionEpoch epoch{};
	};

	struct RefreshActor
	{
		ActorId actor{};
		SessionEpoch epoch{};
	};

	struct TestAdd
	{
		ActorId actor{};
		std::vector<Region> regions;
		std::uint8_t count{};
		TextureVariant variant{ TextureVariant::Normal };
		SessionEpoch epoch{};
	};

	struct TestClear
	{
		ActorId actor{};
		std::vector<Region> regions;
		SessionEpoch epoch{};
	};

	struct RefreshActorCandidates
	{
		SessionEpoch epoch{};
	};

	struct SelectCrosshairActor
	{
		SessionEpoch epoch{};
	};

	using MenuAction = std::variant<ApplySettings, ClearActor, RefreshActor, TestAdd, TestClear,
		RefreshActorCandidates, SelectCrosshairActor>;

	struct ActorVisualInvalidated
	{
		ActorId actor{};
		Revision revision{};
		SessionEpoch epoch{};
	};

	struct OverlayInstalled
	{
		FormId formId{};
		SessionEpoch epoch{};
	};

	enum class LoadPhase : std::uint8_t
	{
		Begin,
		Complete,
		Failed,
		NewGame,
		Revert,
	};

	struct LoadNotice
	{
		LoadPhase phase{};
		SessionEpoch epoch{};
	};

	using RuntimeCommand = std::variant<
		NativeOrgasm,
		SceneNotice,
		PPlusCompleted,
		EnvironmentObserved,
		DeadlineElapsed,
		MenuAction,
		ActorVisualInvalidated,
		OverlayInstalled,
		LoadNotice>;

	class Ingress
	{
	public:
		using WakeCallback = std::function<void()>;

		Ingress() = default;
		explicit Ingress(WakeCallback a_wake);
		Ingress(std::size_t a_capacity, WakeCallback a_wake);

		void setWakeCallback(WakeCallback a_wake);
		void setOpen(bool a_open);
		[[nodiscard]] bool isOpen() const;

		IngressResult post(RuntimeCommand a_command);
		IngressResult postOrgasm(FormId a_sourceFormId, Tid a_tid, SessionEpoch a_epoch);

		// Reserves space for terminal results whose producer is already accepted.
		// Reserved slots cannot be consumed by ordinary ingress traffic.
		[[nodiscard]] bool reserve(std::size_t a_count = 1);
		void release(std::size_t a_count = 1) noexcept;
		[[nodiscard]] std::size_t reserved() const;
		IngressResult postReserved(RuntimeCommand a_command);

		[[nodiscard]] std::optional<RuntimeCommand> pop();
		[[nodiscard]] std::size_t size() const;
		void clear();

	private:
		IngressResult enqueueLocked(RuntimeCommand a_command, bool a_consumeReservation = false);

		mutable std::mutex mutex_;
		std::deque<RuntimeCommand> queue_;
		std::size_t capacity_{ 2048 };
		std::size_t reserved_{};
		IngressSequence nextSequence_{ 1 };
		bool open_{};
		WakeCallback wake_;
	};
}
