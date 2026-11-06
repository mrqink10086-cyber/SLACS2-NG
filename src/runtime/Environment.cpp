#include "runtime/Environment.h"

#include <RE/Skyrim.h>

#include <cmath>
#include <limits>

namespace slacs
{
	namespace
	{
		// Sampling normally happens once per second. A larger calendar jump is
		// sleep/wait/fast travel, and a larger wall gap is a pause, loading
		// screen or a stalled task queue. Neither interval is a continuous
		// observation, so it contributes no rain time.
		constexpr double kMaxObservationGapHours = 0.5;
		constexpr double kMaxObservationGapActiveSeconds = 5.0;
		constexpr auto kMaxObservationGapWall = std::chrono::seconds{ 5 };

		bool GamePaused()
		{
			auto* timer = RE::BSTimer::GetSingleton();
			return !timer || timer->pauseCount != 0;
		}
	}

	std::optional<EnvironmentSample> Environment::sample(ActorId a_actor, RE::Actor& a_actorRef, const GameTime& a_now)
	{
		if (a_actor == kInvalidActorId || GamePaused()) {
			invalidateAll();
			return std::nullopt;
		}

		auto* cell = a_actorRef.GetParentCell();
		if (!cell) {
			// An unloaded actor has no continuous observation interval.
			invalidate(a_actor);
			return std::nullopt;
		}

		auto* sky = RE::Sky::GetSingleton();
		const bool raining = sky && sky->IsRaining();
		const bool snowing = sky && sky->IsSnowing();
		const bool interior = cell->IsInteriorCell();
		auto* actorState = a_actorRef.AsActorState();
		const bool swimming = actorState && actorState->IsSwimming();
		const auto cellFormId = cell->GetFormID();
		const bool exposedRain = raining && !snowing && !interior;
		const auto wall = std::chrono::steady_clock::now();

		double observedRainHours = 0.0;
		const auto found = observations_.find(a_actor);
		if (found != observations_.end()) {
			const auto& previous = found->second;
			const double elapsedHours = a_now.calendarHours - previous.calendarHours;
			const double elapsedActive = a_now.activeSeconds - previous.activeSeconds;
			const double elapsedWall = std::chrono::duration<double>(wall - previous.wall).count();
			const bool continuous = previous.cellFormId == cellFormId &&
				std::isfinite(elapsedHours) && elapsedHours >= 0.0 &&
				elapsedHours <= kMaxObservationGapHours &&
				std::isfinite(elapsedActive) && elapsedActive >= 0.0 &&
				elapsedActive <= kMaxObservationGapActiveSeconds &&
				elapsedWall >= 0.0 && elapsedWall <= kMaxObservationGapWall.count();
			// Both endpoints must be valid outdoor rain observations. If the
			// actor just went indoors, it started snowing, or the sky cleared
			// between two samples, the interval is not a continuous rain
			// exposure and contributes nothing.
			if (continuous && previous.exposedRain && exposedRain) {
				observedRainHours = elapsedHours;
			}
		}

		observations_[a_actor] = Observation{
			a_now.calendarHours,
			a_now.activeSeconds,
			wall,
			cellFormId,
			exposedRain,
		};

		EnvironmentSample sample{};
		sample.actor = a_actor;
		sample.interior = interior;
		sample.raining = raining;
		sample.snowing = snowing;
		sample.swimming = swimming;
		sample.observedRainHours = std::isfinite(observedRainHours) ? observedRainHours : 0.0;
		sample.at = a_now;
		return sample;
	}

	void Environment::reset(SessionEpoch a_epoch)
	{
		epoch_ = a_epoch;
		observations_.clear();
	}

	void Environment::invalidate(ActorId a_actor)
	{
		observations_.erase(a_actor);
	}

	void Environment::invalidateAll()
	{
		observations_.clear();
	}
}
