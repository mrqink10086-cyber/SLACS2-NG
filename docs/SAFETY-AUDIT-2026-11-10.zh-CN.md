# SLACS2 NG 线程、内存、栈与指针安全审计

日期：2026-11-10。审计基准：本地提交 `77f2cce`，包含 1.0.4 功能与性能改动。

结论：发现一项已复现的配置读取内存放大问题。普通运行路径未发现有充分证据支持的数据竞争、释放后使用、双重释放、越界或无界递归。退出/卸载、初始化菜单读取及 VM 对象借用仍有依赖引擎时序的条件风险，不能由宿主测试直接判定安全。

本轮审核覆盖 `src` 内插件自有 C++ 实现、头文件、Papyrus 桥及复制的 RaceMenu 接口声明。三个 Luna xhigh 子代理分别审阅运行时/回调、覆盖层/菜单/引擎对象、Core/配置/持久化；根代理复核发现并运行动态检查。`lib` 内第三方实现没有逐行全审；仅按需读取 CommonLib 的句柄、VM 和 SKSE task 包装。

## 1. 已确认：配置读取可产生大量内存分配（P2）

位置：`src/config/Settings.cpp:632-650`、`src/config/Settings.cpp:261-278`、`src/config/Settings.cpp:293-320`。

`Settings::load()` 先把整个文件读入 `ostringstream`，再用 `stream.str()` 复制文本，随后完整构建 JSON 树，最后才检查设置字段。解析器限制嵌套深度，但没有文件字节数、节点数或数组/对象宽度限制。因此，未知字段中的宽数组也会被完整分配，然后才返回无效配置。启动时的调用点是 `src/runtime/Runtime.cpp:230-231`。

使用独立宿主探针生成 `{"unknown":[0,0,...]}`，数组包含 100,000 个零，实测：

| 项目 | 结果 |
| --- | ---: |
| 输入文件 | 200,013 字节 |
| `Settings::load()` 内累计 C++ `new` 申请字节 | 109,657,636 |
| 最大单次 `new` 申请 | 12,166,479 字节 |
| 最终结果 | `SettingsLoadStatus::Invalid` |

累计申请量包含分配后已释放的内存，不是峰值占用或 RSS。本探针使用有限规模输入，没有尝试耗尽系统内存。它证明了配置被拒绝前仍存在显著分配放大；继续增加输入可以增加内存压力，启动路径没有相应输入预算。

建议按固定配置 schema 设置读取字节数与解析节点/容器上限，在大量分配之前拒绝超限输入。未知及兼容忽略字段也应受同一预算约束。本次审核没有修改该产品路径。

复现源码与输入位于本机 `artifacts/safety-asan/settings_width_probe.cpp`、`artifacts/safety-asan/wide-settings.json`，属于忽略的审计产物。探针统计期间只调用 `Settings::load()`，不把生成输入文件的开销计入数字。

## 2. 条件性生命周期与线程风险

### 2.1 已排队 SKSE task 的销毁边界

位置：`src/runtime/BackgroundPump.cpp:28-35`、`src/runtime/BackgroundPump.cpp:87-105`、`src/runtime/Runtime.h:138-148`、`src/runtime/Runtime.cpp:607-625`。

后台线程提交的 SKSE task 捕获 `this`，执行时访问 `taskPending_`、`stopping_` 和 `tick_`。`stop()` 只 join 后台线程，不排空已经交给 SKSE 的 task。Runtime 是静态单例，且没有找到 `Runtime::shutdown()` 的生产调用点；成员逆序析构也会先销毁多项运行状态，再销毁 BackgroundPump。

若 SKSE task 与对象销毁重叠，或在对象销毁后继续运行，就可能访问失效对象。普通游戏运行期间单例持续存活，尚未找到游戏内触发该问题的路径。应先确认退出阶段队列是否还会执行；若计划支持运行中卸载，则需要独立的任务存活令牌/排空协议，并在成员析构前完成停机。

### 2.2 RaceMenu install callback 的借用对象

位置：`src/integration/OverlayAdapter.cpp:196-199`、`src/integration/OverlayAdapter.cpp:221-225`、`src/integration/OverlayAdapter.cpp:249-260`。

install callback 从全局 `atomic<OverlayAdapter*>` 读取裸指针后访问依赖函数。析构只清空原子指针，没有注销 callback，也没有等待正在执行的 callback。清空指针无法保护已经读取了旧指针的调用；DLL 卸载时，外部保存的回调函数地址也可能失效。

接口提供 `UnregisterInstallCallback`。上游源码在调用 install callbacks 和注销时使用同一回调集合锁；若实施修复，应在 RaceMenu 接口仍存活的明确停机阶段注销，并协调正在执行的调用，而不是在任意静态析构顺序下直接调用借用接口。[RaceMenu 上游实现](https://github.com/expired6978/SKSE64Plugins/blob/master/skee64/OverlayInterface.cpp)

本插件未发现正常运行中销毁该 adapter 的路径；实际风险取决于 callback 线程和退出/卸载顺序。这是条件风险，未复现为普通游戏运行崩溃。

### 2.3 初始化写入与菜单依赖读取

位置：`src/runtime/Runtime.cpp:224-251`、`src/runtime/Runtime.cpp:648-650`、`src/menu/Menu.cpp:482-500`。

`initialize()` 在设置初始化标记后写入 `dependencies_`。菜单直接取得该对象的 const 引用并遍历其 vector，没有采用 MenuSnapshot 的不可变发布机制，也没有共用锁。

若菜单 render 与 DataLoaded 初始化并发，会形成 vector 读写竞争和迭代器失效风险。当前没有验证实际安装版本中这两个入口是否重叠，因而不把它列为已确认数据竞争。上游 Menu Framework 的 RenderUIHook 会调用渲染入口，但仅凭该实现不足以证明它与本机 DataLoaded 串行。[Menu Framework 上游渲染入口](https://github.com/QTR-Modding/SKSE-Menu-Framework-3/blob/master/src/Hooks.cpp)

建议让依赖报告也随不可变菜单快照发布，消除对两个回调必须串行的依赖；或在确认引擎线程契约后记录该约束。

### 2.4 VM 返回 Actor 的即时解引用

位置：`src/integration/PPlusAdapter.cpp:527-540`。

Positions callback 从 VM value 中 `Unpack<RE::Actor*>()`，判空后立即读取 FormID，不跨 callback 保存裸指针。但这里没有独立强句柄；若 VM 不保证返回变量持有对象，且 callback 能与 Actor 删除并发，才会构成悬挂访问。

本地包装未给出足以判定该并发条件的对象存活契约，没有发现实际 UAF。需要确认 VM value/handle 在 callback 期间的存活保证；必要时先取得拥有型 actor handle，再读取对象信息。

## 3. 有界的大存档内存峰值

位置：`src/persistence/SerializationCodec.cpp:15-17`、`src/runtime/Runtime.cpp:512-550`、`src/core/Core.cpp:577-586`。

codec 上限为 64 MiB 记录、100,000 个 actor、每 actor 七区域各最多五层。读档时，decoded staging 保持存活，Runtime 又复制 `persisted.regions`；Core 构建状态和投影目标时还会分配数据。密集且大量 FormID 能成功解析的极端记录可能产生较高瞬时内存。

这是有界容量和复制开销关注，不是已证实的泄漏；本轮未测最坏情况下的进程峰值。后续优化可先消除 staging 到 restored 的可转移复制，再用有效大记录测量峰值，决定是否需要调整上限。不能把宿主持久化微基准的分配量直接视为游戏读档 RSS。

## 4. 已检查的安全机制

| 范围 | 检查结果 |
| --- | --- |
| Ingress | 队列与 reservation 受 mutex 保护；释放锁后调用 wake，未发现反向锁序。 |
| PPlus 查询 | callback gate 保护 adapter，shared_ptr 持有请求状态；token/epoch 拒绝陈旧结果；callback 只更新状态并请求 pump，下一次 VM 派发仍由 Runtime 执行。 |
| Runtime / Scheduler | 生产调用经 pump 串行；到期项保留 epoch/revision 校验；有 item 与墙钟预算。 |
| ActorRegistry / 候选 | map 访问加锁；解析后由 ActorLease/局部拥有型 handle 持有 actor；3D root 也由拥有型指针持有。 |
| 菜单快照 | 原子发布 shared_ptr<const MenuSnapshot>，渲染期间保留句柄；旧快照在发布新快照后仍存活。直接依赖引用的例外见 2.3。 |
| Overlay / Effects | 容量 clamp、区域/索引边界、槽位所有权与缓存迭代已核对；未发现越界、迭代失效或双重释放。 |
| Core / 持久化 | 区域/层数/ID/数值和记录剩余字节有校验；未发现可证整数溢出或越界读取。 |
| 配置栈深度 | JSON 解析在 `a_depth > 32` 时拒绝递归；未发现无界递归。宽度和内存预算问题见第 1 节。 |
| Papyrus / QuestBridge | native 参数指针同步消费；QuestBridge 有删除检查和失效路径。未找到 VM 派发同步重入造成死锁的证据。 |

## 5. 动态验证

- 全量 MSVC 构建及 DLL、Papyrus、资源校验通过；12 组 release 检查全部通过。
- Clang 20 AddressSanitizer：S1、S5/S6、persistence、Overlay、S8、actor candidates 六组通过；额外开启 `ASAN_OPTIONS=detect_stack_use_after_return=1` 后仍全部通过。
- ASan 并发探针：4 个 reader 与 2,000 次菜单快照发布交叠，完成 106,977 次读取，无不一致或 ASan 报告；另以 4 个 producer、容量 128 的队列交付 40,000 条命令，验证数量和 sequence 顺序通过。
- 配置宽数组分配探针复现第 1 节问题。
- 构建 DLL 的 PE 元数据包含 DYNAMIC_BASE、HIGH_ENTROPY_VA、NX_COMPAT 及非零 SecurityCookie。这些元数据不代替源代码或生命周期检查。

ASan 没有报告覆盖到的越界和失效访问；它不是数据竞争检测器。本轮没有运行 TSan，也未在真实 Skyrim/RaceMenu/VM 中执行读档、退出和 Actor 删除并发压力测试。宿主检查使用测试 stub，不能验证真实 DLL ABI 或引擎线程承诺。

Overlay 测试曾在创建系统 Temp 下的可选贴图夹具时收到 `Access is denied`；将测试 TMP/TEMP 指向工作区 `artifacts/test-temp` 后通过，临时诊断代码已移除。这不是本轮发现的产品内存崩溃。

## 6. 优先顺序

1. 优先限制配置读取及解析预算，解决已复现的分配放大。
2. 依赖报告并入菜单不可变快照，使初始化读取不依赖外部串行保证。
3. 在真实引擎中核对 task 排空、RaceMenu callback 注销和 VM 对象存活契约，再按结果完善停机协议。
4. 用有效大 co-save 测量峰值，并消除可转移的读档复制。

本轮新增审计报告和忽略的宿主探针产物；产品代码保持审计基准 `77f2cce`。
