#include "menu/MenuState.h"

#include <atomic>
#include <memory>
#include <utility>

namespace slacs
{
	namespace
	{
		std::atomic<std::shared_ptr<const MenuSnapshot>>& Store()
		{
			static std::atomic<std::shared_ptr<const MenuSnapshot>> store{
				std::make_shared<const MenuSnapshot>()
			};
			return store;
		}
	}

	void MenuState::publish(MenuSnapshot a_snapshot)
	{
		Store().store(
			std::make_shared<const MenuSnapshot>(std::move(a_snapshot)),
			std::memory_order_release);
	}

	MenuSnapshot MenuState::snapshot()
	{
		const auto current = Store().load(std::memory_order_acquire);
		return current ? *current : MenuSnapshot{};
	}
}
