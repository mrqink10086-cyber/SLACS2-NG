#pragma once

#include "menu/MenuTypes.h"

namespace slacs
{
	class MenuState
	{
	public:
		static void publish(MenuSnapshot a_snapshot);
		[[nodiscard]] static MenuSnapshot snapshot();
	};
}
