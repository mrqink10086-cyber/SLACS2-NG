#include "menu/Menu.h"

#include "menu/MenuState.h"
#include "menu/MenuText.h"
#include "runtime/Runtime.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace slacs
{
	namespace
	{
		struct Vec2
		{
			float x{};
			float y{};
		};

		using RenderFunction = void(__stdcall*)();

		struct Api
		{
			using AddSectionItemFn = void (*)(const char*, RenderFunction);
			using ButtonFn = bool (*)(const char*, Vec2);
			using CheckboxFn = bool (*)(const char*, bool*);
			using SliderFloatFn = bool (*)(const char*, float*, float, float, const char*, int);
			using SliderIntFn = bool (*)(const char*, int*, int, int, const char*, int);
			using InputIntFn = bool (*)(const char*, int*, int, int, int);
			using CollapsingHeaderFn = bool (*)(const char*, int);
			using SelectableFn = bool (*)(const char*, bool, int, Vec2);
			using BeginComboFn = bool (*)(const char*, const char*, int);
			using EndComboFn = void (*)();
			using TextFn = void (*)(const char*, ...);
			using TextUnformattedFn = void (*)(const char*, const char*);
			using TextWrappedFn = void (*)(const char*, ...);
			using TextDisabledFn = void (*)(const char*, ...);
			using SeparatorFn = void (*)();
			using SpacingFn = void (*)();
			using SameLineFn = void (*)(float, float);
			using SetNextItemWidthFn = void (*)(float);
			using BeginDisabledFn = void (*)(bool);
			using EndDisabledFn = void (*)();

			AddSectionItemFn addSectionItem{};
			ButtonFn button{};
			CheckboxFn checkbox{};
			SliderFloatFn sliderFloat{};
			SliderIntFn sliderInt{};
			InputIntFn inputInt{};
			CollapsingHeaderFn collapsingHeader{};
			SelectableFn selectable{};
			BeginComboFn beginCombo{};
			EndComboFn endCombo{};
			TextFn text{};
			TextUnformattedFn textUnformatted{};
			TextWrappedFn textWrapped{};
			TextDisabledFn textDisabled{};
			SeparatorFn separator{};
			SpacingFn spacing{};
			SameLineFn sameLine{};
			SetNextItemWidthFn setNextItemWidth{};
			BeginDisabledFn beginDisabled{};
			EndDisabledFn endDisabled{};

			bool connect()
			{
				if (ready) {
					return true;
				}

				const auto module = GetModuleHandleW(L"SKSEMenuFramework");
				if (!module) {
					return false;
				}

				bool ok = true;
				const auto load = [module, &ok](const char* a_name, auto& a_target) {
					a_target = reinterpret_cast<std::remove_reference_t<decltype(a_target)>>(
						GetProcAddress(module, a_name));
					if (!a_target) {
						logs::warn("SKSEMenuFramework export missing: {}", a_name);
						ok = false;
					}
				};

				load("AddSectionItem", addSectionItem);
				load("igButton", button);
				load("igCheckbox", checkbox);
				load("igSliderFloat", sliderFloat);
				load("igSliderInt", sliderInt);
				load("igInputInt", inputInt);
				load("igCollapsingHeader_TreeNodeFlags", collapsingHeader);
				load("igSelectable_Bool", selectable);
				load("igBeginCombo", beginCombo);
				load("igEndCombo", endCombo);
				load("igText", text);
				load("igTextUnformatted", textUnformatted);
				load("igTextWrapped", textWrapped);
				load("igTextDisabled", textDisabled);
				load("igSeparator", separator);
				load("igSpacing", spacing);
				load("igSameLine", sameLine);
				load("igSetNextItemWidth", setNextItemWidth);
				load("igBeginDisabled", beginDisabled);
				load("igEndDisabled", endDisabled);

				ready = ok;
				return ok;
			}

			bool ready{};
		};

		Api& api()
		{
			static Api instance;
			return instance;
		}

		void Text(const std::string& a_text)
		{
			api().textUnformatted(a_text.c_str(), a_text.c_str() + a_text.size());
		}

		void Text(const char* a_text)
		{
			api().textUnformatted(a_text, nullptr);
		}

		void TextDisabled(const std::string& a_text)
		{
			api().textDisabled("%s", a_text.c_str());
		}

		void TextWrapped(const std::string& a_text)
		{
			api().textWrapped("%s", a_text.c_str());
		}

		void SameLine()
		{
			api().sameLine(0.0F, 8.0F);
		}

		std::string HexForm(FormId a_form)
		{
			std::array<char, 16> buffer{};
			std::snprintf(buffer.data(), buffer.size(), "0x%08X", a_form);
			return buffer.data();
		}

		std::string Decimal(std::uint32_t a_value)
		{
			return std::to_string(a_value);
		}

		struct UiState
		{
			SessionEpoch epoch{};
			bool initialized{};
			Settings draft;
			ActorId selectedActor{};
			std::array<bool, kRegionCount> testRegions{ true, true, true, true, true, true, true };
			int testCount{ 1 };
			int testVariant{};
			std::string message;
		};

		UiState& ui()
		{
			static UiState state;
			return state;
		}

		void SyncDraft(const MenuSnapshot& a_snapshot)
		{
			auto& state = ui();
			if (!state.initialized || state.epoch != a_snapshot.epoch) {
				state.epoch = a_snapshot.epoch;
				state.initialized = true;
				state.draft = a_snapshot.settings;
				state.selectedActor = 0;
				state.testCount = 1;
				state.testVariant = 0;
				state.testRegions = { true, true, true, true, true, true, true };
				state.message.clear();
			}
		}

		std::string IngressText(const IngressResult& a_result)
		{
			switch (a_result.status) {
			case IngressStatus::Accepted:
				return "已提交，等待运行时处理。";
			case IngressStatus::SessionClosed:
				return "运行时尚未就绪，操作未提交。";
			case IngressStatus::Overloaded:
				return "通知队列已满，请稍后重试。";
			}
			return "操作未提交。";
		}

		template <class T>
		void Submit(T&& a_action)
		{
			auto result = Menu::submit(MenuAction{ std::forward<T>(a_action) });
			ui().message = IngressText(result);
		}

		void Message(const std::string& a_text)
		{
			ui().message = a_text;
		}

		void RenderSettingsActions(const MenuSnapshot& a_snapshot)
		{
			auto& state = ui();
			const bool dirty = !(state.draft == a_snapshot.settings);
			api().separator();
			if (dirty) {
				Text("当前草稿包含尚未应用的修改。");
			} else {
				TextDisabled("当前草稿与生效设置一致。");
			}

			const auto validation = Settings::validate(state.draft);
			if (!validation.valid) {
				std::string warning{ "输入校验预警：" };
				warning += SettingsIssueText(validation.issues.front());
				if (validation.issues.size() > 1) {
					warning += "；另有 ";
					warning += Decimal(static_cast<std::uint32_t>(validation.issues.size() - 1));
					warning += " 项";
				}
				TextDisabled(warning);
			}

			const bool applySave = api().button("应用并保存", {});
			SameLine();
			const bool applyOnly = api().button("仅应用", {});
			SameLine();
			const bool revert = api().button("还原未应用修改", {});

			if (revert) {
				state.draft = a_snapshot.settings;
				Message("已还原为当前生效设置，未写入磁盘。");
			}
			if (applySave || applyOnly) {
				Submit(ApplySettings{ state.draft, applySave, a_snapshot.epoch });
			}

			const auto feedback = SettingsFeedbackText(a_snapshot.settingsFeedback);
			if (!feedback.empty()) {
				api().spacing();
				Text("上次设置结果：");
				TextWrapped(feedback);
			}
			if (!state.message.empty()) {
				api().spacing();
				TextWrapped(state.message);
			}
		}

		void RenderSettings()
		{
			auto& state = ui();

			api().checkbox("启用体液覆盖层", &state.draft.overlaysEnabled);

			int opacity = static_cast<int>(state.draft.overlayOpacity * 100.0F + 0.5F);
			api().setNextItemWidth(260.0F);
			if (api().sliderInt("覆盖不透明度（%）", &opacity, 0, 100, "%d%%", 0)) {
				state.draft.overlayOpacity = static_cast<float>(opacity) / 100.0F;
			}

			api().checkbox("所有目标使用 Mega 贴图", &state.draft.megaCumForAll);
			api().checkbox("允许女性来源的体液", &state.draft.allowFemaleFluid);
			api().checkbox("女性目标仅限玩家或随从", &state.draft.femaleTargetsRequirePlayerOrFollower);
			api().checkbox("启用体液效果法术", &state.draft.effectsEnabled);

			api().beginDisabled(!state.draft.effectsEnabled);
			int level1 = state.draft.spellLevel1Threshold;
			int level2 = state.draft.spellLevel2Threshold;
			api().setNextItemWidth(260.0F);
			if (api().sliderInt("一级法术阈值（层）", &level1, 1, 5, "%d 层", 0)) {
				state.draft.spellLevel1Threshold = static_cast<std::uint8_t>(level1);
				if (state.draft.spellLevel2Threshold < state.draft.spellLevel1Threshold) {
					state.draft.spellLevel2Threshold = state.draft.spellLevel1Threshold;
				}
			}
			api().setNextItemWidth(260.0F);
			if (api().sliderInt("二级法术阈值（层）", &level2, 1, 5, "%d 层", 0)) {
				state.draft.spellLevel2Threshold = static_cast<std::uint8_t>(level2);
			}
			api().endDisabled();

			if (api().collapsingHeader("来源类别开关", 0)) {
				struct SourceRow
				{
					const char* label;
					std::size_t index;
				};
				constexpr std::array<SourceRow, 6> rows{ {
					{ "男性", 0 },
					{ "女性", 1 },
					{ "扶他", 2 },
					{ "生物（雄性）", 3 },
					{ "生物（雌性）", 4 },
					{ "未知来源", 5 },
				} };
				for (const auto& row : rows) {
					auto& enabled = state.draft.enabledSourceClasses[row.index];
					api().checkbox(row.label, &enabled);
				}
				TextDisabled("生物来源默认关闭；只有 P+ 明确返回对应性别类别时才会使用。");
			}

			if (api().collapsingHeader("高级：RaceMenu 覆盖槽位", 0)) {
				TextDisabled("起始槽位必须与 skee64.ini 的 iNumOverlays 容量匹配；默认 Body 会占用 7..26 连续 20 个槽位。");
				constexpr std::array<const char*, 4> labels{
					"身体起始槽",
					"面部起始槽",
					"手部起始槽",
					"足部起始槽",
				};
				constexpr std::array<int, 4> maxStarts{ 107, 121, 125, 125 };
				for (std::size_t i = 0; i < labels.size(); ++i) {
					int value = state.draft.firstOwnedSlot[i];
					api().setNextItemWidth(220.0F);
					if (api().inputInt(labels[i], &value, 1, 1, 0)) {
						state.draft.firstOwnedSlot[i] = static_cast<std::uint8_t>(std::clamp(value, 0, maxStarts[i]));
					}
				}
				TextDisabled("应用后会迁移已有精痕；修改 skee64.ini 的容量后需重启游戏。RaceMenu 每个域最多 127 个槽。");
			}

		}

		void RenderDecay()
		{
			auto& state = ui();

			TextDisabled("衰减按游戏日历时间结算；睡眠、等待和快速旅行都会推进游戏时间。菜单打开时游戏暂停，主动秒数不增长。");
			api().separator();

			api().checkbox("启用自然衰减", &state.draft.naturalDecay);
			float interval = static_cast<float>(state.draft.decayIntervalGameHours);
			api().setNextItemWidth(260.0F);
			if (api().sliderFloat("每层衰减间隔（游戏小时）", &interval, 0.1F, 24.0F, "%.1f 小时", 0)) {
				state.draft.decayIntervalGameHours = static_cast<double>(interval);
			}
			api().checkbox("室外持续降雨时加速衰减", &state.draft.rainAccelerates);
			TextDisabled("雨水只累计真实观测到的室外连续降雨时长；进入室内、雨停或读档会中断累计。");

			api().separator();
			const bool timedEnabled = state.draft.timedClear == TimedClearMode::Legacy120ActiveSeconds;
			if (api().beginCombo("定时清洗", timedEnabled ? "120 秒（游戏内主动时间）" : "禁用", 0)) {
				if (api().selectable("禁用", !timedEnabled, 0, {})) {
					state.draft.timedClear = TimedClearMode::Disabled;
				}
				if (api().selectable("120 秒（游戏内主动时间）", timedEnabled, 0, {})) {
					state.draft.timedClear = TimedClearMode::Legacy120ActiveSeconds;
					if (state.draft.timedClearSeconds <= 0.0) {
						state.draft.timedClearSeconds = 120.0;
					}
				}
				api().endCombo();
			}

			float seconds = static_cast<float>(state.draft.timedClearSeconds);
			api().beginDisabled(!timedEnabled);
			api().setNextItemWidth(260.0F);
			if (api().sliderFloat("定时清洗时长（秒）", &seconds, 10.0F, 600.0F, "%.0f 秒", 0)) {
				state.draft.timedClearSeconds = static_cast<double>(seconds);
			}
			api().endDisabled();
			TextDisabled("定时清洗只累计游戏内主动时间：暂停、菜单和加载界面不计时；成功添加新层后重新计时。");

			api().separator();
			Text("游泳清洗");
			TextDisabled("沿用 0.7 行为：检测到角色进入游泳状态时立即清除全部区域覆盖，不保留残余层。");
			Text("层移除顺序");
			TextDisabled("沿用 0.7 行为：后添加的层先移除（LIFO）；设置变更裁剪时也先裁剪最新层。");

		}

		const TrackedActorRow* FindActor(const MenuSnapshot& a_snapshot, ActorId a_actor)
		{
			for (const auto& row : a_snapshot.actors) {
				if (row.actor == a_actor) {
					return &row;
				}
			}
			return nullptr;
		}

		void RenderMaintenance(const MenuSnapshot& snapshot)
		{
			auto& state = ui();


			if (snapshot.actors.empty()) {
				TextDisabled("当前会话尚未跟踪任何角色。");
				state.selectedActor = 0;
			} else {
				for (const auto& row : snapshot.actors) {
					std::string label = row.displayName.empty() ? "未命名角色" : row.displayName;
					label += "  ";
					label += HexForm(row.formId);
					if (!row.tracked) {
						label += "  [可测试]";
					}
					if (row.visualDegraded) {
						label += "  [显示降级]";
					}
					if (!row.has3D) {
						label += "  [无 3D]";
					}
					if (api().selectable(label.c_str(), state.selectedActor == row.actor, 0, {})) {
						state.selectedActor = row.actor;
						state.message.clear();
					}
				}
			}

			const auto* selected = FindActor(snapshot, state.selectedActor);
			if (!selected) {
				state.selectedActor = 0;
			}

			api().separator();
			if (selected) {
				Text("选中角色：" + (selected->displayName.empty() ? std::string{ "未命名角色" } : selected->displayName));
				for (std::size_t i = 0; i < kRegionCount; ++i) {
					std::string line{ "  " };
					line += RegionLabel(kRegions[i]);
					line += "：";
					line += Decimal(selected->layers[i]);
					line += " 层";
					Text(line);
				}

				api().beginDisabled(!selected->tracked);
				if (api().button("清除该角色全部覆盖", {})) {
					Submit(ClearActor{ selected->actor, snapshot.epoch });
				}
				SameLine();
				if (api().button("刷新该角色显示", {})) {
					Submit(RefreshActor{ selected->actor, snapshot.epoch });
				}
				api().endDisabled();
			} else {
				TextDisabled("请在上方选择一个角色。");
			}

			api().separator();
			Text("依赖与资源");
			const auto& dependencies = Runtime::instance().dependencies();
			for (const auto& check : dependencies.checks) {
				std::string line{ DependencyLabel(check.dependency) };
				line += "：";
				line += DependencyStatusLabel(check.status);
				if (check.status != DependencyStatus::Available) {
					if (!check.expectedVersion.empty()) {
						line += "（期望 ";
						line += check.expectedVersion;
						line += "，实际 ";
						line += check.actualVersion.empty() ? "未检测到" : check.actualVersion;
						line += "）";
					}
				}
				if (check.status == DependencyStatus::Available) {
					Text(line);
				} else {
					TextDisabled(line);
				}
			}

			api().separator();
			Text("最近诊断");
			if (snapshot.diagnostics.empty()) {
				TextDisabled("无诊断记录。");
			} else {
				const std::size_t first = snapshot.diagnostics.size() > 8 ? snapshot.diagnostics.size() - 8 : 0;
				for (std::size_t i = snapshot.diagnostics.size(); i > first; --i) {
					TextWrapped(snapshot.diagnostics[i - 1].text);
				}
			}

			if (api().collapsingHeader("测试：折叠加层", 0)) {
				TextDisabled("仅用于验收覆盖层和效果；测试加层与正常加层共用 5 层上限、LIFO 移除和定时清洗。");
				for (std::size_t i = 0; i < kRegionCount; ++i) {
					api().checkbox(RegionLabel(kRegions[i]).data(), &state.testRegions[i]);
					if (i % 3 != 2 && i + 1 < kRegionCount) {
						SameLine();
					}
				}
				api().setNextItemWidth(220.0F);
				api().sliderInt("测试层数", &state.testCount, 1, 5, "%d 层", 0);
				if (api().beginCombo("贴图变体", state.testVariant == 0 ? "普通" : "Mega", 0)) {
					if (api().selectable("普通", state.testVariant == 0, 0, {})) {
						state.testVariant = 0;
					}
					if (api().selectable("Mega", state.testVariant == 1, 0, {})) {
						state.testVariant = 1;
					}
					api().endCombo();
				}

				api().beginDisabled(state.selectedActor == 0);
				if (api().button("添加测试覆盖", {})) {
					std::vector<Region> regions;
					for (std::size_t i = 0; i < kRegionCount; ++i) {
						if (state.testRegions[i]) {
							regions.push_back(kRegions[i]);
						}
					}
					if (regions.empty()) {
						Message("请至少选择一个区域。");
					} else {
						Submit(TestAdd{
							state.selectedActor,
							std::move(regions),
							static_cast<std::uint8_t>(state.testCount),
							state.testVariant == 0 ? TextureVariant::Normal : TextureVariant::Mega,
							snapshot.epoch,
						});
					}
				}
				SameLine();
				api().beginDisabled(!selected || !selected->tracked);
				if (api().button("清除选中区域", {})) {
					std::vector<Region> regions;
					for (std::size_t i = 0; i < kRegionCount; ++i) {
						if (state.testRegions[i]) {
							regions.push_back(kRegions[i]);
						}
					}
					if (regions.empty()) {
						Message("请至少选择一个区域。");
					} else {
						Submit(TestClear{ state.selectedActor, std::move(regions), snapshot.epoch });
					}
				}
				api().endDisabled();
				api().endDisabled();
			}

		}

		void __stdcall renderMenu()
		{
			try {
				const auto snapshot = Menu::snapshot();
				SyncDraft(snapshot);
				Text("体液覆盖");
				TextWrapped("设置跨存档共用。点击“应用并保存”后持久保存；“仅应用”只修改本次运行的设置。");
				if (api().collapsingHeader("覆盖层与效果", 1 << 5)) RenderSettings();
				if (api().collapsingHeader("衰减与清洗", 1 << 5)) RenderDecay();
				RenderSettingsActions(snapshot);
				if (api().collapsingHeader("角色管理与运行状态", 0)) RenderMaintenance(snapshot);
			} catch (const std::exception& e) {
				logs::error("Menu render failed: {}", e.what());
			} catch (...) {
				logs::error("Menu render failed with unknown exception");
			}
		}
	}

	bool Menu::available()
	{
		return api().connect();
	}

	bool Menu::install()
	{
		static bool installed = false;
		if (installed) {
			return true;
		}
		auto& framework = api();
		if (!framework.connect()) {
			logs::warn("SKSE Menu Framework is not available; SLACS2 menu is not registered");
			return false;
		}

		framework.addSectionItem("体液覆盖/设置", &renderMenu);
		installed = true;
		logs::info("Registered SLACS2 Chinese settings menu with SKSE Menu Framework");
		return true;
	}

	void Menu::publish(MenuSnapshot a_snapshot)
	{
		MenuState::publish(std::move(a_snapshot));
	}

	MenuSnapshot Menu::snapshot()
	{
		return MenuState::snapshot();
	}

	IngressResult Menu::submit(MenuAction a_action)
	{
		return Runtime::instance().enqueue(RuntimeCommand{ std::move(a_action) });
	}
}
