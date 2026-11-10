#include "config/Settings.h"
#include "runtime/Ingress.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

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

	void TestSettings()
	{
		const auto defaults = slacs::Settings::validate(slacs::Settings{});
		Check(defaults.valid, "default settings should validate");

		const auto path = std::filesystem::path("slacs2ng_settings_test.json");
		std::filesystem::remove(path);

		auto missing = slacs::Settings::load(path);
		Check(missing.status == slacs::SettingsLoadStatus::MissingUsingDefaults, "missing file should use defaults");

		auto saved = slacs::Settings::save(path, slacs::Settings{});
		if (saved.status != slacs::SettingsSaveStatus::Saved) {
			std::cerr << "save status=" << static_cast<int>(saved.status) << " issues=" << saved.issues.size() << "\n";
			for (const auto& issue : saved.issues) {
				std::cerr << "  " << issue.key << " code=" << static_cast<int>(issue.code) << "\n";
			}
		}
		Check(saved.status == slacs::SettingsSaveStatus::Saved, "default settings should save");
		auto loaded = slacs::Settings::load(path);
		Check(loaded.status == slacs::SettingsLoadStatus::Loaded, "saved settings should load");
		Check(loaded.value.maxLayersPerRegion == 5, "layer cap round-trips");
		Check(loaded.value.enabledSourceClasses == slacs::Settings{}.enabledSourceClasses, "class array round-trips");
		{
			std::ifstream file(path, std::ios::binary);
			const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			Check(text.find("femaleTargetsRequirePlayerOrFollower") == std::string::npos,
				"saving removes the obsolete female target restriction");
		}

		{
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file << R"({"schemaVersion":1,"femaleTargetsRequirePlayerOrFollower":true})";
		}
		auto legacyTargetRestriction = slacs::Settings::load(path);
		Check(legacyTargetRestriction.status == slacs::SettingsLoadStatus::Loaded,
			"legacy female target restriction is accepted and ignored");

		{
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file << R"({"schemaVersion":1,"unknownField":true})";
		}
		auto unknown = slacs::Settings::load(path);
		Check(unknown.status == slacs::SettingsLoadStatus::Invalid, "unknown field should be rejected");
		Check(!unknown.issues.empty() && unknown.issues.front().code == slacs::SettingsIssueCode::UnknownField,
			"unknown field issue should be reported");

		{
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file << R"({"schemaVersion":1,"maxLayersPerRegion":4})";
		}
		auto invalidCap = slacs::Settings::load(path);
		Check(invalidCap.status == slacs::SettingsLoadStatus::Invalid, "non-five layer cap should be rejected");

		const auto writeText = [&path](std::string_view a_text) {
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file.write(a_text.data(), static_cast<std::streamsize>(a_text.size()));
		};
		const auto rejectedByParserBudget = [&path](const char* a_message) {
			const auto loadedSettings = slacs::Settings::load(path);
			Check(loadedSettings.status == slacs::SettingsLoadStatus::Invalid &&
					!loadedSettings.issues.empty() &&
					loadedSettings.issues.front().code == slacs::SettingsIssueCode::Parse,
				a_message);
		};

		{
			std::string atArrayWidth = R"({"schemaVersion":1,"unknown":[)";
			for (std::size_t i = 0; i < 64; ++i) {
				if (i != 0) {
					atArrayWidth += ',';
				}
				atArrayWidth += '0';
			}
			atArrayWidth += "]}";
			writeText(atArrayWidth);
			auto loadedAtArrayWidth = slacs::Settings::load(path);
			Check(loadedAtArrayWidth.status == slacs::SettingsLoadStatus::Invalid &&
					!loadedAtArrayWidth.issues.empty() &&
					loadedAtArrayWidth.issues.front().code == slacs::SettingsIssueCode::UnknownField,
				"array at the container limit should still be parsed");

			std::string atObjectWidth = R"({"schemaVersion":1)";
			for (std::size_t i = 0; i < 63; ++i) {
				atObjectWidth += ",\"unknown" + std::to_string(i) + "\":true";
			}
			atObjectWidth += '}';
			writeText(atObjectWidth);
			auto loadedAtObjectWidth = slacs::Settings::load(path);
			Check(loadedAtObjectWidth.status == slacs::SettingsLoadStatus::Invalid &&
					!loadedAtObjectWidth.issues.empty() &&
					loadedAtObjectWidth.issues.front().code == slacs::SettingsIssueCode::UnknownField,
				"object at the container limit should still be parsed");
		}

		{
			std::string wideArray = R"({"schemaVersion":1,"unknown":[)";
			for (std::size_t i = 0; i < 2048; ++i) {
				if (i != 0) {
					wideArray += ',';
				}
				wideArray += '0';
			}
			wideArray += "]}";
			writeText(wideArray);
			rejectedByParserBudget("wide array should be rejected before its values are materialized");
		}

		{
			std::string wideObject = R"({"schemaVersion":1)";
			for (std::size_t i = 0; i < 128; ++i) {
				wideObject += ",\"unknown" + std::to_string(i) + "\":true";
			}
			wideObject += '}';
			writeText(wideObject);
			rejectedByParserBudget("wide object should be rejected before its members are materialized");
		}

		{
			std::string manyNodes = R"({"schemaVersion":1,"unknown":[)";
			for (std::size_t row = 0; row < 16; ++row) {
				if (row != 0) {
					manyNodes += ',';
				}
				manyNodes += '[';
				for (std::size_t column = 0; column < 64; ++column) {
					if (column != 0) {
						manyNodes += ',';
					}
					manyNodes += '0';
				}
				manyNodes += ']';
			}
			manyNodes += "]}";
			writeText(manyNodes);
			rejectedByParserBudget("JSON node budget should reject a broad nested tree");
		}

		{
			std::string atNodeBudget = R"({"schemaVersion":1,"unknown":[)";
			for (std::size_t row = 0; row < 16; ++row) {
				if (row != 0) {
					atNodeBudget += ',';
				}
				atNodeBudget += '[';
				const std::size_t columns = row == 15 ? 60 : 63;
				for (std::size_t column = 0; column < columns; ++column) {
					if (column != 0) {
						atNodeBudget += ',';
					}
					atNodeBudget += '0';
				}
				atNodeBudget += ']';
			}
			atNodeBudget += "]}";
			writeText(atNodeBudget);
			auto loadedAtNodeBudget = slacs::Settings::load(path);
			Check(loadedAtNodeBudget.status == slacs::SettingsLoadStatus::Invalid &&
					!loadedAtNodeBudget.issues.empty() &&
					loadedAtNodeBudget.issues.front().code == slacs::SettingsIssueCode::UnknownField,
				"JSON at the node limit should still be parsed");
		}

		{
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file << R"({"schemaVersion":1,"unknown":")";
			const std::string chunk(4096, 'x');
			for (int i = 0; i < 32; ++i) {
				file.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
			}
			file << R"("})";
		}
		rejectedByParserBudget("oversized settings file should be rejected before parsing");

		{
			std::string atByteLimit = R"({"schemaVersion":1})";
			atByteLimit.resize(64 * 1024, ' ');
			writeText(atByteLimit);
			auto loadedAtByteLimit = slacs::Settings::load(path);
			Check(loadedAtByteLimit.status == slacs::SettingsLoadStatus::Loaded,
				"settings file at the byte limit should still load");
		}

		{
			std::string withinDepth = R"({"schemaVersion":1,"unknown":)";
			withinDepth.append(31, '[');
			withinDepth += '0';
			withinDepth.append(31, ']');
			withinDepth += '}';
			writeText(withinDepth);
			auto loadedWithinDepth = slacs::Settings::load(path);
			Check(loadedWithinDepth.status == slacs::SettingsLoadStatus::Invalid &&
					!loadedWithinDepth.issues.empty() &&
					loadedWithinDepth.issues.front().code == slacs::SettingsIssueCode::UnknownField,
				"JSON at the existing nesting limit should still be parsed");

			std::string beyondDepth = R"({"schemaVersion":1,"unknown":)";
			beyondDepth.append(32, '[');
			beyondDepth += '0';
			beyondDepth.append(32, ']');
			beyondDepth += '}';
			writeText(beyondDepth);
			rejectedByParserBudget("JSON beyond the existing nesting limit should be rejected");
		}

		std::filesystem::remove(path);
	}

	void TestIngress()
	{
		int wakes = 0;
		slacs::Ingress ingress(2, [&wakes] { ++wakes; });

		auto closed = ingress.postOrgasm(0x1234, 7, 42);
		Check(closed.status == slacs::IngressStatus::SessionClosed, "closed ingress rejects orgasm");
		Check(ingress.size() == 0, "closed ingress stays empty");

		ingress.setOpen(true);
		const auto first = ingress.postOrgasm(0x1234, 7, 42);
		const auto second = ingress.postOrgasm(0x1234, 7, 42);
		Check(first.status == slacs::IngressStatus::Accepted && first.sequence.has_value(), "first orgasm accepted");
		Check(second.status == slacs::IngressStatus::Accepted && second.sequence.has_value(), "second orgasm accepted");
		Check(first.sequence && second.sequence && *second.sequence == *first.sequence + 1, "orgasm sequence increments");
		Check(wakes == 2, "each accepted command wakes once");
		Check(ingress.size() == 2, "both orgasms are queued");

		const auto overflow = ingress.postOrgasm(0x1234, 7, 42);
		Check(overflow.status == slacs::IngressStatus::Overloaded, "full ingress rejects excess commands");

		auto firstCommand = ingress.pop();
		auto secondCommand = ingress.pop();
		Check(firstCommand && secondCommand, "queued orgasms are consumable");
		if (firstCommand) {
			const auto* orgasm = std::get_if<slacs::NativeOrgasm>(&*firstCommand);
			Check(orgasm && orgasm->sourceFormId == 0x1234 && orgasm->tid == 7 && orgasm->epoch == 42,
				"orgasm parameters survive enqueue");
		}
		if (secondCommand) {
			const auto* orgasm = std::get_if<slacs::NativeOrgasm>(&*secondCommand);
			Check(orgasm && orgasm->sequence == *second.sequence, "second orgasm sequence survives enqueue");
		}

		const auto scene = ingress.post(slacs::SceneNotice{ slacs::ScenePhase::Start, 9, true, 42 });
		Check(scene.status == slacs::IngressStatus::Accepted, "scene start accepted");
		auto sceneCommand = ingress.pop();
		Check(sceneCommand.has_value(), "scene command queued");
		if (sceneCommand) {
			const auto* notice = std::get_if<slacs::SceneNotice>(&*sceneCommand);
			Check(notice && notice->phase == slacs::ScenePhase::Start && notice->tid == 9 && notice->hasPlayer,
				"scene start parameters survive enqueue");
		}

		const auto sceneEnd = ingress.post(slacs::SceneNotice{ slacs::ScenePhase::End, 9, true, 42 });
		Check(sceneEnd.status == slacs::IngressStatus::Accepted, "scene end accepted");
		auto sceneEndCommand = ingress.pop();
		Check(sceneEndCommand.has_value(), "scene end command queued");
		if (sceneEndCommand) {
			const auto* notice = std::get_if<slacs::SceneNotice>(&*sceneEndCommand);
			Check(notice && notice->phase == slacs::ScenePhase::End && notice->tid == 9 && notice->hasPlayer,
				"scene end parameters survive enqueue");
		}

		ingress.setOpen(false);
		const auto closedAgain = ingress.post(slacs::SceneNotice{ slacs::ScenePhase::End, 9, true, 42 });
		Check(closedAgain.status == slacs::IngressStatus::SessionClosed, "closing ingress rejects new commands");

		int reservedWakes = 0;
		slacs::Ingress reserved(1, [&reservedWakes] { ++reservedWakes; });
		reserved.setOpen(true);
		Check(reserved.reserve(1), "terminal capacity can be reserved");
		Check(reserved.reserved() == 1, "reserved count reflects reservation");
		const auto blocked = reserved.postOrgasm(0x55, 11, 42);
		Check(blocked.status == slacs::IngressStatus::Overloaded, "reservation protects terminal capacity");
		const auto terminal = reserved.postReserved(slacs::PPlusCompleted{});
		Check(terminal.status == slacs::IngressStatus::Accepted, "reserved completion is accepted");
		Check(reserved.reserved() == 0, "accepted completion consumes reservation");
		Check(reservedWakes == 1, "reserved completion wakes once");
		const auto full = reserved.postOrgasm(0x55, 11, 42);
		Check(full.status == slacs::IngressStatus::Overloaded, "capacity is still bounded after completion");
		auto drained = reserved.pop();
		Check(drained.has_value(), "reserved completion is consumable");
		reserved.release(1);
		Check(reserved.reserved() == 0, "release is clamped at zero");
	}
}

int main()
{
	TestSettings();
	TestIngress();
	if (g_failures != 0) {
		std::cerr << g_failures << " test(s) failed\n";
		return 1;
	}
	std::cout << "SLACS2 NG S1 checks passed\n";
	return 0;
}
