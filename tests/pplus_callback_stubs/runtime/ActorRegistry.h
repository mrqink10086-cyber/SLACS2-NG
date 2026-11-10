#pragma once

#include "core/Types.h"

#include <RE/Skyrim.h>

#include <optional>

namespace slacs
{
	class ActorRegistry
	{
	public:
		std::optional<ActorId> bind(FormId a_formId)
		{
			return a_formId == kInvalidFormId ? std::nullopt : std::optional<ActorId>{ a_formId };
		}
	};
}
