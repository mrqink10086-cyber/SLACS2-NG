# P+ 事件接收桥与 C++ 接入研究

以下结论以本地 P+ 脚本、SKSE 2.2.6、项目 CommonLibSSE-NG、Devious Curses 源码及 SLACS2 原 ESP 记录研究为依据。已选接收方式是复用原 Main Quest 和挂载脚本 `SLACS_Main`，由薄 Papyrus Event 转发到 C++，保留按高潮来源和互动关系分配污渍的业务目的。本轮更新规划，尚未实现脚本、DLL 或 ESP。

## 结论与新约束

- **唯一自动加层入口采用 P+ 的 `SexlabOrgasmSeparate(Form source, int tid)`。** 它在通过 P+ 高潮条件与冷却检查后发送，同时提供来源 actor 和当前动画过程的 `tid`。
- SKSE 确实提供原生 ModEvent sink；其公开 dispatcher 接收 `Form.SendModEvent` 的固定四字段事件。P+ 上述高潮通知使用 `ModEvent.Create/Push/Send` 的变长参数路径，后者没有经过这个 dispatcher。
- **采用 Devious Curses 的接收链：`RegisterForModEvent → Quest Papyrus Event → Global Native 函数 → C++ 队列`。** 复用原 ESP 的 Main Quest `SLACS_Main_Q` 和其挂载脚本名 `SLACS_Main extends Quest`，将该脚本改成薄桥；不新建事件 Quest，也不新增独立 native 声明脚本。菜单继续使用中文 Menu Framework。
- 同一 Quest 订阅 typed `HookAnimationStart/HookAnimationEnd(int tid, bool hasPlayer)`，只转发生命周期；加层仍只来自 Separate，不再同时处理 ApplyCum、MGEF 和自定义 tracking 三种业务入口。
- `tid` 就是当前独立动画过程的身份。内部按 `tid` 管理活动上下文，不要求 P+ 额外提供永久 ID 或 generation。读档/新游戏仍会作废本会话未完成工作。
- `SLACS_Main.psc` 同时声明三个 `Global Native` 函数，DLL 在 `SLACS_Main` 类注册它们；交付一份重新编译的 `SLACS_Main.pex`。清除 Main VMAD 中旧业务属性；不为保留类名继续绑定 MCM、SpellHandler、FormList 或其他旧依赖。
- SLACS2 NG 自己的状态、持久化、计时和游戏操作由 C++ 承担；不再依赖 StorageUtil、JContainers、DbFormTimer、PO3。菜单按用户要求使用中文 SKSE Menu Framework，不引入 MCM。

本地 SKSE 版本头明确写有 `CURRENT_RELEASE_RUNTIME = RUNTIME_VERSION_1_6_1170` 与 `CURRENT_RELEASE_SKSE_STR = "2.2.6"`：[skse_version.h](<E:/source/repos/skse64/skse64_common/skse_version.h:61>)。这给版本固定的研究提供了源码基线，实际安装 DLL 的版本和二进制身份仍须在 S1 核实。

## SKSE 两条发送路径

### 简单 `Form.SendModEvent`

1. `papyrusForm::SendModEvent` 构造 `SKSEModCallbackEvent`，调用 `g_modCallbackEventDispatcher.SendEvent(&evn)`：[PapyrusForm.cpp](<E:/source/repos/skse64/skse64/PapyrusForm.cpp:274>)。
2. 事件结构仅含 `eventName`、`strArg`、`numArg`、`sender`：[PapyrusEvents.h](<E:/source/repos/skse64/skse64/PapyrusEvents.h:492>)。
3. SKSE 的 Papyrus handler 也是这个 dispatcher 的消费者，再把固定字段送入 Papyrus 注册表：[PapyrusEvents.cpp](<E:/source/repos/skse64/skse64/PapyrusEvents.cpp:273>)。
4. 插件可通过 `SKSEMessagingInterface::GetEventDispatcher(kDispatcher_ModEvent)` 取得该源：[PluginAPI.h](<E:/source/repos/skse64/skse64/PluginAPI.h:218>)。

CommonLib 已封装 `SKSE::GetModCallbackEventSource()`；初始化时向同一 dispatcher 取源：[API.cpp](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/src/SKSE/API.cpp:113>)、[取源函数](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/src/SKSE/API.cpp:211>)。对应事件类型是 `SKSE::ModCallbackEvent`：[Events.h](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/SKSE/Events.h:12>)。接收器使用 `RE::BSTEventSink<SKSE::ModCallbackEvent>`，通过该源的 `AddEventSink` 接入即可，无需创建本插件的 Papyrus 监听脚本。

不能从这个 API 推断“全部 ModEvent 都会广播给 C++”。其覆盖范围由实际发送端决定。

### typed `ModEvent.Create/Push/Send`

`Create` 创建匿名命名空间内的 `PapyrusModEvent`，将对象存入 SKSE 持久对象存储并返回 handle：[PapyrusModEvent.cpp](<E:/source/repos/skse64/skse64/PapyrusModEvent.cpp:245>)。`Push<T>` 按 handle 找回对象并追加参数：[Push](<E:/source/repos/skse64/skse64/PapyrusModEvent.cpp:282>)。

`Send` 的实际路径是：

```text
TakeObject<PapyrusModEvent>(handle)
  → g_modCallbackRegs.ForEach(eventName, eventObject)
  → registry->QueueEvent(reg.handle, callbackName, eventObject)
  → eventObject.Copy(...) 打包变长参数
  → delete eventObject
```

证据分别是 [Send 与销毁](<E:/source/repos/skse64/skse64/PapyrusModEvent.cpp:255>)、[注册项调用器](<E:/source/repos/skse64/skse64/PapyrusModEvent.cpp:123>)、[Copy 按参数类型打包](<E:/source/repos/skse64/skse64/PapyrusModEvent.cpp:80>)。这个发送函数没有调用 `g_modCallbackEventDispatcher.SendEvent`。即使监听相同事件名，普通 `ModCallbackEvent` sink 也不会收到这条 typed 通知。

这里的事件原名也没有自动成为接收器的首参数；`Copy` 将参数数组原样复制，事件名插入代码已经被注释：[PapyrusModEvent.cpp](<E:/source/repos/skse64/skse64/PapyrusModEvent.cpp:85>)。不能套用简单 ModEvent 的四参数回调签名。

## P+ 单一路径的选择

### `SexlabOrgasmSeparate` 满足来源与场景身份需求

P+ 的 `DoOrgasm` 先处理 `_hasOrgasm`、是否允许高潮和冷却检查，之后递增 `_OrgasmCount`：[sslActorAlias.psc](<E:/source/repos/sexlab P+/Source/Scripts/sslActorAlias.psc:878>)。发送顺序是：

```text
ApplyCumFX(source)（适用性由 P+ 决定）
→ SexLabOrgasm(Form source, int enjoyment, int orgasmCount)
→ SexlabOrgasmSeparate(Form source, int tid)
→ TrackedEvent(TRACK_ORGASM)
```

源码连续给出这条顺序和参数：[sslActorAlias.psc](<E:/source/repos/sexlab P+/Source/Scripts/sslActorAlias.psc:921>)。因此选 `SexlabOrgasmSeparate` 即可覆盖该函数内的成功高潮通知，并直接取得 source 与 tid；无需再次 TrackActor，也不需要从 actor 反查“最近一个场景”。

`SexLabApplyCumFX` 的参数则是 `target, source, type`：[sslThreadModel.psc](<E:/source/repos/sexlab P+/Source/Scripts/sslThreadModel.psc:2233>)。它已经是 P+ 自身 cum 效果分类后的输出，不等同于旧 SLACS2 按互动关系计算七区域的目的，也没有直接携带 tid。本设计不把它作为第二条自动加层入口。

### 简单 `OrgasmStart` 不能等价替换

P+ 的 `SetupThreadEvent` 同时发送 typed `Hook<name>(int tid, bool hasPlayer)` 与简单 `<name>`；简单事件的 `strArg` 是 `thread_id`，sender 为发送它的 thread quest：[sslThreadModel.psc](<E:/source/repos/sexlab P+/Source/Scripts/sslThreadModel.psc:2256>)。因此直接 sink 能收到 `OrgasmStart`，但它不具备相同业务语义：

- `OrgasmStart` 只在所示 legacy/scene stage 分支发送，先发通知，再触发全部或特定 alias 的 `DoOrgasm`：[sslThreadModel.psc](<E:/source/repos/sexlab P+/Source/Scripts/sslThreadModel.psc:1209>)。
- 享受值更新也能直接调用 `DoOrgasm`，不经过上面的 stage 事件：[sslActorAlias.psc](<E:/source/repos/sexlab P+/Source/Scripts/sslActorAlias.psc:766>)。
- 简单 `SSL_ORGASM_Thread<tid>` 是 `TriggerOrgasm` 发出的命令：[sslThreadModel.psc](<E:/source/repos/sexlab P+/Source/Scripts/sslThreadModel.psc:1236>)；alias 收到后调用 `DoOrgasm`，仍可能在允许性/冷却检查处返回：[sslActorAlias.psc](<E:/source/repos/sexlab P+/Source/Scripts/sslActorAlias.psc:875>)。

所以 `OrgasmStart`/`HookOrgasmStart` 缺少确切高潮来源，也可能漏掉享受值高潮；`SSL_ORGASM_Thread` 不能证明最终成功高潮。为了直接使用普通 sink 而改用它们，会改变原功能目的。

## 已选：Quest 薄 Event 转发到 native

### 参考实现与接收契约

`Form.RegisterForModEvent(eventName, callbackName)` 只检查 form 和两个字符串，然后把 callbackName 与接收 form 注册到 `g_modCallbackRegs`，并未检查回调必须由 Papyrus 字节码实现：[PapyrusForm.cpp](<E:/source/repos/skse64/skse64/PapyrusForm.cpp:250>)。注册表生成接收对象的 handle、保存参数，并为成功插入的 handle 增加引用：[PapyrusEvents.h](<E:/source/repos/skse64/skse64/PapyrusEvents.h:103>)。typed `Send` 随后调用 `QueueEvent(reg.handle, callbackName, eventObject)`，完整保留 `Form source, int tid` 两个参数：[PapyrusModEvent.cpp](<E:/source/repos/skse64/skse64/PapyrusModEvent.cpp:123>)。

Devious Curses 用生成的 `DCurses_MCM` 脚本声明普通 Papyrus Event，再从 Event 调用 `DCursesLib` 的 native 静态函数：[订阅](<E:/source/repos/devious-curses-main/CODEGEN.py:572>)、[Event 接收并调用 native](<E:/source/repos/devious-curses-main/CODEGEN.py:590>)、[C++ native 注册](<E:/source/repos/devious-curses-main/src/Sex.cpp:633>)。SLACS2 NG 复用原 Main Quest `SLACS_Main_Q` 和挂载脚本类名 `SLACS_Main`，仅借用这条接收结构并转发参数。完整参考见 [Devious Curses 研究](devious-curses-events.md)。

实施订阅表固定如下：

| typed 事件 | `SLACS_Main` 的普通 Papyrus Event | 调用的 `SLACS_Main` Global Native 函数 |
| --- | --- | --- |
| `SexlabOrgasmSeparate` | `OnPPlusOrgasm(Form sourceActor, int tid)` | `NotifyOrgasm(sourceActor, tid)` |
| `HookAnimationStart` | `OnPPlusStart(int tid, bool hasPlayer)` | `NotifySceneStart(tid, hasPlayer)` |
| `HookAnimationEnd` | `OnPPlusEnd(int tid, bool hasPlayer)` | `NotifySceneEnd(tid, hasPlayer)` |

`SLACS_Main.psc` 中的声明签名：

```papyrus
Function NotifyOrgasm(Form sourceActor, int tid) Global Native
Function NotifySceneStart(int tid, bool hasPlayer) Global Native
Function NotifySceneEnd(int tid, bool hasPlayer) Global Native
```

复用原 Main QUST `SLACS_Main_Q` 上的 `SLACS_Main extends Quest`。该 PSC/PEX 包含 `RegisterEvents`、`UnregisterEvents`、三个普通 Event 和三个 `Global Native` 声明；DLL 在 Papyrus 类 `SLACS_Main` 注册静态 native 函数。只交付重新编译的 `SLACS_Main.pex`，不新增 Quest 记录或第二份 native 声明 PEX。事件桥只转发原始参数；参与者查询、玩家/随从筛选、有向交互判定和所有状态更新放在 C++。`hasPlayer` 仅作输入信息，不在 PSC 中用它过滤掉仅含随从的过程。

### 注册与运行职责

CommonLib 的 `RegisterFunction` 构造 `RE::NativeFunction` 并调用 `BindNativeMethod`：[NativeFunction.h](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/N/NativeFunction.h:174>)。VM 调用时由 native 包装器解包参数并调用 C++ stub：[NativeFunction.h](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/N/NativeFunction.h:81>)。在本方案中，DLL 将静态函数注册到 `SLACS_Main` 类，Papyrus 的 `RegisterForModEvent` 负责 Quest 的订阅，两项职责分别执行。

1. 在 DLL 的 Papyrus 注册阶段，将 native 函数注册到 `SLACS_Main`；在既有 `SLACS_Main_Q` 上挂载重编译后的 `SLACS_Main.pex`，编译时使用 Skyrim/SKSE 对应脚本声明。
2. 新游戏与成功读档后，C++ 通过 `SLACS_2_Plus_SCO.esp` 和 local FormID `0x000D62` 查找 `SLACS_Main_Q`，再经 VM 调用其 `RegisterEvents`，待绑定对象可用后重建这三项订阅；未就绪采用有界重试。`0x000D62` 来自原 ESP 记录 FormID `04000D62` 的低 24 位，是文件记录证据，尚未在游戏内验证；不得把旧 load order 字节 `04` 硬编码进查找值。参考项目的新档/读档入口见 [plugin.cpp](<E:/source/repos/devious-curses-main/plugin.cpp:281>)、[Quest 注册方法调用](<E:/source/repos/devious-curses-main/src/Scripting.cpp:337>)。
3. Quest Event 调用对应 native；native 核对参数与本会话状态，复制 source 的 FormID、tid 和内部 sequence 后入队。Runtime 在游戏执行阶段通过 ActorRegistry 绑定 ActorId；Form 裸指针留在 native 同步调用内。
4. C++ 在核验后的游戏执行阶段查询 P+、更新业务和外观；桥中不保存 actor 状态、不设置定时器、不等待任务完成。
5. 进入加载时停止本会话处理并取消已采集请求；注销仅操作本 Quest 的三项订阅，读档后按上述流程恢复。epoch 作废已采集工作；对尚在 VM 排队、未进入 native 的迟到通知，S2/S3 验证加载和过程结束行为。

本路线使用标准 Quest 脚本和同类中的 native 声明。直接将 native 方法作为 ModEvent callback、运行时构造类型元数据、拦截发送点均退出当前实施计划。

## 直接调用 P+ 自带 API

不新增 Papyrus 业务层，并不要求复制 P+ 自己的动画框架。C++ 可以通过 CommonLib 的 VM 接口调用已经存在的 P+ 方法，再用 C++ callback 接收结果：`FindBoundObject`、`DispatchMethodCall` 及 callback 接口都已声明在 [IVirtualMachine.h](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/I/IVirtualMachine.h:99>)、[IStackCallbackFunctor.h](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/I/IStackCallbackFunctor.h:13>)。

可使用的 P+ 现有查询包括：

| 数据 | P+ 实现与用途 |
| --- | --- |
| tid → thread | `SexLabFramework.GetThread(tid)` 委托给 ThreadSlots：[SexLabFramework.psc](<E:/source/repos/sexlab P+/Source/Scripts/SexLabFramework.psc:120>)；ThreadSlots 返回 `Threads[tid]`：[sslThreadSlots.psc](<E:/source/repos/sexlab P+/Source/Scripts/sslThreadSlots.psc:43>)。 |
| actor 列表 | `sslThreadModel.GetPositions()` 是 P+ 已注册的 native 方法：[sslThreadModel.psc](<E:/source/repos/sexlab P+/Source/Scripts/sslThreadModel.psc:47>)。 |
| actor 性别分类 | `GetActorSex(actor)` 经当前 position/alias 查询：[sslThreadModel.psc](<E:/source/repos/sexlab P+/Source/Scripts/sslThreadModel.psc:64>)。 |
| 互动区域与方向 | `GetInteractionTypes` / `HasInteractionType` 转发到 P+ collision 方法：[sslThreadModel.psc](<E:/source/repos/sexlab P+/Source/Scripts/sslThreadModel.psc:392>)；类型包含 HandJob、FootJob、Facial，参数有明确方向：[SexLabThread.psc](<E:/source/repos/sexlab P+/Source/Scripts/SexLabThread.psc:129>)。 |
| 当前 scene/stage | `GetActiveScene`、`GetActiveStage` 都是已有 native 方法：[sslThreadModel.psc](<E:/source/repos/sexlab P+/Source/Scripts/sslThreadModel.psc:14>)。 |

本地 P+ 包含 `SexLabUtil.dll` 与脚本声明，未找到配套 C++ 头文件/实现。现有证据足以规划通过 VM 调用其已存在 API；不足以声明 P+ 有可直接链接的公开 C++ singleton、消息接口或导出查询函数。

**查询时序：** VM dispatch 接收调用后，结果通过异步 callback 返回；多个 getter 读取的是各自执行时的数据。CommonLib 的转发见 [IVirtualMachine.cpp](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/src/RE/I/IVirtualMachine.cpp:83>)。native 复制 source/tid，Runtime 创建请求并在必要查询完成后一次提交业务命令；scene end/load 撤销未完成请求。Papyrus Event 和查询都有排队延迟，共用 Quest 也不能确定不同 Event 的执行顺序。S2/S3 测量到达顺序、延迟和结束取消；Runtime 保持本插件已经采集的同 tid 请求顺序。

## tid 与生命周期

P+ API 明确将 thread ID 描述为当前 thread 的唯一身份：[SexLabThread.psc](<E:/source/repos/sexlab P+/Source/Scripts/SexLabThread.psc:6>)。本地实现有 15 个固定 slot，安装时设定各自 tid，空闲后可以再次使用：[sslThreadSlots.psc](<E:/source/repos/sexlab P+/Source/Scripts/sslThreadSlots.psc:6>)、[PickModel](<E:/source/repos/sexlab P+/Source/Scripts/sslThreadSlots.psc:27>)、[SetTID](<E:/source/repos/sexlab P+/Source/Scripts/sslThreadSlots.psc:127>)。这不改变用户的建模要求：活动动画过程以 tid 识别，slot 可重用也不要求为业务制造永久场景 ID。

Quest 接收 typed `HookAnimationStart/HookAnimationEnd`，沿相同 native 转发链管理该 tid 当前上下文。P+ 的 start/end 调用点见 [AnimationStart](<E:/source/repos/sexlab P+/Source/Scripts/sslThreadModel.psc:1551>)、[AnimationEnding/AnimationEnd](<E:/source/repos/sexlab P+/Source/Scripts/sslThreadModel.psc:1714>)；`SetupThreadEvent` 创建 typed `Hook<name>`：[发送实现](<E:/source/repos/sexlab P+/Source/Scripts/sslThreadModel.psc:2256>)。生命周期只负责开始、结束、取消查询和释放缓存。一次场景可发生多次合法高潮，同一 actor 在同一 tid 的多条 `SexlabOrgasmSeparate` 都必须允许处理；不能按 actor 短时间窗或“每 tid 只一次”吞掉事件。

## S2/S3 验证与交付

1. 固定实际 Skyrim 1.6.1170、SKSE 2.2.6 与 P+ 安装包；确认脚本源与运行 PEX 的对应关系。
2. 复用 `SLACS_2_Plus_SCO.esp` 中 EDID 为 `SLACS_Main_Q`、原文件 FormID 为 `04000D62`（local ID `0x000D62`）的 QUST，以及 VMAD 脚本名 `SLACS_Main`；清理其旧属性绑定，挂载重编译的唯一 `SLACS_Main.pex`。核对 DLL 类名、函数名、参数顺序与静态签名；记录 ID 来自原文件，尚非游戏验证结果。
3. 验证 `RegisterForModEvent → Quest Event → native → 队列`，记录三类通知的参数、次数与执行阶段；确认多人、玩家和仅随从过程均能进入 C++ 筛选，其他监听者正常收到原事件。
4. 覆盖享受值高潮、legacy/scene 高潮、多人场景、多次高潮、冷却拒绝和 back-to-back 场景，确认业务只从成功的 Separate 通知进入一次。
5. 验证 Event 与既有 P+ VM 查询的执行阶段、返回延迟和 scene/stage 变化，确认通知后查询的业务语义。
6. 验证新游戏、连续读档、失败读档、过程结束、tid 复用下的重订阅和 pending callback 取消；重登记不能造成重复调用。日志以 tid、source 和内部请求状态诊断，不添加上游永久 ID 契约。
7. 核对安装包的 DLL、原 Main Quest/VMAD 和一份重新编译的 `SLACS_Main.pex` 一致；移除不再使用的 MCM、SpellHandler Quest、别名及其旧 VMAD/PEX 绑定，不留下保留名字的空壳；菜单仅使用中文 Menu Framework，脚本编译与运行均无 SkyUI/MCM、StorageUtil、JContainers、DbFormTimer、PO3 调用。

接收结构已经选定。后续验证聚焦薄桥在目标 1170 组合中的参数交付、生命周期和业务时序，C++ 继续独占状态、算法与持久化职责。
