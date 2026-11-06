# 中文 SKSE 菜单：接口、执行阶段与配置边界

更新日期：2026-11-05。本文只研究本地 `E:/source/repos/SKSE-Menu-Framework-3-master`，未改动框架、编译插件或进行游戏测试。已确认产品约束：彻底移除 MCM，使用该框架提供唯一菜单，SLACS2 NG 的页面、控件、提示与操作结果全部中文；不保留旧 MCM、Papyrus 配置或旧配置键的兼容层。

## 1. 接入结论与 1170 证据边界

**事实：**该框架自行创建 ImGui 上下文、安装 DX11/Win32 后端并调用消费方页面回调。消费方通过公开头文件包装的 DLL 导出调用，不需要为本插件再建立一套 ImGui renderer。[上下文与后端初始化](<E:/source/repos/SKSE-Menu-Framework-3-master/src/Hooks.cpp:215>)、[页面回调](<E:/source/repos/SKSE-Menu-Framework-3-master/src/UI.cpp:851>)。

**事实：**本地构建同时启用 SE/AE/VR，hook 使用三分支 relocation，并对 1.7 的一个偏移单独分支；这说明源码有多运行时接入设计，不能单凭 `ENABLE_SKYRIM_AE` 推导已经通过 1.6.1170 测试。[构建选项](<E:/source/repos/SKSE-Menu-Framework-3-master/CMakeLists.txt:62>)、[hook 安装](<E:/source/repos/SKSE-Menu-Framework-3-master/src/Hooks.cpp:89>)。本次查阅的加载入口没有精确 1170 检查。[加载入口](<E:/source/repos/SKSE-Menu-Framework-3-master/src/plugin.cpp:10>)。

**待验证：**固定实际用于 1170 的 Menu Framework DLL、其公开头文件与字体/字符串资源，验证插件加载、打开/关闭、中文显示、输入和暂停/恢复。现有证据不等于本地源码生成的 DLL 已获 1170 验证。SLACS2 NG 本身仍精确限制 1.6.1170，不跟随框架扩大支持范围。

## 2. 公开 API、版本与注册

### 2.1 使用消费方头文件

`resources/SKSEMenuFramework.h` 是消费方接口；`include/SKSEMenuFramework.h` 是框架内部的导出声明。消费方头通过 `GetModuleHandleW(L"SKSEMenuFramework")` 与 `GetProcAddress` 找函数，不通过 SKSE messaging 交换菜单对象。[动态查找](<E:/source/repos/SKSE-Menu-Framework-3-master/resources/SKSEMenuFramework.h:10>)、[内部导出](<E:/source/repos/SKSE-Menu-Framework-3-master/include/SKSEMenuFramework.h:9>)。

| 接口 | 已核实契约 | 本插件用法建议 |
| --- | --- | --- |
| `IsInstalled()` | 只检查 `Data/SKSE/Plugins/SKSEMenuFramework.dll` 文件存在 | 不能当作 DLL 已加载、所需导出存在或上下文已就绪的证明 |
| `GetMenuFrameworkVersion()` | 本地实现返回 `3.8f`；CMake 项目版本却为 `3.14.0.0` | 记录诊断值，不能把 float 当完整语义版本排序 |
| `GetMenuFrameworkAPIVersion()` | 本地实现返回 `uint32_t{1}`；无导出时 wrapper 返回 0 | 结合所需导出检查、选定头文件和 DLL 验证使用 |
| `FullPathAddSectionItem(path, callback)` | 注册完整路径；路径中未转义 `/` 是分隔符 | 一次注册固定的中文页面路径 |
| `SetSection(key)` / `AddSectionItem(menu, callback)` | wrapper 将 `key + "/" + menu` 传给同一个导出 | 与完整路径方式二选一；第一版使用完整路径更直接 |
| `Model::RenderFunction` | `void(__stdcall*)()`，无对象实例参数 | 使用进程生命周期内有效的静态入口，读取插件自己的 UI 状态 |

依据：[安装检查](<E:/source/repos/SKSE-Menu-Framework-3-master/resources/SKSEMenuFramework.h:37>)、[回调及接口签名](<E:/source/repos/SKSE-Menu-Framework-3-master/resources/SKSEMenuFramework.h:60>)、[路径 wrapper](<E:/source/repos/SKSE-Menu-Framework-3-master/resources/SKSEMenuFramework.h:148>)、[SetSection](<E:/source/repos/SKSE-Menu-Framework-3-master/resources/SKSEMenuFramework.h:265>)、[导出版本实现](<E:/source/repos/SKSE-Menu-Framework-3-master/src/SKSEMenuFramework.cpp:110>)、[CMake 版本](<E:/source/repos/SKSE-Menu-Framework-3-master/CMakeLists.txt:30>)。

**建议：**在 SKSE 插件加载完成后的单次初始化阶段取得接口并注册页面，例如 `kPostPostLoad`，避免在全局静态构造或其他插件尚未加载时调用 wrapper。公开头的多个 wrapper 用 `static auto func` 缓存首次查找结果；过早取得空指针后，随后再加载 DLL 并不使这个局部缓存自动重新解析。[缓存实例](<E:/source/repos/SKSE-Menu-Framework-3-master/resources/SKSEMenuFramework.h:149>)。框架自己也使用 `kPostPostLoad` 等待另一个插件完成接口注册，但这是其 VR helper 的实现，并非对所有菜单 API 的正式就绪承诺。[框架消息监听](<E:/source/repos/SKSE-Menu-Framework-3-master/src/plugin.cpp:19>)。

**建议：**启动时核实使用到的导出；接口不满足时记录明确的中文原因。ImGuiMCP wrapper 中的 `GetIO`、`TextUnformatted` 等会直接调用返回的函数指针，没有逐项空值回退。[GetIO](<E:/source/repos/SKSE-Menu-Framework-3-master/resources/SKSEMenuFramework.h:3839>)、[TextUnformatted](<E:/source/repos/SKSE-Menu-Framework-3-master/resources/SKSEMenuFramework.h:4474>)。仅凭 API 版本为 1 不能证明任意生成头的每个导出都在所选 DLL 中存在。

第一版只注册内嵌页面，不增加 HUD、独立窗口、输入钩子或渲染前后监听。当前 consumer 头的绘图命名空间是 `ImGuiMCP`，README 的旧例子仍写 `ImGui`，应以选定头文件为准。[命名空间](<E:/source/repos/SKSE-Menu-Framework-3-master/resources/SKSEMenuFramework.h:301>)、[README 例子](<E:/source/repos/SKSE-Menu-Framework-3-master/README.md:130>)。头文件还暴露 `AddWindowWithView` wrapper，而内部导出声明及本地实现没有对应接口；不把“头里出现了函数”当作它可用的充分证据。[wrapper](<E:/source/repos/SKSE-Menu-Framework-3-master/resources/SKSEMenuFramework.h:182>)、[内部导出列表](<E:/source/repos/SKSE-Menu-Framework-3-master/include/SKSEMenuFramework.h:12>)。

### 2.2 注册对象的归属

`AddSectionItem` 解析并复制路径，向菜单树保存函数指针；`UI::AddToTree` 用框架自己的 mutex 保护树的修改。消费方无需保持传入字符串临时缓冲区，但回调代码必须一直有效。[注册实现](<E:/source/repos/SKSE-Menu-Framework-3-master/src/SKSEMenuFramework.cpp:15>)、[树写入](<E:/source/repos/SKSE-Menu-Framework-3-master/src/UI.cpp:889>)。建议只在初始化时注册一次，读档不重复注册；页面不持有 actor、NiNode 或 RaceMenu 对象。

## 3. ImGui 上下文与线程边界

**事实：**框架的 `Render()` 在初始化标志成立后依次调用渲染前事件、后端 `NewFrame`、`ImGui::NewFrame()`、窗口/页面回调，再 `EndFrame/Render`。窗口回调同步调用页面 renderer。[帧流程](<E:/source/repos/SKSE-Menu-Framework-3-master/src/Hooks.cpp:262>)、[窗口遍历](<E:/source/repos/SKSE-Menu-Framework-3-master/src/Renderer.cpp:104>)、[页面执行](<E:/source/repos/SKSE-Menu-Framework-3-master/src/UI.cpp:851>)。因此页面回调是使用框架当前 ImGui 帧的适当入口；`kBeforeRender` 在 `ImGui::NewFrame()` 之前，不能当普通控件绘制回调。

**事实：**消费方 `ImGuiMCP` 的函数 wrapper 转发到框架 `ig*` 导出，后者调用框架内的 ImGui 实现。[wrapper 示例](<E:/source/repos/SKSE-Menu-Framework-3-master/resources/SKSEMenuFramework.h:4474>)、[导出转发示例](<E:/source/repos/SKSE-Menu-Framework-3-master/src/cimgui.cpp:55>)。上游 ImGui 源码明确说明上下文全局变量与堆跨 DLL 不共享，并且同一当前上下文的普通调用不具备线程安全性。[上下文说明](<E:/source/repos/SKSE-Menu-Framework-3-master/imgui/imgui.cpp:1205>)。

**建议：**使用框架匹配的 `ImGuiMCP` 接口绘制；不额外链接另一份 ImGui 实现来操作其对象，不自行创建/替换框架上下文，不自行调用 `NewFrame/EndFrame/Render`。ImGui、字体、draw list 与临时控件数据只在页面回调内访问，不跨线程保存指针，也不跨帧缓存 `ImFont*`：框架在字体设置改变时会清空并重建 atlas。[字体重建](<E:/source/repos/SKSE-Menu-Framework-3-master/src/Hooks.cpp:341>)。

**待实机验证：**源码能定位 hook 与同步调用顺序，不能据此证明该 UI 回调是所有引擎操作都安全的固定“主线程”。菜单还允许恢复游戏运行，暂停状态也可配置。[暂停/恢复分支](<E:/source/repos/SKSE-Menu-Framework-3-master/src/Hooks.cpp:315>)、[暂停配置](<E:/source/repos/SKSE-Menu-Framework-3-master/src/Config.cpp:45>)。不把 UI 回调当作 RaceMenu/actor3D 处理入口，也不把 UI 帧循环当作业务计时器。

UI 与业务边界建议：

1. 游戏侧有界处理入口生成纯值 `MenuSnapshot`：设置、目标名称/标识、七区域层数、依赖状态、上一操作结果。UI 快速取得完整快照，不在绘制期间持有业务锁。
2. 页面只编辑自己的草稿，点击按钮后提交值类型命令，例如“应用设置”“清除玩家覆盖”“清除所选目标覆盖”“刷新显示”。命令带 actor handle/稳定标识及当前存档 epoch，不捕获 `Actor*`、3D 指针、栈引用或 ImGui 内部指针。
3. 经 1170 验证的游戏处理阶段解析目标、校验当前 epoch/设置范围，再更新权威状态并安排 RaceMenu/法术 reconcile。UI 只显示结果，不等待该任务、不直接执行引擎操作。
4. 菜单关闭不停止自然衰减、清洗或 P+ 事件处理。读档清空旧目标与旧操作结果，页面本身仍保留单次注册。

这里的命令队列复用主计划已有入口，不新增独立线程或另一套计时器。

## 4. 仅中文需要文字与字体同时成立

### 4.1 本地默认资源尚不是中文

**事实：**框架随附的 `SKSEMenuFrameworkStrings.json` 是英文；`Translations` 先查该文件，缺少条目时回退 `_EN.json`，再回退英文 `missing translation`。[当前资源](<E:/source/repos/SKSE-Menu-Framework-3-master/public/SKSE/plugins/SKSEMenuFrameworkStrings.json:1>)、[加载与回退](<E:/source/repos/SKSE-Menu-Framework-3-master/src/Translations.cpp:45>)。框架默认主字体为 `Futura Condensed (EN)`，`EnableChinese=false`。[随附配置](<E:/source/repos/SKSE-Menu-Framework-3-master/public/SKSE/plugins/SKSEMenuFramework.ini:29>)。

**建议：**SLACS2 NG 自身直接维护一套中文文案，不做语言选择器。若“仅中文”涵盖用户打开菜单时看到的框架外壳，则发行/安装说明还须选定框架的完整中文字符串资源，覆盖所有在用条目，并选择可显示汉字的字体与 Chinese 字形设置；不能只翻译本插件页面后声称整个菜单已中文化。本轮不修改外部框架文件。

### 4.2 UTF-8、字形范围与字体文件

**事实：**ImGui 要求字符串是 UTF-8，而不是 Windows 本地代码页。[ImGui 字形说明](<E:/source/repos/SKSE-Menu-Framework-3-master/imgui/imgui.h:3181>)。框架启用 `EnableChinese` 后将 `GetGlyphRangesChineseFull()` 加入 atlas，加载 `Data/SKSE/Plugins/Fonts` 下的 `.ttf/.otf`，也从游戏 Interface 字体配置/SWF 读取字体。[字形范围](<E:/source/repos/SKSE-Menu-Framework-3-master/src/FontManager.cpp:179>)、[字体目录与格式](<E:/source/repos/SKSE-Menu-Framework-3-master/src/FontManager.cpp:40>)、[字体枚举及主字体选择](<E:/source/repos/SKSE-Menu-Framework-3-master/src/FontManager.cpp:220>)、[SWF 字体来源](<E:/source/repos/SKSE-Menu-Framework-3-master/src/SwfFontReader.cpp:1270>)。

**事实：**字形范围只决定请求哪些字符，不能让缺汉字的字体产生汉字。主字体加载失败时，框架可退回英文 Futura 或 ImGui 内置字体；fallback 成功也不证明中文显示成功。[fallback](<E:/source/repos/SKSE-Menu-Framework-3-master/src/FontManager.cpp:260>)。中文模式还将 atlas 期望宽度设为 16384，因此字体组合与字号需要在目标环境验证。[中文 atlas 参数](<E:/source/repos/SKSE-Menu-Framework-3-master/src/FontManager.cpp:213>)。

**建议：**源码与配置使用 UTF-8；MSVC 明确启用 `/utf-8`，给接收 `const char*` 的 API 传 UTF-8 窄字符串。C++20/23 的 `u8` 字面量产生 `char8_t`，不要通过随意转换掩盖类型边界。优先使用选定的含汉字默认字体，不在每个控件上动态切换字体。`PushFont` 只选择已加载字体，找不到字体会记警告；它不是运行时加载汉字字体的接口。[字体选择](<E:/source/repos/SKSE-Menu-Framework-3-master/src/FontManager.cpp:398>)。

中文验收包含页面标题、提示、格式化数字与单位、角色名称、操作失败文本、框架导航/设置/确认框；检查方框、缺字和乱码。第一版不用自由文本输入，避免为固定设置额外引入中文输入法流程。

## 5. 最小菜单规划

建议根节点为“体液覆盖”，通过三个固定页面承载已有功能目的。项目名继续使用 SLACS2 NG，页面文案全部中文。旧 MCM 的原始选项用于追溯需求，不照搬其页面、测试脚本或未实现开关。[旧选项](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_MCM.psc:94>)。

| 页面路径 | 必要内容 | 交互与约束 |
| --- | --- | --- |
| `体液覆盖/设置` | 不透明度；全体使用增强纹理；允许女性体液；启用附加效果及一级/二级层数阈值 | 复选框、滑块/数值控件；效果关闭通过核心撤销自有效果；阈值统一校验；底部“应用并保存”“还原未应用修改” |
| `体液覆盖/衰减与清洗` | 自然衰减开关、每层间隔（游戏小时）、雨中加速；120 活动游戏秒整体清除开关 | 说明睡眠/等待/快速旅行补算自然衰减，雨水仅累计已观察暴露；游泳规则显示为采样确认后全清 |
| `体液覆盖/状态与维护` | 玩家或游戏侧已选定目标的名称与七区域层数；“清除覆盖”“刷新显示”；依赖与资源可用状态 | 清除只作用本插件状态/覆盖层；刷新不新增层；目标不可用时禁用操作并给中文原因；槽位配置放折叠的“高级设置”，仅在容量策略确认后开放 |

沿用框架现有菜单入口。维护页的折叠测试区提供区域、数量与“添加测试覆盖”，复用核心命令。首版采用 LIFO、游泳即清和可关闭的 120 活动游戏秒整体清除；连续淡出、FIFO 和持续游泳清洗不显示为可用设置。

调度采样间隔和线程细节作为内部实现参数，不直接复制旧“游泳轮询秒数”等控件。可选 BiS/DnB/UBE 支持只有在相应原生适配/资源存在后才显示真实状态，不显示由旧依赖残留推断的“已启用”。

## 6. 配置与存档分工

**事实：**框架 `Config::Save()` 只保存自己的 `SKSEMenuFramework.ini`，如热键、暂停、主题和字体；公开菜单导出中没有替消费方保存业务设置的接口。[框架配置保存](<E:/source/repos/SKSE-Menu-Framework-3-master/src/Config.cpp:81>)、[公开导出](<E:/source/repos/SKSE-Menu-Framework-3-master/include/SKSEMenuFramework.h:12>)。

**实施配置：**全局设置固定保存到 UTF-8 文件 `Data/SKSE/Plugins/SLACS2NG.json`。菜单草稿在“应用并保存”时统一校验并提交；native 文件模块执行写盘，菜单分别显示应用结果和保存结果。具体动作与返回字段见 [接口契约](../INTERFACES.zh-CN.md)。

| 数据 | 保存位置/寿命 |
| --- | --- |
| 全局用户设置：不透明度、功能开关、衰减参数、已确认的槽位分配 | 本插件配置文件；不随读档回滚 |
| actor 层、时间锚点、剩余衰减/清洗进度、必要的随机纹理选择 | SKSE 版本化 co-save；与主计划的唯一权威状态相同 |
| 当前选中目标、UI 草稿、操作结果、ImGui/节点指针 | UI/运行时临时状态；不写业务存档 |
| 框架自己的热键、暂停、主题和字体 | 由框架管理；本插件不在运行时改写其全局配置 |

读档后用当前全局配置重建表现；业务状态在游戏侧完成恢复后发布新菜单快照。设置修改如何结算已有衰减进度由核心统一规定，菜单不自己补算。新版本不读取旧 SLACS MCM 配置、StorageUtil 键或 JContainers 数据。

## 7. 实施前最小验证

1. 在选定的 1.6.1170 环境确认 DLL/头文件组合、必要导出和三页注册；读档/开新档后不重复注册。
2. 按上述中文验收覆盖框架外壳与本插件文字；切换字号/重建字体后不持有失效字体对象。
3. 记录 UI 回调与业务处理入口的线程/执行阶段；验证打开、关闭、暂停和恢复游戏时命令仍通过核心处理入口执行。
4. 应用设置、清除目标、刷新外观均得到中文结果；目标消失或读档后，旧命令不会作用于新状态；文件保存与 co-save 职责分离。

这些是进入实现时的小实验与游戏验收项，不代表本轮已经通过测试。
