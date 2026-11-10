#pragma once

#include "menu/MenuTypes.h"

#include <memory>

namespace slacs
{
	class MenuState
	{
	public:
		static void publish(MenuSnapshot a_snapshot);
		[[nodiscard]] static std::shared_ptr<const MenuSnapshot> snapshot();
	};
}
