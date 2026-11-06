#pragma once

#include "menu/MenuTypes.h"
#include "runtime/Ingress.h"

namespace slacs
{
	class Menu
	{
	public:
		static bool available();
		static bool install();
		static void publish(MenuSnapshot a_snapshot);
		[[nodiscard]] static MenuSnapshot snapshot();
		static IngressResult submit(MenuAction a_action);
	};
}
