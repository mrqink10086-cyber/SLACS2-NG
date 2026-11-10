# SLACS2 NG

SLACS2 NG 是 SLACS 2 的原生 C++ 重写：把原来由 Papyrus、StorageUtil、JContainers、PO3 承担的体液层状态迁到 SKSE64 插件，用 C++ 容器、调度器和 SKSE 序列化实现七区域 × 五层的覆盖层业务。

- 只支持 Skyrim SE **1.6.1170**（`SKSE::Init` 之前拒绝其他运行时）。
- 复用原 `SLACS_2_Plus_SCO.esp` 的身份：Main Quest `SLACS_Main_Q`、VMAD 脚本类 `SLACS_Main`，Papyrus 只做 P+ 事件转发。
- 菜单改用 SKSE Menu Framework，单一入口、同页分组的全中文界面。
- 面向新档设计，存档格式为版本化 co-save；全局配置在 `Data/SKSE/Plugins/SLACS2NG.json`。

当前版本 **1.0.3**。本版自动使用 RaceMenu 实际加载的槽位容量，角色列表包含当前已加载的随从，并为失败重试加入限频。**尚未在游戏内验收**，详见下方“验证状态”。

## 功能

- **七区域、五层**：Face、BodyVaginal、BodyAnal、BodyBreast、BodyLegs、Hands、Feet，每区默认五层，LIFO 移除。
- **唯一自动加层入口**：P+ 的 `SexlabOrgasmSeparate`，经 `SLACS_Main` 薄桥把 `(sourceActor, tid)` 交给 native；`HookAnimationStart/End` 只维护过程上下文。
- **衰减与清洗**：自然衰减补算睡眠、等待、快速旅行经过的游戏时间；雨水只累计已观测到的室外降雨；游泳立即清洗；120 活动秒整体清洗（暂停/菜单冻结期间不累计）。
- **26 个效果组合**：口腔 × 阴道 × 肛门 × 等级（0/1/2），FormID 从 ESP 解析，索引 0 为关闭占位。
- **RaceMenu 覆盖层**：通过 `skee64` 接口读取实际加载的 INI 容量，自动分配空槽与本插件已有槽，保留其他模组占用；槽位不足时保留逻辑层并限频重试。
- **中文设置菜单**：打开“体液覆盖/设置”，在同页展开覆盖层、衰减清洗及角色管理分组，统一应用或保存。配置跨存档共用；菜单只读运行时快照并提交值命令，动作携带会话 epoch，换档后旧动作被拒绝。
- **诊断**：每 60 秒输出一行 `Perf` 统计（actor 数、环境采样、覆盖层提交、pump 耗时、队列深度、P+ 请求延迟）。

## 运行环境

| 组件 | 版本/标识 |
| --- | --- |
| Skyrim SE | 1.6.1170.0 |
| SKSE64 | 2.2.6（`skse64_1_6_1170.dll`） |
| P+ / SexLab | 参考 2.18.1（`SexLab.esm`、`SexLabUtil.dll`） |
| RaceMenu | 0.4.20（`skee64.dll`） |
| SKSE Menu Framework | 提供本插件所需菜单导出的兼容版本，不锁定 DLL 文件版本 |
| SkyUI | `SkyUI_SE.esp`（ESP 主文件要求） |

RaceMenu 的 `Data/SKSE/Plugins/skee64.ini` 决定可用容量；插件直接采用 RaceMenu 已加载的值，无需填写起始槽。完整显示七区域各五层所需的空闲容量和 INI 设置见 [安装说明](package/README.zh-CN.md#racemenu-必须调整的设置)。

## 构建

需要 [XMake](https://xmake.io) 3.0+（本机验证 3.0.9）与 MSVC 2022 / C++23：

```powershell
git clone --recurse-submodules https://github.com/mrqink10086-cyber/SLACS2-NG "SLACS2 NG"
cd "SLACS2 NG"
xmake f -p windows -a x64 --toolchain=msvc -y
xmake build -y "SLACS2 NG"
```

`after_build` 会自动执行 `tools/package.py`：编译 Papyrus 桥、核对 ESP/VMAD/264 张贴图/字体缺字/菜单字符串/1170 运行时元数据，并把 DLL 放进 `package/Data`。

宿主检查（不启动游戏）：

```powershell
foreach ($t in "slacs_s1_checks","slacs_s4_checks","slacs_s5s6_checks","slacs_s7_checks",
               "slacs_s5_persistence_checks","slacs_s8_checks","slacs_s9_checks",
               "slacs_overlay_checks","slacs_effects_checks","slacs_pplus_checks",
               "slacs_followers_checks","slacs_retry_schedule_checks") {
  xmake build -y $t
  & "build/windows/x64/release/$t.exe"
}
```

发布产物：

```powershell
py tools/release.py
```

输出为 `artifacts/SLACS2NG-1.0.3/`、同名 ZIP 和文件清单。

## 安装

1. 备齐上表依赖，确认游戏为 1.6.1170。
2. 把发布包内 `Data/` 合并到游戏 `Data/`，启用 `SLACS_2_Plus_SCO.esp`。
3. 按[安装说明](package/README.zh-CN.md#racemenu-必须调整的设置)设置 `skee64.ini` 并重启游戏；菜单会显示 RaceMenu 实际容量。
4. 进游戏按 F1 打开菜单。

完整步骤、配置语义、菜单说明与已知限制见 [package/README.zh-CN.md](package/README.zh-CN.md)。

## 仓库结构

```text
src/config       设置模型、JSON 读写与校验
src/core         七区域状态机、加层/清除/结算（纯值类型，无引擎依赖）
src/runtime      生命周期、值命令队列、pump、调度器、环境采样、轮转
src/papyrus      native 绑定与 P+ 事件桥
src/integration  P+ 适配、RaceMenu 覆盖层、效果、3D/引擎查询
src/menu         快照发布与同页分组菜单
src/persistence  co-save 编解码
tests            十二组宿主检查
tools            package.py / release.py / clean_esp.py / verify_*.py
docs             PLAN、INTERFACES、SESSION-01..05、AUDIT、RELEASE-NOTES
```

## 文档

- [实现计划](docs/PLAN.zh-CN.md)
- [接口契约](docs/INTERFACES.zh-CN.md)
- 实现记录：[SESSION-01](docs/implementation/SESSION-01.md)、[02](docs/implementation/SESSION-02.md)、[03](docs/implementation/SESSION-03.md)、[04](docs/implementation/SESSION-04.md)、[05](docs/implementation/SESSION-05.md)
- [源码审核与修复（2026-11-06）](docs/AUDIT-2026-11-06.zh-CN.md)
- [发布说明](docs/release/RELEASE-NOTES.zh-CN.md)

## 验证状态

| 项目 | 状态 |
| --- | --- |
| DLL / PEX / ESP / 264 贴图 / 字体 / 菜单资源构建 | 构建完成，`tools/package.py` 全部校验通过 |
| 十二组宿主检查（逻辑、调度、序列化、菜单、时钟、适配器、随从、重试） | 全部通过 |
| LLVM 20 AddressSanitizer | 历史检查结果见审计文档；本版宿主检查不能替代游戏内验证 |
| 菜单动作贯通 UI → Runtime → Core → 外观/效果 | 待游戏验收 |
| 读档、换档、tid 复用、异步取消 | 待游戏验收 |
| 七区域覆盖层、26 效果、与 SlaveTatsNG 共存 | 待游戏验收 |
| 实机性能预算 | 待游戏验收 |

没有完成的实机项目不会被标记为“验收通过”。审核结论与被修复缺陷的清单见 `docs/AUDIT-2026-11-06.zh-CN.md`。

## 已知限制

- RaceMenu 默认容量只能显示部分图层；Body 四区域共享身体槽位，完整容量要求见安装说明。
- 未接入 BiS / DnB：没有验证过的外部信号，不写“已支持”。
- UBE 贴图不在发布包内，缺 `textures/slacs2_ube` 是预期状态。
- 保存快照可能比最新一次 pump 晚一拍；保存回调不等待 VM、任务或材质。
- 没有自动 `Runtime::shutdown`；进程退出由 SKSE 回收。
- 旧 Papyrus 通知不带会话令牌，换档瞬间的极旧事件会被 epoch 拒绝，但不会跨档加层。

## 许可

GPL-3.0，附 Modding Exception（见 [LICENSE](LICENSE)、[EXCEPTIONS](EXCEPTIONS)）。

项目骨架来自 [libxse/commonlibsse-ng-template](https://github.com/libxse/commonlibsse-ng-template)，依赖 [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR)（6.0.0，`15ad90b`）。发布包内含 Noto Sans CJK SC（SIL OFL 1.1，随附许可证）；SKSE Menu Framework、P+、RaceMenu、SkyUI、SKSE 均为外部依赖，不再分发。
