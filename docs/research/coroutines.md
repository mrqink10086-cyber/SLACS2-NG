# 协程、定时器与生命周期依据

研究日期：2026-11-05。用途：为 SLACS2 NG 的调度选择提供依据；本文件不表示已经采用协程。

## 结论与推荐

推荐第一版使用显式状态机、集中期限队列和一个经 1170 验证的游戏任务执行入口。协程保留为后续流程组织选项。它适合表达多阶段等待，但不自动提供游戏时钟、执行线程、取消、对象存活或 RaceMenu 的安全更新阶段。

本项目需要先解决“谁调度、在哪恢复、读档如何作废、谁销毁状态”，再比较回调与 `co_await` 的写法。两种写法应服从同一份执行契约。

## 可核查的语言事实

| 事实 | 对本项目的影响 | 一手依据 |
| --- | --- | --- |
| 协程的恢复依赖 awaiter/执行环境；C++/WinRT 的上下文恢复是该库额外提供的能力 | 自定义 `Delay` awaiter 必须指定恢复执行器，不能把它当作 SKSE 游戏线程调度器 | [Microsoft：执行上下文与恢复](https://learn.microsoft.com/en-us/windows/apps/develop/cpp-winrt/concurrency-2#execution-contexts-resuming-and-switching-in-a-coroutine) |
| 捕获型协程 lambda 的闭包可能早于恢复销毁；引用参数也可能悬空 | 协程入口优先普通函数和值参数，延迟工作保存任务标识、代次和对象句柄 | [Core Guidelines CP.51–53](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp-coro-coroutines)、[MSVC C26810](https://learn.microsoft.com/en-us/cpp/code-quality/c26810?view=msvc-170)、[MSVC C26811](https://learn.microsoft.com/en-us/cpp/code-quality/c26811?view=msvc-170) |
| 跨挂起点持有锁可能造成死锁或在其他线程解锁 | 先复制输入并释放锁，再挂起；恢复后重新校验状态 | [Core Guidelines CP.52](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp52-do-not-hold-locks-or-other-synchronization-primitives-across-suspension-points) |
| 协程状态可能需要额外存储，分配可受 promise 与优化影响 | 统计存活帧与分配次数；不能宣称协程“零内存开销”或“永远在堆上” | [C++ 标准草案：协程定义](https://eel.is/c++draft/dcl.fct.def.coroutine) |
| 同步完成后的内联恢复链可能累积调用栈 | 采用队列化恢复或经验证的 symmetric transfer，禁止以递归恢复链实现无限 tick | [Microsoft：避免重入完成](https://devblogs.microsoft.com/oldnewthing/20230202-00/?p=107779) |

上述 Microsoft C++/WinRT 资料说明的是语言与库机制。本项目不因此引入 WinRT 或 COM 依赖。

## 方案比较

| 方案 | 优点 | 本项目需要承担的成本 | 建议 |
| --- | --- | --- | --- |
| 每个 actor 一组回调/定时器 | 直接对应旧版脚本 | 数量随 actor 和效果增长；取消、读档、重复注册分散 | 不选作主架构 |
| 集中期限队列 + 显式状态机 | 状态可序列化，取消与预算集中，便于虚拟时钟测试 | 需要一个有界 pump 和明确的唤醒机制 | 第一版基线 |
| 集中执行器 + 协程流程 | 多阶段等待的控制流更直观 | 仍需相同队列/执行器；额外管理 frame、恢复与销毁竞争 | 出现足够复杂的顺序流程后评估 |
| 后台线程直接运行游戏/覆盖层更新 | 可以离开调用线程执行 | 无法从这些接口推出引擎对象与 RaceMenu 操作的线程安全 | 不采用 |

## 协程候选的进入条件

以后确需引入时，应先通过以下独立验证，再将一条流程从状态机改写为协程：

1. awaiter 指定恢复入口；后台到期通知只提交任务 ID，不访问 actor、节点和材质。
2. 每个 frame 有唯一所有者；定时条目、取消通知与恢复队列不各自拥有可销毁的裸 `coroutine_handle`。
3. 取消和恢复由同一执行器串行决定，`destroy()` 与 `resume()` 不并发；先撤销所有可恢复登记，再销毁 frame。
4. 读档/回主菜单提升会话代次，旧通知即使已经进入 SKSE 队列也失效。代次检查保证语义作废，不能替代 frame 的内存所有权。
5. 不跨挂起点保存裸 `Actor*`、`NiAVObject*`、材质指针、栈引用、容器迭代器或锁；恢复时从句柄重新获取当前对象。
6. 异常被执行入口处理并记录；任务失败完成终结与资源回收，不把 C++ 异常传播过引擎/DLL 回调边界。
7. 长链同步完成、取消同时到期、重复取消、读档后旧通知和退出等待都有确定性测试。

## 时钟设计先于语法选择

- `steady_clock` 用于唤醒、超时和执行预算，避免系统时间校正改变期限。
- 可暂停的活动游戏秒用于默认 120 秒整体清除，成功加层重置 actor 的期限；暂停/菜单时停止累计。后续视觉过渡沿用该时钟。
- 已确认游戏日历用于自然衰减，包含睡眠/等待/快速旅行补算；雨水加速只累计已观测的暴露区间，不根据加载后的当前天气倒推。
- 保存逻辑状态、累计量和游戏时间锚点，不保存系统时钟的绝对时间点或协程 frame。

具体实现顺序与清洗规则见 [主计划](../PLAN.zh-CN.md)，模块接口见 [接口契约](../INTERFACES.zh-CN.md)。
