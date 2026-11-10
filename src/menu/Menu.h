#pragma once

#include "menu/MenuTypes.h"
#include "runtime/Ingress.h"

#include <memory>

namespace slacs
{
	class Menu
	{
	public:
		static bool available();
		static bool install();
		static void publish(MenuSnapshot a_snapshot);
		[[nodiscard]] static std::shared_ptr<const MenuSnapshot> snapshot();
		static IngressResult submit(MenuAction a_action);
	};
}
