# Devious Curses 的事件接收方式

本笔记核查本地 `E:/source/repos/devious-curses-main` 的源码设计。该仓库未提供独立 PSC/PEX 成品；没有执行代码生成、编译或游戏测试。

## typed ModEvent：Papyrus Event 接收，再调用 C++

它的现成路线是绑定在 Quest 上的 `DCurses_MCM` 脚本接收事件，随后调用 `DCursesLib` 的 native 函数。

1. 新档或读档后，插件调用 `ScriptingManager().MCMRegisterModEvents()`。该函数查找 `Devious Curses.esp` 中的 MCM Quest，调用其 `DCurses_MCM.RegisterModEvents`；通用调用器通过 VM handle 查找绑定对象，再执行 `DispatchMethodCall`。参见 [加载入口](E:/source/repos/devious-curses-main/plugin.cpp:281)、[注册调用](E:/source/repos/devious-curses-main/plugin.cpp:315)、[Quest 方法调用](E:/source/repos/devious-curses-main/src/Scripting.cpp:337)、[VM 分派](E:/source/repos/devious-curses-main/src/Scripting.cpp:183)。
2. 生成的 `DCurses_MCM extends SKI_ConfigBase` 声明包含 `RegisterForModEvent("HookAnimationStart", "OnSexStart")` 和结束事件订阅；还订阅 DD orgasm 与暂停/恢复事件。参见 [脚本类型与订阅](E:/source/repos/devious-curses-main/CODEGEN.py:560)。
3. `Event OnSexStart(int tid, bool HasPlayer)` 接收原始 typed 参数；包含玩家时，调用现有 SexLab 脚本 API 获取 controller 和 `thread.positions`，再调用 `DCursesLib.SexStarted(positions)`。结束事件做同类处理。参见 [结束回调](E:/source/repos/devious-curses-main/CODEGEN.py:581)、[开始回调](E:/source/repos/devious-curses-main/CODEGEN.py:590)。
4. C++ 将 `SexStarted`/`SexEnded` 注册到 `DCursesLib`，对应实现首参是 `RE::StaticFunctionTag*`。这是 PSC 调用 native 静态函数；这些 C++ 函数并不是订阅表中的 `OnSexStart`/`OnSexEnd`。参见 [C++ 实现](E:/source/repos/devious-curses-main/src/Sex.cpp:594)、[native 注册](E:/source/repos/devious-curses-main/src/Sex.cpp:633)。

接收链为：

```text
typed ModEvent → DCurses_MCM.OnSexStart(tid, HasPlayer)
              → SexLab.GetController(tid).positions
              → DCursesLib.SexStarted(actors) → C++ P_SexStarted
```

DD 事件也采用这条桥接方式：`OnDDOrgasm(Form, Form, int)` 在 PSC 中判断是否玩家，再调用 `DCursesLib.DDPlayerOrgasm()`。参见 [DD 回调](E:/source/repos/devious-curses-main/CODEGEN.py:599)、[native 实现及注册](E:/source/repos/devious-curses-main/src/Sex.cpp:627)。

这些 PSC 内容保存在 `CODEGEN.py` 的生成模板中，最终生成 `DCurses_MCM.psc`；CMake 定义先执行 Codegen，再复制脚本并调用 PapyrusCompiler 的构建过程。因此不是没有脚本，只是脚本由生成器产出。参见 [生成调用](E:/source/repos/devious-curses-main/CODEGEN.py:617)、[构建规则](E:/source/repos/devious-curses-main/CMakeLists.txt:71)。

## 普通 ModCallback：另一条 C++ sink 路线

项目还注册了 `SKSE::ModCallbackEvent` sink，处理自己的 `dcurses_` 前缀事件。这与上面的 typed 接收分开：普通 sink 使用固定的 `eventName/strArg/numArg/sender` 数据。参见 [sink 处理](E:/source/repos/devious-curses-main/src/TESEvents.cpp:350)、[sink 注册](E:/source/repos/devious-curses-main/src/TESEvents.cpp:546)。

`src/ModEvents.cpp` 的 `CreateFunctor → Push*Functor → SendFunctor` 则通过 VM 调用 `ModEvent.Create/Push*/Send` 来发送 typed 事件；它不是 typed 事件监听器。参见 [发送调用](E:/source/repos/devious-curses-main/src/ModEvents.cpp:26)、[回调串联](E:/source/repos/devious-curses-main/src/ModEvents.cpp:94)。

## 对 SLACS2 NG 规划的意义

- 此例提供了可照着核查的“Quest 脚本订阅 → Event 接收 → native 调用”源码路径，并展示新档/读档后的重新订阅。SLACS2 NG 复用原 Main Quest `SLACS_Main_Q` 与原挂载脚本类名 `SLACS_Main`，不照搬 Devious Curses 的独立 MCM Quest。
- 此例没有监听 `SexlabOrgasmSeparate`；若用于 SLACS2 NG，仍应订阅此前选定的唯一业务事件，并接收其 `Form sourceActor, int tid`，不能用开始/结束事件代替高潮事件。
- SLACS2 NG 已选用这一接收形式：`SLACS_Main extends Quest` 改为薄 Event 桥，转发参数到 native 队列；typed HookAnimationStart/End 也由该脚本接收，仅管理生命周期。三个 Global Native 声明与 Event 放在同一个 `SLACS_Main.psc`，DLL 注册类名 `SLACS_Main`，因此只需交付重编译的 `SLACS_Main.pex`。
- 菜单使用中文 SKSE Menu Framework；业务状态、参与者/交互查询、衰减、资源管理继续放在 C++。桥不按 `hasPlayer` 筛选，由 C++ 处理符合性别和目标规则的过程，也包括没有玩家或随从参与的过程。组件与打包要求见 [主计划](../PLAN.zh-CN.md)。
