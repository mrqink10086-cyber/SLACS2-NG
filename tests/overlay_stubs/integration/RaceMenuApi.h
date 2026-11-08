#pragma once

#include "integration/racemenu/IPluginInterface.h"

inline IOverlayInterface fakeOverlay;
inline IOverrideInterface fakeOverride;

namespace slacs
{
	class RaceMenuApi
	{
	public:
		bool initialize() { return true; }
		bool available() const { return true; }
		std::uint32_t version() const { return 2; }
		void* overlay() const { return &fakeOverlay; }
		void* overrideApi() const { return &fakeOverride; }
	};
}
