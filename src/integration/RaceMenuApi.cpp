#include "integration/RaceMenuApi.h"

#include "integration/racemenu/IPluginInterface.h"

#include <SKSE/SKSE.h>

#include <memory>

namespace slacs
{
	bool RaceMenuApi::initialize()
	{
		reset();

		auto* messaging = SKSE::GetMessagingInterface();
		if (!messaging) {
			logs::warn("RaceMenu handshake skipped: messaging interface unavailable");
			return false;
		}

		::InterfaceExchangeMessage message{};
		const bool dispatched = messaging->Dispatch(
			static_cast<std::uint32_t>(::InterfaceExchangeMessage::kMessage_ExchangeInterface),
			std::addressof(message),
			sizeof(message),
			"SKEE");
		if (!dispatched || !message.interfaceMap) {
			logs::warn("RaceMenu handshake failed: SKEE did not answer");
			return false;
		}

		auto* overlay = message.interfaceMap->QueryInterface("Overlay");
		auto* overrideApi = message.interfaceMap->QueryInterface("Override");
		if (!overlay || !overrideApi) {
			logs::warn("RaceMenu handshake failed: Overlay/Override interface missing");
			return false;
		}
		if (overlay->GetVersion() < 2 || overrideApi->GetVersion() < 2) {
			version_ = overlay->GetVersion();
			logs::warn(
				"RaceMenu handshake version mismatch: overlay={} override={}",
				overlay->GetVersion(),
				overrideApi->GetVersion());
			return false;
		}

		overlay_ = overlay;
		override_ = overrideApi;
		version_ = overlay->GetVersion();
		ready_ = true;
		logs::info("RaceMenu overlay/override v{} ready", version_);
		return true;
	}

	void RaceMenuApi::reset()
	{
		overlay_ = nullptr;
		override_ = nullptr;
		version_ = 0;
		ready_ = false;
	}

	bool RaceMenuApi::available() const noexcept
	{
		return ready_ && overlay_ != nullptr && override_ != nullptr;
	}

	std::uint32_t RaceMenuApi::version() const noexcept
	{
		return version_;
	}

	void* RaceMenuApi::overlay() const noexcept
	{
		return overlay_;
	}

	void* RaceMenuApi::overrideApi() const noexcept
	{
		return override_;
	}
}
