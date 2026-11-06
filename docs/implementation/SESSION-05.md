# SESSION-05 — S8 中文菜单/配置与 S9 打包验收

日期：2026-11-06
固定方案：Skyrim SE 1.6.1170、CommonLibSSE-NG、新档结构、P+ typed 事件、RaceMenu、SKSE Menu Framework 3.10、中文界面。

## 1. 本次结论

S8、S9 和影响交付的集成修复已经落地并完成构建、自动检查和打包。**没有任何 1170 游戏内用例被执行**，因此交付状态为：

- **构建完成**：DLL、PEX、ESP、264 个贴图、配置文件、中文字体/字符串、安装目录、zip 和清单均已生成并核对。
- **待游戏验收**：菜单动作实机贯通、配置重启/读档、多人/仅随从、tid 复用、连续换档、NPC/3D、七区域/26 效果、RaceMenu/SlaveTatsNG 共存。
- **游戏验收通过**：本次无项目适用（没有可用的 1170 运行环境）。

本次新增的关键修复是暂停感知活动时钟：菜单框架 `FreezeTimeOnMenu=true` 时 `RE::BSTimer::runTimeMS` 仍会增长，原实现会让 120 活动秒清洗在菜单打开期间流逝。`ActiveClock` 只在 `pauseCount==0` 且 `freezeTime==false` 时累计，并覆盖 32 位回绕。

## 2. S8 实现状态

### 2.1 菜单数据与状态

新增：

- `src/menu/MenuTypes.h`：`SettingsApplyStatus`、`SettingsWriteStatus`、`SettingsFeedback`、`TrackedActorRow`、`DiagnosticRow`、`MenuSnapshot`。
- `src/menu/MenuState.h/.cpp`：`atomic<shared_ptr<const MenuSnapshot>>` 发布/读取；UI 永远拿只读拥有值，不接触 `CoreState`、actor 句柄或 3D 对象。
- `src/menu/MenuText.h/.cpp`：区域、模块、诊断码、设置问题、依赖状态的中文文本。
- `src/menu/Menu.h/.cpp`：`install/publish/snapshot/submit` 和三个页面。

`MenuSnapshot` 每个 pump 发布一次，包含 epoch、递增 revision、`sessionReady`、完整 `Settings`、`SettingsFeedback`、角色行（ActorId、FormId、显示名、七区域层数、`has3D`、`visualDegraded`）和最近诊断。显示名由 Runtime 通过 `TESForm::LookupByID<Actor>` + `GetDisplayFullName()` 缓存为拥有字符串；UI 不查询引擎。

### 2.2 三个中文页面

注册路径与文本：

- `体液覆盖/设置`
- `体液覆盖/衰减与清洗`
- `体液覆盖/状态与维护`

框架 ABI：锁定已安装的 3.10.0.0，直接使用其已验证导出 `AddSectionItem` 与 cimgui `ig*`，在 `Menu.cpp` 内声明最小函数指针表。没有直接包含 3.14 源码树的 `resources/SKSEMenuFramework.h`，避免把未安装版本的 ABI 带入 3.10 运行时；连接失败时记录日志并在 `kDataLoaded` 重试一次。

页面 1：覆盖层总开关、透明度 0–100%、Mega、女性来源、女性目标限制、效果法术开关、1/3 级阈值、来源类别、高级起始槽位。提供“应用并保存”“仅应用”“还原未应用修改”。

页面 2：自然衰减、每层间隔（0.1–24 游戏小时）、雨水加速、120 活动秒整体清洗/关闭、自定义秒数；说明睡眠/等待/快速旅行按日历结算、只有连续室外降雨才计入、菜单暂停不消耗活动秒、游泳立即全清、LIFO。

页面 3：从快照读取角色列表、七区域层数、诊断；提供清除该角色全部覆盖、刷新显示、依赖/资源状态；折叠高级槽位和测试加层区（区域、1–5 层、Normal/Mega）。测试加层与正常加层共用同一 Core 上限、LIFO 和定时清洗。

`Menu::submit` 只把 `MenuAction` 放入 Ingress 并返回中文状态；按钮回调不做文件、VM、RaceMenu 或队列等待。所有动作携带快照 epoch，Runtime 用 `epoch()!=action.epoch` 拒绝换档后的旧动作。

### 2.3 Runtime 动作

`Runtime::processMenuAction`（`src/runtime/Runtime.cpp`）：

- `ApplySettings`：完整校验 → 无效则 `Rejected` + `SettingsRejected` 诊断；有效则按旧设置 `settleAllWithOldSettings`，替换 `settings_`，触发 `CoreOp{SettingsChanged{}}` 并投影 ChangeSet。`saveToDisk=true` 才调用 `Settings::save`；保存失败保留新设置，反馈 `write=Failed`，并给出“设置已应用，保存失败……”中文文本。
- `ClearActor`：清除全部七区域。
- `TestClear`：清除选中区域。
- `TestAdd`：选中区域、1–5 层、Normal/Mega，与正常路径共用 Core 规则。
- `RefreshActor`：只重放当前 Core 目标到外观/效果，不修改层。
- 所有 actor 动作先校验 `coreState_.actors` 中仍存在该 ActorId，否则 `Menu/ActorUnavailable`；不会用测试命令凭空创建 actor。

`Runtime::publishMenuSnapshot()` 在 `pump()` 末尾执行；`appendMenuDiagnostics()` 对连续相同诊断去重并保留 32 条；`recordPumpTiming()` 每 60 秒输出性能累计；`recordRequestLatency()` 记录 P+ 请求从提交到完成的时间。`Menu::install()` 在 `kPostPostLoad` 调用，并在 `kDataLoaded` 再试一次（幂等），避免框架 DLL 加载时序导致页面缺失。

### 2.4 Settings 持久化

`src/config/Settings.h/.cpp`：

- 配置路径：`Data/SKSE/Plugins/SLACS2NG.json`，UTF-8，schemaVersion 1。
- `load`：剥离 UTF-8 BOM；缺失 → `MissingUsingDefaults` + 默认值且不自动写盘；解析/未知字段/非法枚举/越界 → `Invalid`，不发布半成品；成功 → `Loaded`。
- `validate`：间隔有限且 >0、层数上限 5、清洗秒数有限且 >0、透明度 [0,1]、阈值 1..5 且 1 级 ≤2 级、槽位范围 ≤250、Body 四个子区间不重叠。
- `save`：先校验；写 `<path>.tmp` → flush/close → `std::filesystem::rename`；失败时删除临时文件并返回 `IoError`。写盘内容为 ASCII，天然是合法 UTF-8。
- `operator==` 用于菜单判断草稿是否真的变化。

## 3. S9 中文资源与打包

### 3.1 中文资源

- `package/Data/SKSE/Plugins/Fonts/NotoSansCJKsc-Regular.otf`（16,437,364 字节），来源 `https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf`，OFL 1.1。
- `NotoSansCJKsc-OFL.txt`（4,301 字节），来源 `https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/LICENSE`。
- `SKSEMenuFramework.ini`：`PrimaryFont=NotoSansCJKsc-Regular.otf`、`EnableChinese=true`、`FreezeTimeOnMenu=true`、`ToggleKey=F1`、字号 18/30/48。
- `SKSEMenuFrameworkStrings.json`：覆盖框架 3.10 的全部 13 个键，菜单外壳/导航/设置页中文。
- `tools/verify_chinese_font.py`：无第三方依赖的 OTF/TTF cmap（format 4/12/6）解析器，扫描 `Menu.cpp`、`MenuText.cpp`、框架字符串和 README；`--strict` 缺字即失败。本次输出 `Chinese glyph coverage OK (NotoSansCJKsc-Regular.otf, 4 sources)`。

安装包会覆盖框架自带 ini/strings；README 已标注备份要求。框架 DLL 不随包分发。

### 3.2 构建期核对

`tools/package.py` 在每次构建后执行：

- ESP：结构与 VMAD（QUST `0x04000D62`、脚本 `SLACS_Main`、26 MGEF/SPEL、FLST、旧记录清理）。
- 贴图：七区域 264 个 DDS。
- 菜单资源：配置 schemaVersion、ini 中文开关/字体、13 个字符串键、字体/许可证/缺字。
- Papyrus：`SLACS_Main.pex`，0 错误 0 警告；PEX 必含/禁用字符串。
- SKSE 元数据：精确 `RuntimeCompatibility{1,6,1170,0}`。
- 把构建 DLL 放入 `package/Data/SKSE/Plugins/SLACS2 NG.dll`。

### 3.3 发布工具与产物

新增 `tools/release.py`：从 `package/Data` 生成安装目录、zip 和 SHA-256 清单。

产物：

- 安装目录：`E:/source/repos/SLACS2 NG/artifacts/SLACS2NG-1.0.0-rc1/`
  - `Data/`（272 个文件）
  - `README.zh-CN.md`
  - `RELEASE-NOTES.zh-CN.md`
  - `FILE-MANIFEST.json` / `FILE-MANIFEST.txt`
- 归档包：`E:/source/repos/SLACS2 NG/artifacts/SLACS2NG-1.0.0-rc1.zip`（1,281,038,571 字节，276 个条目，264 个 DDS 以 store 方式保存）
- 清单：`artifacts/SLACS2NG-1.0.0-rc1/FILE-MANIFEST.json`（274 个载荷文件，1,285,305,664 字节）

关键文件：

| 文件 | 大小 | SHA-256 |
| --- | --- | --- |
| `Data/SKSE/Plugins/SLACS2 NG.dll` | 2,105,344 | `469035d3133978913a368a1bf4ab1c73a40fbce71c54369d4b1d133dcc83ad8f` |
| `Data/Scripts/SLACS_Main.pex` | 1,198 | `35b32bd52de26ece058c75732ac8fd50945d4119119071b63c2ca2220a90ebd8` |
| `Data/SLACS_2_Plus_SCO.esp` | 25,850 | `77477cfe385a67e3c7e8c487b60450cb94f549913aeca1523063f97d6669976e` |
| `artifacts/SLACS2NG-1.0.0-rc1.zip` | 1,281,038,571 | `cec61f3ffd9d485ac4cf4354615997a04d6e39bfe29034f2b82ec0b7aa418945` |

## 4. 修改入口

新增：

- `src/menu/MenuTypes.h`
- `src/menu/MenuState.h/.cpp`
- `src/menu/MenuText.h/.cpp`
- `src/menu/Menu.h/.cpp`
- `src/runtime/ActiveClock.h`
- `tests/s9_checks.cpp`
- `tools/release.py`
- `package/Data/SKSE/Plugins/SLACS2NG.json`
- `package/Data/SKSE/Plugins/SKSEMenuFramework.ini`
- `package/Data/SKSE/Plugins/SKSEMenuFrameworkStrings.json`
- `package/Data/SKSE/Plugins/Fonts/NotoSansCJKsc-Regular.otf`
- `package/Data/SKSE/Plugins/Fonts/NotoSansCJKsc-OFL.txt`
- `docs/release/RELEASE-NOTES.zh-CN.md`
- `tests/s8_checks.cpp`（S8 前序新增）

修改：

- `src/runtime/Runtime.h/.cpp`：菜单快照/反馈/诊断/显示名/性能计数、`processMenuAction`、`publishMenuSnapshot`、`Menu::install` 生命周期、`ActiveClock`、`saveTo` 使用 `lastObservedTime_`。
- `src/config/Settings.h/.cpp`：`operator==`、BOM、临时文件写盘。
- `xmake.lua`：`set_encodings("utf-8")`、`slacs_s8_checks`、`slacs_s9_checks`。
- `tools/package.py`：菜单/字体/配置核对，去 fontTools 依赖。
- `package/README.zh-CN.md`：S8/S9 最终说明。
- `src/integration/Papyrus.cpp` 未改；S1/S2 契约保持。

## 5. 公开接口

与 `docs/INTERFACES.zh-CN.md` 第 11 节一致，未改变已有 Core/Runtime 契约：

```cpp
struct ApplySettings { Settings value; bool saveToDisk; SessionEpoch epoch; };
struct ClearActor { ActorId actor; SessionEpoch epoch; };
struct RefreshActor { ActorId actor; SessionEpoch epoch; };
struct TestAdd { ActorId actor; std::vector<Region> regions; std::uint8_t count; TextureVariant variant; SessionEpoch epoch; };
struct TestClear { ActorId actor; std::vector<Region> regions; SessionEpoch epoch; };
using MenuAction = std::variant<ApplySettings, ClearActor, RefreshActor, TestAdd, TestClear>;

struct TrackedActorRow { ActorId actor; FormId formId; std::string displayName; std::array<std::uint8_t, 7> layers; bool has3D; bool visualDegraded; };
struct DiagnosticRow { RuntimeDiagnostic diagnostic; std::string text; };
struct SettingsFeedback { SettingsApplyStatus apply; SettingsWriteStatus write; std::vector<SettingsIssue> issues; };
struct MenuSnapshot { SessionEpoch epoch; Revision revision; bool sessionReady; Settings settings; SettingsFeedback settingsFeedback; std::vector<TrackedActorRow> actors; std::vector<DiagnosticRow> diagnostics; };

void Menu::publish(MenuSnapshot snapshot);
MenuSnapshot Menu::snapshot();
IngressResult Menu::submit(MenuAction action);
```

`Settings::load(path)`、`Settings::validate(settings)`、`Settings::save(path, settings)` 保持原签名；`Settings::operator==` 为新增便利接口。`ActiveClock::advance(uint32_t rawMs, bool paused)` 是纯逻辑、无 SKSE 依赖。

## 6. 构建

环境：Windows、MSVC VS2022、XMake 3.0.9、CommonLibSSE-NG 6.0.0（commit `15ad90b31b2ca09cf477c7e1a43ff5ff05af120f`）、Skyrim 1.6.1170。

```powershell
xmake f -p windows -a x64 --toolchain=msvc -y
xmake build -y "SLACS2 NG"
```

结果：`build ok, spent 2.938s`；DLL 2,105,344 字节；`tools/package.py` 输出 `package ready: E:\source\repos\SLACS2 NG\package\Data`，Papyrus 0 错误 0 警告。

## 7. 自动检查

```powershell
foreach ($t in "slacs_s1_checks","slacs_s4_checks","slacs_s5s6_checks","slacs_s7_checks","slacs_s5_persistence_checks","slacs_s8_checks","slacs_s9_checks") {
  xmake build -y $t
  & "build/windows/x64/release/$t.exe"
}
```

| 检查 | 覆盖 | 结果 |
| --- | --- | --- |
| `slacs_s1_checks` | 类型/设置/Ingress | PASS |
| `slacs_s4_checks` | Core 五层/LIFO/清理 | PASS |
| `slacs_s5s6_checks` | 序列化/调度/衰减 | PASS |
| `slacs_s7_checks` | 七区域/26 效果映射 | PASS |
| `slacs_s5_persistence_checks` | co-save 编解码 | PASS |
| `slacs_s8_checks` | 配置 roundtrip/BOM/tmp 改名/非法透明度/`operator==`/快照/中文反馈 | PASS |
| `slacs_s9_checks` | 活动时钟暂停/恢复/32 位回绕 | PASS |

附加：`py tools/verify_chinese_font.py --strict` → `Chinese glyph coverage OK`；`py tools/release.py` → 274 个载荷文件、264 DDS、zip/manifest 生成成功。

## 8. S1–S7 剩余项与集成修复

逐项复核结果：

| 项目 | 代码证据 | 状态 |
| --- | --- | --- |
| 唯一自动加层入口 | `src/papyrus/SLACS_Main.psc` 只注册 `SexlabOrgasmSeparate`；`HookAnimationStart/End` 只建立场景上下文 | 静态通过 |
| 多人/仅随从 | `PPlusAdapter.cpp` 按参与者方向/性别筛选；场景必须含玩家或队友；女性目标限制可配 | 静态通过，待实测 |
| tid 复用 | `processNativeOrgasm` 对同 tid 先 `cancelTid`，`tidOrder_`/sequence 排序 | 静态通过 |
| 异步取消 | `PPlusAdapter` 5s 超时、`cancelTid/cancelEpoch`、场景结束丢弃在途完成 | 静态通过 |
| 连续换档 | `beginSession` 递增 epoch、取消 P+/调度、清 staging；菜单动作 epoch 校验 | 静态通过，待实测 |
| NPC/3D | 3D 未就绪/重建走 `DeferredNo3D`、dirty 目标和 500ms 重试 | 静态通过，待实测 |
| 七区域/26 效果 | `OverlayAdapter` 七个 profile；`Effects` 27 项 FormID 表与组合索引 | 静态通过，待实测 |
| 配置持久化 | `Settings::load/validate/save` + Runtime 独立全局配置 | 自动检查通过，待实测重启 |
| 覆盖层共存 | 只回收 `textures\slacs2\` 自有前缀槽位，清除写默认贴图 | 待实测 |

本次已修复：

1. **暂停时间误计活动秒**（见第 1 节 `ActiveClock`）。
2. **菜单框架注册时序**：`kPostPostLoad` 失败时在 `kDataLoaded` 幂等重试。
3. **配置写盘原子性**：临时文件 + rename，失败不破坏旧文件；保存失败保留本次已应用设置并给中文反馈。

未修改但需在验收时确认的已知限制：保存快照可能晚一拍、无自动 `Runtime::shutdown`、旧 Papyrus 事件不带会话令牌、UBE 资源缺失、RaceMenu 默认槽位不足、SlaveTatsNG 共存未实测。

## 9. 1170 游戏实测

**执行状态：未执行。** 当前机器有 1.6.1170 与依赖文件，但没有可完成启动、进入场景并操作菜单的验收环境；不把静态检查冒充游戏实测。

待执行用例：

1. 新档：F1 打开三页，确认全部中文、无缺字、菜单开关和暂停恢复。
2. 单人/多人/仅随从各触发一次 `SexlabOrgasmSeparate`，确认只加一次、目标正确、七区域槽位写入。
3. 同 tid 连续场景、场景中途结束、快速连续换档，确认旧完成被丢弃。
4. 调整 RaceMenu Body/Face/Hands/Feet=27/13/9/9、`bPlayerOnly=0`，检查 NPC 与 3D 重建。RaceMenu 按 `i < iNumOverlays` 遍历，槽位 26 需要计数 27；这更正了 SESSION-04 中 26/12/8/8 的记录。
5. 修改设置“应用并保存” → 重启 → 确认生效；读档 → 确认全局配置未被存档覆盖。
6. 等待/睡眠/快速旅行、室外降雨、游泳、菜单停留 120 秒，确认清洗计时语义。
7. 与 SlaveTatsNG 同装，确认不占用其他模组槽位、清除后不残留。
8. 记录 60 秒 `Perf` 日志中的 actor 数、采样/材质提交、pump 耗时、队列深度和请求延迟，再决定是否调整预算。

## 10. 验收项对照

| 验收项 | 结果 | 证据/缺口 |
| --- | --- | --- |
| 菜单动作贯通 UI → Runtime → Core → 外观/效果 | 待游戏验收 | 代码路径完整；自动检查覆盖状态/设置，未覆盖实机 RaceMenu/法术 |
| 配置重启生效、读档保持全局设置 | 待游戏验收 | `slacs_s8_checks` 覆盖 load/save；重启/读档需实机 |
| 中文显示与反馈完整 | 构建完成 | 字体 strict 检查 + 13 个框架键 + 页面文本；实机字形渲染待验收 |
| 包内挂载、脚本、资源和依赖匹配 | 构建完成 | ESP/VMAD/PEX/264 DDS/字体/配置核对通过 |
| 验收记录对应实际产物 | 构建完成 | manifest + 哈希见第 3.3 节 |

## 11. 五次实施汇总

| 会话 | 范围 | 状态 | 关键产物 |
| --- | --- | --- | --- |
| SESSION-01 | S1/S2 运行时、类型、Quest/Papyrus 桥 | 构建完成 | `src/core`、`src/runtime`、`SLACS_Main.psc` |
| SESSION-02 | S3/S4 P+ 适配、Core、BodyVaginal 覆盖层 | 构建完成 | `PPlusAdapter`、`Core`、`OverlayAdapter` |
| SESSION-03 | S5/S6A 序列化、调度、衰减 | 构建完成 | `Serialization`、`Scheduler`、`Core::SettleActor` |
| SESSION-04 | S6B/S7 环境采样、七区域/效果 | 构建完成 | `Environment`、七区域 profile、26 SPEL |
| SESSION-05 | S8/S9 菜单、配置、中文、打包 | 构建完成，待游戏验收 | 本文件、`artifacts/SLACS2NG-1.0.0-rc1` |

最终安装内容：`E:/source/repos/SLACS2 NG/package/Data`
最终发布目录：`E:/source/repos/SLACS2 NG/artifacts/SLACS2NG-1.0.0-rc1/`
最终归档：`E:/source/repos/SLACS2 NG/artifacts/SLACS2NG-1.0.0-rc1.zip`

## 12. 剩余事项与下一次入口

剩余事项按优先级：

1. 在可用的 1170 环境执行第 9 节实机用例；只有完成并记录证据后才把对应项改为“游戏验收通过”。
2. 调整 `skee64.ini` 槽位与 `bPlayerOnly=0`；否则七区域只能部分显示。
3. 与 SlaveTatsNG 共存验证；当前只保证自有前缀与默认贴图清理。
4. 采集 `Perf` 数据后评估 `WorkBudget` 与脏视觉批量 64 是否需要调整。
5. 如后续要接 BiS/DnB，先验证外部信号，再复用现有 `ClearActor`/`TestClear` 命令；当前不声称支持。
6. 如需消除“保存快照晚一拍”，可在保存前强制一次不延迟的快照捕获；当前保存路径不等待 VM/任务/材质，保持现状。

下一次入口：`docs/implementation/SESSION-05.md` 第 9 节和发布说明中的“待游戏验收”表。
