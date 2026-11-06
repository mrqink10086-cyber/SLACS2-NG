#pragma once

#include "core/Types.h"

namespace slacs
{
	struct Settings;

	struct Core
	{
		static ChangeSet apply(CoreState& a_state, const CoreOp& a_op, const Settings& a_settings, const GameTime& a_now);
		static ChangeSet replaceState(CoreState& a_state, std::vector<ActorState> a_actors, const Settings& a_settings, const GameTime& a_now);
	};
}
