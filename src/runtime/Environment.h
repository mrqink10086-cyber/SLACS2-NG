#pragma once

#include "core/Types.h"

#include <unordered_map>

namespace RE
{
	class Actor;
}

namespace slacs
{
	// Samples the world state for one actor during a verified game execution
	// phase. The returned sample is a value object; observedRainHours covers
	// only the interval that is known to have been spent outdoors in rain.
	class Environment
	{
	public:
		[[nodiscard]] std::optional<EnvironmentSample> sample(ActorId a_actor, RE::Actor& a_actorRef, const GameTime& a_now);

		void reset(SessionEpoch a_epoch);
		void invalidate(ActorId a_actor);
		void invalidateAll();

	private:
		struct Observation
		{
			double calendarHours{};
			double activeSeconds{};
			SteadyTime wall{};
			FormId cellFormId{};
			bool exposedRain{};
		};

		SessionEpoch epoch_{};
		std::unordered_map<ActorId, Observation> observations_;
	};
}
