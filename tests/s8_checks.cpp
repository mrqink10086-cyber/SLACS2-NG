#include "config/Settings.h"
#include "menu/MenuState.h"
#include "menu/MenuText.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
	int g_failures = 0;

	void Check(bool a_condition, const char* a_message)
	{
		if (!a_condition) {
			std::cerr << "FAIL: " << a_message << "\n";
			++g_failures;
		}
	}

	void TestLegacySlotMigration()
	{
		const auto path = std::filesystem::path("slacs2ng_legacy_slots.json");
		{
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file << R"({"schemaVersion":1,"firstOwnedSlot":[27,13,9,9],"overlayOpacity":0.6})";
		}
		const auto loaded = slacs::Settings::load(path);
		Check(loaded.status == slacs::SettingsLoadStatus::Loaded, "legacy manual slots do not prevent loading settings");
		Check(loaded.value.overlayOpacity == 0.6F, "legacy migration preserves other settings");
		Check(slacs::Settings::save(path, loaded.value).status == slacs::SettingsSaveStatus::Saved, "legacy settings can be saved");
		std::ifstream file(path, std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		Check(text.find("firstOwnedSlot") == std::string::npos, "saving migrates away from manual slot indices");
		file.close();
		std::filesystem::remove(path);
	}

	void TestSettingsRoundTrip()
	{
		const auto path = std::filesystem::path("slacs2ng_s8_settings.json");
		std::filesystem::remove(path);
		std::filesystem::remove(path.string() + ".tmp");

		slacs::Settings custom{};
		custom.overlayOpacity = 0.42F;
		custom.megaCumForAll = true;
		custom.timedClear = slacs::TimedClearMode::Disabled;
		custom.enabledSourceClasses = { true, false, true, false, false, true };

		const auto saved = slacs::Settings::save(path, custom);
		Check(saved.status == slacs::SettingsSaveStatus::Saved, "custom settings should save");
		Check(std::filesystem::exists(path), "saved settings file exists");
		Check(!std::filesystem::exists(path.string() + ".tmp"), "temporary settings file is renamed");

		const auto loaded = slacs::Settings::load(path);
		Check(loaded.status == slacs::SettingsLoadStatus::Loaded, "saved settings should load");
		Check(loaded.value == custom, "all settings round-trip");
		Check(loaded.value.overlayOpacity == 0.42F, "opacity round-trips");

		auto bomPath = std::filesystem::path("slacs2ng_s8_bom.json");
		{
			std::ifstream source(path, std::ios::binary);
			std::string text((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
			std::ofstream target(bomPath, std::ios::binary | std::ios::trunc);
			target.put(static_cast<char>(0xEF));
			target.put(static_cast<char>(0xBB));
			target.put(static_cast<char>(0xBF));
			target << text;
		}
		const auto bom = slacs::Settings::load(bomPath);
		Check(bom.status == slacs::SettingsLoadStatus::Loaded, "UTF-8 BOM settings load");
		Check(bom.value == custom, "BOM settings preserve values");

		auto invalid = custom;
		invalid.overlayOpacity = 1.5F;
		const auto rejected = slacs::Settings::save(path, invalid);
		Check(rejected.status == slacs::SettingsSaveStatus::Invalid, "invalid opacity is rejected before writing");
		Check(!rejected.issues.empty(), "invalid save reports an issue");

		std::filesystem::remove(path);
		std::filesystem::remove(bomPath);
	}

	void TestSettingsDiff()
	{
		slacs::Settings a{};
		slacs::Settings b{};
		Check(a == b, "default settings compare equal");
		b.overlayOpacity = 0.5F;
		Check(!(a == b), "settings compare unequal after a field change");
		b = a;
		b.decayIntervalGameHours = 2.0;
		Check(!(a == b), "settings compare unequal after interval change");
	}

	void TestMenuState()
	{
		slacs::MenuSnapshot published{};
		published.epoch = 9;
		published.revision = 3;
		published.sessionReady = true;
		published.settings.overlayOpacity = 0.25F;
		published.settingsFeedback.apply = slacs::SettingsApplyStatus::Applied;
		published.settingsFeedback.write = slacs::SettingsWriteStatus::Saved;
		published.actors.push_back(slacs::TrackedActorRow{
			7,
			0x1234,
			"测试角色",
			{ 1, 2, 3, 4, 5, 0, 0 },
			true,
			false,
		});
		published.diagnostics.push_back(slacs::DiagnosticRow{
			{},
			"测试诊断",
		});

		slacs::MenuState::publish(std::move(published));

		const auto snapshot = slacs::MenuState::snapshot();
		Check(snapshot.epoch == 9, "snapshot epoch is visible");
		Check(snapshot.revision == 3, "snapshot revision is visible");
		Check(snapshot.sessionReady, "snapshot session state is visible");
		Check(snapshot.settings.overlayOpacity == 0.25F, "snapshot settings are visible");
		Check(snapshot.actors.size() == 1, "snapshot actor rows are visible");
		if (!snapshot.actors.empty()) {
			Check(snapshot.actors.front().displayName == "测试角色", "snapshot owns actor display name");
			Check(snapshot.actors.front().layers[4] == 5, "snapshot owns layer counts");
		}
		Check(snapshot.diagnostics.size() == 1, "snapshot diagnostics are visible");

		const auto copy = slacs::MenuState::snapshot();
		Check(copy.epoch == snapshot.epoch, "snapshot reads are repeatable");

		slacs::MenuState::publish(slacs::MenuSnapshot{});
		const auto empty = slacs::MenuState::snapshot();
		Check(empty.epoch == 0 && empty.actors.empty(), "new snapshot replaces the old one");
	}

	void TestFeedbackText()
	{
		slacs::SettingsFeedback applied{};
		applied.apply = slacs::SettingsApplyStatus::Applied;
		applied.write = slacs::SettingsWriteStatus::Failed;
		const auto failedWrite = slacs::SettingsFeedbackText(applied);
		Check(failedWrite.find("设置已应用，保存失败") != std::string::npos,
			"failed write keeps applied settings message");
		Check(failedWrite.find("重启后会恢复") != std::string::npos,
			"failed write explains restart behavior");

		slacs::SettingsFeedback rejected{};
		rejected.apply = slacs::SettingsApplyStatus::Rejected;
		rejected.issues.push_back(slacs::SettingsIssue{ "overlayOpacity", slacs::SettingsIssueCode::OutOfRange });
		const auto rejectedText = slacs::SettingsFeedbackText(rejected);
		Check(rejectedText.find("设置未应用") != std::string::npos, "rejected settings text is shown");
		Check(rejectedText.find("覆盖不透明度") != std::string::npos, "rejected issue field is translated");

		slacs::SettingsFeedback unchanged{};
		unchanged.apply = slacs::SettingsApplyStatus::Unchanged;
		unchanged.write = slacs::SettingsWriteStatus::NotRequested;
		Check(slacs::SettingsFeedbackText(unchanged).empty(), "unchanged settings produce no feedback");

		slacs::RuntimeDiagnostic diagnostic{};
		diagnostic.module = slacs::RuntimeModule::Menu;
		diagnostic.code = slacs::RuntimeDiagnosticCode::StaleEpoch;
		const auto formatted = slacs::FormatDiagnostic(diagnostic);
		Check(formatted.find("菜单") != std::string::npos, "diagnostic module is translated");
		Check(formatted.find("会话已变化") != std::string::npos, "diagnostic code is translated");
	}
}

int main()
{
	TestLegacySlotMigration();
	TestSettingsRoundTrip();
	TestSettingsDiff();
	TestMenuState();
	TestFeedbackText();
	if (g_failures != 0) {
		std::cerr << g_failures << " test(s) failed\n";
		return 1;
	}
	std::cout << "SLACS2 NG S8 checks passed\n";
	return 0;
}
