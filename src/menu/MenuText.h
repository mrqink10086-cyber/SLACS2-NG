#pragma once

#include "config/Settings.h"
#include "core/Types.h"
#include "menu/MenuTypes.h"

#include <string>
#include <string_view>

namespace slacs
{
	[[nodiscard]] std::string_view RegionLabel(Region a_region);
	[[nodiscard]] std::string_view ModuleLabel(RuntimeModule a_module);
	[[nodiscard]] std::string_view DiagnosticCodeLabel(RuntimeDiagnosticCode a_code);
	[[nodiscard]] std::string_view SettingsIssueCodeLabel(SettingsIssueCode a_code);
	[[nodiscard]] std::string SettingsIssueText(const SettingsIssue& a_issue);
	[[nodiscard]] std::string FormatDiagnostic(const RuntimeDiagnostic& a_diagnostic);
	[[nodiscard]] std::string SettingsFeedbackText(const SettingsFeedback& a_feedback);
	[[nodiscard]] std::string_view DependencyLabel(DependencyKind a_dependency);
	[[nodiscard]] std::string_view DependencyStatusLabel(DependencyStatus a_status);
}
