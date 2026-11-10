#include "menu/MenuText.h"

#include <array>
#include <cstdio>
#include <utility>

namespace slacs
{
	namespace
	{
		std::string HexForm(FormId a_form)
		{
			std::array<char, 16> buffer{};
			std::snprintf(buffer.data(), buffer.size(), "0x%08X", a_form);
			return buffer.data();
		}

		std::string_view IssueKeyLabel(std::string_view a_key)
		{
			if (a_key == "$") {
				return "配置文件内容";
			}
			if (a_key == "schemaVersion") {
				return "配置版本";
			}
			if (a_key == "naturalDecay") {
				return "自然衰减";
			}
			if (a_key == "rainAccelerates") {
				return "雨水加速衰减";
			}
			if (a_key == "decayIntervalGameHours") {
				return "自然衰减间隔";
			}
			if (a_key == "maxLayersPerRegion") {
				return "每区域层数上限";
			}
			if (a_key == "layerRemovalOrder") {
				return "层移除顺序";
			}
			if (a_key == "swimCleaning") {
				return "游泳清洗策略";
			}
			if (a_key == "timedClear") {
				return "定时清洗模式";
			}
			if (a_key == "timedClearSeconds") {
				return "定时清洗秒数";
			}
			if (a_key == "overlayOpacity") {
				return "覆盖不透明度";
			}
			if (a_key == "overlaysEnabled") {
				return "覆盖开关";
			}
			if (a_key == "effectsEnabled") {
				return "效果法术开关";
			}
			if (a_key == "spellLevel1Threshold") {
				return "一级法术阈值";
			}
			if (a_key == "spellLevel2Threshold") {
				return "二级法术阈值";
			}
			if (a_key == "allowFemaleFluid") {
				return "女性来源体液";
			}
			if (a_key == "femaleTargetsRequirePlayerOrFollower") {
				return "女性目标限制";
			}
			if (a_key == "enabledSourceClasses") {
				return "来源类别开关";
			}
			if (a_key == "megaCumForAll") {
				return "Mega 贴图";
			}
			return a_key;
		}
	}

	std::string_view RegionLabel(Region a_region)
	{
		switch (a_region) {
		case Region::BodyVaginal:
			return "阴道";
		case Region::BodyAnal:
			return "肛门";
		case Region::BodyBreast:
			return "胸部";
		case Region::BodyLegs:
			return "腿部";
		case Region::Face:
			return "面部/口腔";
		case Region::Hands:
			return "手部";
		case Region::Feet:
			return "足部";
		}
		return "未知区域";
	}

	std::string_view ModuleLabel(RuntimeModule a_module)
	{
		switch (a_module) {
		case RuntimeModule::Runtime:
			return "运行时";
		case RuntimeModule::Ingress:
			return "通知队列";
		case RuntimeModule::PPlus:
			return "SexLab P+";
		case RuntimeModule::Core:
			return "核心逻辑";
		case RuntimeModule::Scheduler:
			return "计时器";
		case RuntimeModule::Environment:
			return "环境";
		case RuntimeModule::Overlay:
			return "覆盖层";
		case RuntimeModule::Effects:
			return "效果";
		case RuntimeModule::Settings:
			return "设置";
		case RuntimeModule::Menu:
			return "菜单";
		case RuntimeModule::Persistence:
			return "存档";
		case RuntimeModule::Papyrus:
			return "Papyrus";
		}
		return "未知模块";
	}

	std::string_view DiagnosticCodeLabel(RuntimeDiagnosticCode a_code)
	{
		switch (a_code) {
		case RuntimeDiagnosticCode::DependencyUnavailable:
			return "依赖不可用";
		case RuntimeDiagnosticCode::VersionMismatch:
			return "依赖版本不匹配";
		case RuntimeDiagnosticCode::InvalidNativeForm:
			return "原生表单无效";
		case RuntimeDiagnosticCode::ActorUnavailable:
			return "角色不可用";
		case RuntimeDiagnosticCode::QueryFailed:
			return "P+ 查询失败";
		case RuntimeDiagnosticCode::QueryTimedOut:
			return "P+ 查询超时";
		case RuntimeDiagnosticCode::QueryCancelled:
			return "P+ 查询已取消";
		case RuntimeDiagnosticCode::StaleEpoch:
			return "会话已变化，操作被忽略";
		case RuntimeDiagnosticCode::StaleRevision:
			return "角色状态版本已变化";
		case RuntimeDiagnosticCode::CapacityUnavailable:
			return "RaceMenu 覆盖槽不足";
		case RuntimeDiagnosticCode::AdapterFailure:
			return "适配器失败";
		case RuntimeDiagnosticCode::SettingsRejected:
			return "设置校验未通过";
		case RuntimeDiagnosticCode::PersistenceFailure:
			return "存档读写失败";
		case RuntimeDiagnosticCode::QueueOverloaded:
			return "通知队列已满";
		}
		return "未知诊断";
	}

	std::string_view SettingsIssueCodeLabel(SettingsIssueCode a_code)
	{
		switch (a_code) {
		case SettingsIssueCode::Parse:
			return "解析失败";
		case SettingsIssueCode::UnknownField:
			return "未知字段";
		case SettingsIssueCode::InvalidEnum:
			return "无效枚举值";
		case SettingsIssueCode::OutOfRange:
			return "超出允许范围";
		case SettingsIssueCode::DuplicateForm:
			return "重复表单";
		}
		return "未知问题";
	}

	std::string SettingsIssueText(const SettingsIssue& a_issue)
	{
		std::string text{ IssueKeyLabel(a_issue.key) };
		text += "：";
		text += SettingsIssueCodeLabel(a_issue.code);
		if (IssueKeyLabel(a_issue.key) != a_issue.key) {
			text += "（";
			text += a_issue.key;
			text += "）";
		}
		return text;
	}

	std::string FormatDiagnostic(const RuntimeDiagnostic& a_diagnostic)
	{
		std::string text{ "[" };
		text += ModuleLabel(a_diagnostic.module);
		text += "] ";
		text += DiagnosticCodeLabel(a_diagnostic.code);

		if (a_diagnostic.actor) {
			text += "，角色 ";
			text += std::to_string(*a_diagnostic.actor);
		}
		if (a_diagnostic.form && *a_diagnostic.form != kInvalidFormId) {
			text += "，表单 ";
			text += HexForm(*a_diagnostic.form);
		}
		if (a_diagnostic.tid) {
			text += "，tid ";
			text += std::to_string(*a_diagnostic.tid);
		}
		if (a_diagnostic.requestId) {
			text += "，请求 ";
			text += std::to_string(*a_diagnostic.requestId);
		}
		return text;
	}

	std::string SettingsFeedbackText(const SettingsFeedback& a_feedback)
	{
		switch (a_feedback.apply) {
		case SettingsApplyStatus::Rejected:
			{
				std::string text{ "设置未应用" };
				if (!a_feedback.issues.empty()) {
					text += "：";
					text += SettingsIssueText(a_feedback.issues.front());
					if (a_feedback.issues.size() > 1) {
						text += "；另有 ";
						text += std::to_string(a_feedback.issues.size() - 1);
						text += " 项问题";
					}
				}
				text += "。";
				return text;
			}
		case SettingsApplyStatus::Applied:
			switch (a_feedback.write) {
			case SettingsWriteStatus::Saved:
				return "设置已应用并保存。";
			case SettingsWriteStatus::Failed:
				return "设置已应用，保存失败。本次运行仍使用新设置，重启后会恢复为磁盘上的旧设置。";
			case SettingsWriteStatus::NotRequested:
				return "设置已应用（尚未写盘）。";
			}
			break;
		case SettingsApplyStatus::Unchanged:
			switch (a_feedback.write) {
			case SettingsWriteStatus::Saved:
				return "设置未变化，已写入配置文件。";
			case SettingsWriteStatus::Failed:
				return "设置未变化，保存失败。";
			case SettingsWriteStatus::NotRequested:
				return {};
			}
			break;
		}
		return {};
	}

	std::string_view DependencyLabel(DependencyKind a_dependency)
	{
		switch (a_dependency) {
		case DependencyKind::Runtime1170:
			return "Skyrim 1.6.1170 运行时";
		case DependencyKind::PPlus:
			return "SexLab P+";
		case DependencyKind::RaceMenuV2:
			return "RaceMenu 覆盖接口";
		case DependencyKind::MenuFramework:
			return "SKSE Menu Framework";
		case DependencyKind::RequiredForms:
			return "SLACS 插件记录";
		}
		return "未知依赖";
	}

	std::string_view DependencyStatusLabel(DependencyStatus a_status)
	{
		switch (a_status) {
		case DependencyStatus::Available:
			return "可用";
		case DependencyStatus::Missing:
			return "缺失";
		case DependencyStatus::VersionMismatch:
			return "版本不匹配";
		}
		return "未知状态";
	}
}
