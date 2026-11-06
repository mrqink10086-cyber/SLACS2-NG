#include "integration/Papyrus.h"
#include "runtime/Runtime.h"

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	if (!a_skse || a_skse->RuntimeVersion() != SKSE::RUNTIME_SSE_1_6_1170) {
		return false;
	}

	SKSE::Init(a_skse);
	logs::info("SLACS2 NG loading for Skyrim 1.6.1170");

	auto& runtime = slacs::Runtime::instance();
	if (!runtime.install()) {
		logs::error("SLACS2 NG runtime install failed");
		return false;
	}

	auto* papyrus = SKSE::GetPapyrusInterface();
	if (!papyrus || !papyrus->Register(slacs::Native::Register)) {
		logs::error("SLACS2 NG Papyrus registration failed");
		return false;
	}

	return true;
}
