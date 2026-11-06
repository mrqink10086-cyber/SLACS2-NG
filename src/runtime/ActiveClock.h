#pragma once

#include "core/Types.h"

#include <cstdint>

namespace slacs
{
	// Converts the engine's 32-bit runTimeMS into a monotonic active-game clock.
	// Samples taken while the game or menu framework is frozen are ignored so
	// timed clears cannot elapse behind a paused screen. Unsigned subtraction
	// makes the 32-bit wrap harmless.
	class ActiveClock
	{
	public:
		ActiveSeconds advance(std::uint32_t a_rawMs, bool a_paused) noexcept
		{
			if (!initialized_) {
				initialized_ = true;
			} else if (!a_paused) {
				seconds_ += static_cast<double>(a_rawMs - lastRawMs_) / 1000.0;
			}
			lastRawMs_ = a_rawMs;
			return seconds_;
		}

		[[nodiscard]] ActiveSeconds seconds() const noexcept { return seconds_; }

	private:
		std::uint32_t lastRawMs_{};
		double seconds_{};
		bool initialized_{};
	};
}
