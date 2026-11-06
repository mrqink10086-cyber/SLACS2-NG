#pragma once

#include <cstdint>

namespace slacs
{
	class RaceMenuApi
	{
	public:
		bool initialize();
		void reset();

		[[nodiscard]] bool available() const noexcept;
		[[nodiscard]] std::uint32_t version() const noexcept;
		[[nodiscard]] void* overlay() const noexcept;
		[[nodiscard]] void* overrideApi() const noexcept;

	private:
		void* overlay_{};
		void* override_{};
		std::uint32_t version_{};
		bool ready_{};
	};
}
