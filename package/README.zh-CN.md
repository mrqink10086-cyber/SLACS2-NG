# SLACS2 NG 交付包说明

本目录的 `Data` 是当前项目维护的安装内容，不覆盖旧包或其他模组。S8/S9 交付包含中文菜单、全局配置、七区域覆盖层、26 个效果法术、保存/读档和诊断日志。

## 运行环境

| 组件 | 固定/验证版本 | 说明 |
| --- | --- | --- |
| Skyrim SE | 1.6.1170.0 | 插件在 `SKSE::Init` 前拒绝其他运行时 |
| SKSE64 | 2.2.6（`skse64_1_6_1170.dll`） | 与 1170 匹配 |
| CommonLibSSE-NG | 6.0.0，commit `15ad90b31b2ca09cf477c7e1a43ff5ff05af120f` | 编译期依赖 |
| P+ / SexLab | 参考基线 2.18.1（`SexLab.esm` 381,243 字节） | Runtime 要求 `SexLab.esm` 和 `SexLabUtil.dll` 已加载；其他版本需自行确认 typed ModEvent 兼容性 |
| RaceMenu | 0.4.20（`skee64.dll` 0.4.20.0） | 覆盖层接口；必须启用 `RaceMenu.esp` |
| SKSE Menu Framework | 提供所需菜单导出的兼容版本（`SKSEMenuFramework.dll`） | 不锁定文件版本；缺失框架或所需导出时会记录依赖错误并停止初始化 |
| SkyUI | 随 ESP 主文件要求 | `SLACS_2_Plus_SCO.esp` 的主文件包含 `SkyUI_SE.esp` |
| 中文字体 | Noto Sans CJK SC Regular，OFL 1.1 | 随包提供字体与许可证 |

`SLACS_2_Plus_SCO.esp` 的主文件顺序为 `Skyrim.esm`、`SexLab.esm`、`SkyUI_SE.esp`、`RaceMenu.esp`，因此 RaceMenu 和 SkyUI 必须在加载顺序中启用。

## 安装

1. 确认游戏为 Skyrim SE 1.6.1170，SKSE 为 2.2.6。
2. 安装 P+（含 `SexLab.esm`、`SexLabUtil.dll`）、RaceMenu 0.4.20、兼容的 SKSE Menu Framework。
3. 将本目录 `Data` 下的内容合并到游戏 `Data` 目录：
   - `SKSE/Plugins/SLACS2 NG.dll`
   - `SKSE/Plugins/SLACS2NG.json`（全局配置，UTF-8）
   - `SKSE/Plugins/SKSEMenuFramework.ini` 与 `SKSEMenuFrameworkStrings.json`（中文界面）
   - `SKSE/Plugins/Fonts/NotoSansCJKsc-Regular.otf` 与 `NotoSansCJKsc-OFL.txt`
   - `Scripts/SLACS_Main.pex`
   - `SLACS_2_Plus_SCO.esp`
   - `Textures/slacs2/**/*.dds`（七区域共 264 个文件，约 1.2 GB）
4. 启用 `SLACS_2_Plus_SCO.esp`，确认 `SLACS_Main_Q` 的 VMAD 脚本为 `SLACS_Main`。
5. 进入游戏后按 F1 打开 SKSE Menu Framework，选择“体液覆盖/设置”，全部设置在同页分组展开。

> 本包中的 `SKSEMenuFramework.ini` 和 `SKSEMenuFrameworkStrings.json` 会替换框架自带的英文配置与字符串，以获得完整中文外壳。若你还使用其他依赖框架字符串的模组，请先备份这两个文件。

## 配置

全局配置：`Data/SKSE/Plugins/SLACS2NG.json`，schemaVersion 1，UTF-8。

- 只在菜单点击“应用并保存”时写盘；写盘采用同目录临时文件后重命名。
- 写盘失败时本次运行仍使用已应用的新设置，界面给出中文失败结果；重启后回到磁盘上的旧设置。
- 读档不会覆盖全局配置；每个存档的角色层、衰减余量和整体清除剩余时间保存在 co-save 中。
- 文件缺失时使用内置默认值，不会自动创建。
- 非法值、未知字段、重复字段和枚举错误会以中文字段说明拒绝；`SettingsFeedback` 分别报告“应用”和“写盘”状态。

## 菜单

“体液覆盖/设置”只有一个入口，以下分组在同页展开，设置共用“应用并保存”“仅应用”“撤销修改”：

- **覆盖层与效果**：覆盖层开关、透明度、Mega、女性来源、女性目标限制、效果法术及 1/3 级阈值、来源类别、高级起始槽位。
- **衰减与清洗**：自然衰减、每层间隔（游戏小时）、雨水加速、120 活动秒整体清洗或关闭；说明睡眠/等待/快速旅行、仅室外已观测降雨、菜单暂停和游泳立即清除。
- **角色管理与运行状态**：角色列表、七区域层数、清除/刷新、依赖与资源状态、最近诊断和测试加层。

菜单只读取运行时发布的只读快照并提交值命令；角色查找、显示名、配置校验、写盘、RaceMenu 和 VM 访问都由 Runtime 执行。菜单动作携带会话 epoch，切换存档后的旧动作会被拒绝。

## RaceMenu 必须调整的设置

`Data/SKSE/Plugins/skee64.ini` 由 RaceMenu 提供，本包不会覆盖。默认槽位不足以容纳七个区域：

```ini
[Overlays]
bPlayerOnly=0

[Overlays/Body]
iNumOverlays=27 ; 7..12 阴道、13..18 肛门、19..24 胸部、25..26 腿部

[Overlays/Face]
iNumOverlays=13 ; 7..12

[Overlays/Hands]
iNumOverlays=9  ; 7..8

[Overlays/Feet]
iNumOverlays=9  ; 7..8
```

RaceMenu 按 `i < iNumOverlays` 遍历，所以槽位编号 26 需要 `iNumOverlays=27`。`bPlayerOnly=1` 会限制 RaceMenu 对随从/NPC 的自动覆盖层重建。槽位不足时 OverlayAdapter 返回 `NoOwnedCapacity`，逻辑层保留，不会占用其他模组的槽位；提高槽位后脏目标会在重试周期内重新写入。

## 覆盖层与效果资源

- 七个区域均有普通与 Mega 贴图：Face、BodyVaginal、BodyAnal、BodyBreast 各 12 个索引，BodyLegs、Hands、Feet 各 4 个。
- Body 区域使用 diffuse + normal + specular；Face 只写 normal；Hands/Feet/Legs 按配置写现有贴图。
- 效果法术共 26 个组合（口腔 × 阴道 × 肛门，等级 0/1/2），FormID 从 `SLACS_2_Plus_SCO.esp` 解析。
- UBE 贴图不在本包内；缺少 `textures/slacs2_ube` 是预期状态，不影响其他区域。

## 验证状态

| 项目 | 状态 | 证据 |
| --- | --- | --- |
| 构建 DLL/PEX/ESP/资源 | 构建完成 | `xmake build -y "SLACS2 NG"`；`tools/package.py` 核对 ESP、264 张贴图、字体与 VMAD |
| 纯逻辑/时钟/序列化测试 | 构建完成 | 七个 `slacs_s*_checks.exe` 全部通过 |
| 菜单动作贯通 UI → Runtime → Core → 外观/效果 | 待游戏验收 | 代码路径、静态审核和界面文本已核对；需 1170 实机点击 |
| 中文显示、缺字、字体重建 | 构建完成 | `tools/verify_chinese_font.py --strict` 覆盖菜单/宿主字符串/README |
| 配置重启生效、读档保持全局设置 | 待游戏验收 | 单元测试覆盖 load/save/roundtrip；实机重启与读档待执行 |
| 多人/仅随从、tid 复用、异步取消、连续换档 | 待游戏验收 | S3/S5 静态审核与逻辑测试通过；P+ 实机场景待执行 |
| RaceMenu 覆盖层、NPC/3D、七区域/26 效果 | 待游戏验收 | 需要已调整 `skee64.ini` 的 1170 环境 |
| 与 SlaveTatsNG 共存 | 待游戏验收 | 仅通过自有槽位前缀和默认贴图清理避免占用；需实机共存验证 |

没有完成的游戏实测不会被标记为“游戏验收通过”。详细验证记录见 `docs/implementation/SESSION-05.md`。

## 已知限制

- 需要 RaceMenu 槽位调整；默认 6/3/3/3 且 `bPlayerOnly=1` 时只能显示部分区域。
- 未集成 BiS/DnB；没有外部信号适配器时不产生对应效果或覆盖。
- 保存快照可能比最新一次 pump 晚一拍；保存回调不等待 VM、任务或材质。
- 没有自动 `Runtime::shutdown` 消息处理；进程退出由 SKSE 回收。
- 旧 Papyrus 通知不带会话令牌；换档瞬间的极旧事件可能被 epoch 拒绝，但不会跨档加层。

## 目录结构

```text
Data/
  SKSE/Plugins/
    SLACS2 NG.dll
    SLACS2NG.json
    SKSEMenuFramework.ini
    SKSEMenuFrameworkStrings.json
    Fonts/NotoSansCJKsc-Regular.otf
    Fonts/NotoSansCJKsc-OFL.txt
  Scripts/SLACS_Main.pex
  SLACS_2_Plus_SCO.esp
  Textures/slacs2/...
```

## 第三方分发条件

- SKSE Menu Framework：按框架自身许可分发其配置格式和字符串，不重分发 DLL。
- Noto Sans CJK SC：SIL Open Font License 1.1，随包附 `NotoSansCJKsc-OFL.txt`，允许嵌入与再分发。
- P+、RaceMenu、SkyUI、SKSE：作为外部依赖，不随本包分发；用户需自行安装并遵守各自许可。
