#pragma once

#include "config/Settings.h"
#include "core/Types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace slacs
{
	enum class SettingsApplyStatus : std::uint8_t
	{
		Unchanged,
		Applied,
		Rejected,
	};

	enum class SettingsWriteStatus : std::uint8_t
	{
		NotRequested,
		Saved,
		Failed,
	};

	struct SettingsFeedback
	{
		SettingsApplyStatus apply{ SettingsApplyStatus::Unchanged };
		SettingsWriteStatus write{ SettingsWriteStatus::NotRequested };
		std::vector<SettingsIssue> issues;
	};

	struct TrackedActorRow
	{
		ActorId actor{};
		FormId formId{};
		std::string displayName;
		std::array<std::uint8_t, kRegionCount> layers{};
		bool has3D{};
		bool visualDegraded{};
		bool tracked{ true };
	};

	struct DiagnosticRow
	{
		RuntimeDiagnostic diagnostic;
		std::string text;
	};

	struct MenuSnapshot
	{
		SessionEpoch epoch{};
		Revision revision{};
		bool sessionReady{};
		std::array<std::uint32_t, 4> overlayCapacities{};
		Settings settings;
		SettingsFeedback settingsFeedback;
		std::vector<TrackedActorRow> actors;
		std::vector<DiagnosticRow> diagnostics;
	};
}
