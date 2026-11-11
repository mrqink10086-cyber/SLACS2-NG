#pragma once

#include "core/Types.h"
#include <memory>

namespace RE
{
	class Actor;
}

namespace slacs
{
	struct ActorLease
	{
		std::shared_ptr<RE::Actor> actor;
	};
}

namespace logs
{
	template<class... T>
	void error(T&&...)
	{}
}
