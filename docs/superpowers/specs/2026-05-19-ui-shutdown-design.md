# UI 关闭后的退出设计 (2026-05-19)

## 目标
实现界面关闭时彻底退出应用及所有子进程，确保 Xray 实例及相关子进程在退出时被正确清理，避免僵尸进程。

## 成功标准
- 关闭界面后，Xray 与所有子进程无遗留。
- 多线程场景下仍能可靠清理。
- Windows 环境无僵尸进程，退出过程可重复。
- 日志记录清晰，包含退出时间、进程列表、退出原因。

## 约束
- 目标平台：Windows。
- 保留现有进程创建与管理模式，新增退出清理模块作为独立层。

## 方案概览
- 方案 1：Windows Job Object 绑定法（推荐）
- 方案 2：退出超时的释放策略
- 方案 3：守护进程/服务式管理

本设计选择：方案 1。 

## 架构设计
- ShutdownController：关闭事件入口，捕获界面销毁/退出信号。
- WindowsJobManager：创建/维护一个全局 Job 对象，将 Xray 及相关子进程绑定到同一 Job。
- ProcessLifecycleBinding：在创建/启动 Xray 及其子进程时执行绑定，将其纳入 Job 管理。
- GracefulShutdownTimer：在退出时启动定时器，等待指定超时（如 5-10s），超时再执行强制结束。

数据流：
1) UI 关闭事件 -> ShutdownController
2) ShutdownController 调用 WindowsJobManager 执行绑定/释放
3) WindowsJobManager 关闭 Job，系统自动结束绑定的进程
4) GracefulShutdownTimer 监控并在超时后执行 kill
5) 日志记录退出过程

## 实现要点
- API/系统调用：CreateJobObjectW、SetInformationJobObject、AssignProcessToJobObject
- 结构体：JOBOBJECT_EXTENDED_LIMIT_INFORMATION、JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
- 错误处理：创建失败时退回逐个结束子进程的兜底路径
- 兼容性：仅限 Windows，其他平台保留原有退出行为。

## 错误处理与回退
- 若 Job 创建失败：逐个结束 Xray 与子进程，记录日志。
- 若 绑定失败：尝试重新绑定一次，失败则走兜底路径。
- 超时未清理：强制结束并退出应用。

## 日志与监控
- 退出开始时间、Job 对象创建与销毁状态、绑定进程列表、退出结果。

## 测试计划
- 手动测试：启动应用，触发关闭，检查 Xray 及子进程是否被结束。
- 集成测试：对模拟子进程创建、绑定与超时策略进行验证。
- 边界测试：无权限、Job 已存在、绑定重复等异常路径。

## 迁移与回滚
- 分阶段实现：1) 引入 WindowsJobObject 封装；2) 集成到退出流程；3) 增加日志与超时策略。
- 如出现严重问题，提供禁用 kill-on-close 的回滚开关。

## 风险评估
- 仅适用于 Windows。
- 需要正确处理权限与句柄遗留。
- 可能在极端异常场景下无法完全清理。

## 参考
- Windows Job Object API 文档
