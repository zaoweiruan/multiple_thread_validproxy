# Current Context

##  **一、基于xray、v2rayn开源项目**
- **v2rayn源码目录**: `E:\eclipse_workspace\v2rayn`
-- **关键文件**:
  - `v2rayN\ServiceLib\Handler\ConfigHandler.cs` - DedupServerList() 方法
  - `v2rayN\ServiceLib\Models\ProfileItem.cs` - 数据模型
  - `v2rayN\ServiceLib\Manager\AppManager.cs` - ProfileItems() 获取代理列表
  
- **xray源码目录**: E:\eclipse_workspace\Xray-core

## **二、项目配置文件**
- E:\eclipse_workspace\multiple_thread_validproxy\bin\confgi.json

## **三、近期修复 (2026-06-01)**
- **CMD 窗口闪烁**: `XrayApi.cpp` 使用 `_popen("cmd /c ...")` 在 GUI 无控制台模式下创建临时 CMD 窗口，导致批量测试时闪烁。修复：替换为 `CreateProcessA(CREATE_NO_WINDOW)` + Win32 管道。详见 `docs/bugfix/2026-06-01-batch-test-cmd-window-flicker.md`
- **诊断日志级别**: 10 处 `[DIAG]` 日志从 `INFO`/`DEBUG` 降为 `TRACE`。详见 `docs/reports/2026-06-01-diag-log-level-adjustment.md`