# 独立代理启动前清理 (Standalone Proxy Pre-Start Cleanup) — 技术方案 v1.0

- **文档类型**: Spec / 技术方案
- **模块**: Standalone Proxy（独立代理）启动流程
- **版本**: v1.0
- **日期**: 2026-08-31
- **关联 BUG**: `docs/bugfix/` 独立代理测试失败点击关闭后，SOCKS 端口未被释放，导致后续启动其它代理提示"端口占用"。

---

## 一、问题背景

### 1.1 现象

用户报告（原文摘录，systematic-debugging 已定位）：

> 当独立代理测试失败，点击关闭后没有立刻关闭启动的代理，导致启动其它代理时显示端口占用，与前期端口探测、测试失败后关闭表现有很大差异？

经与用户确认：

- **"点击关闭"** = 连通性失败对话框 `"代理已启动但连通性验证失败，是否关闭该进程？"` 里的 **"是(Y)"** 按钮（`AppController::startStandaloneProxy`, line ~960, `wxYES_NO|wxCANCEL`）。
- 用户明确要求：
  1. **不使用 Job Object**（"前期程序没有此种问题"，拒绝新增机制）；
  2. **启动独立代理前** 进行清理，**还原前期行为**；
  3. **只清理当前 indexId 对应的** `standalone_<indexId>-*.json` 进程（用 `ProcessInspector` 按命令行定位），**不是**按 exe 名杀掉全部 `xray.exe/sing-box.exe`；
  4. **"否/取消" 保留（leave alive）分支保持现状不动**。

### 1.2 根因（已确认）

提交 `3649905`（2026-08-19）重构独立代理启动流程时：

- **移除了** 旧的启动前清理逻辑：`utils::killProcessByName(exeName)`（在用户 `MB_YESNO` 同意后，杀掉所有同名 `xray.exe/ sing-box.exe` 进程）。
- **改为**：在 `startStandaloneProxy` R1 段（line 666-672）仅做软拒绝：`if (proc::ProcessInspector::isProcessRunningWithConfig(configFileName)) { return false; }` —— 只按配置文件判定"已存在"并拒绝，**不再主动清理遗留进程**。
- `utils::killProcessByName`（`src/Utils.cpp:297-318`，`include/Utils.h:47`）因此成为**死代码**（全仓库零调用）。

后果：当某次独立代理启动（尤其连通性失败后走"否/取消"保留分支、或进程残留）留下一个仍占用 SOCKS 端口的 `xray.exe/ sing-box.exe` 进程时，后续再次启动同一 `indexId` 的代理，新进程因端口被占用而无法绑定，连通性验证失败，用户点击"是"仅对**当前**新进程句柄执行裸 `TerminateProcess`，**清理不掉遗留进程**，端口依旧被占 → 表现为"端口占用"。

> 对比：批量/池路径（`XrayInstance.cpp`）使用 Job Object + `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` 可靠杀掉整棵进程树、释放端口，但独立代理路径（`AppController`）用裸 `CreateProcessA(CREATE_NEW_CONSOLE)` 无 Job Object。用户确认**不通过引入 Job Object 修复**，而是**还原前期"启动前按需清理"行为**。

---

## 二、修复目标

在**每次启动独立代理之前**，若已存在一个使用**同一 `standalone_<indexId>-xray.json` / `standalone_<indexId>-singbox.json` 配置文件**的进程，则：

1. 按**命令行**定位到该**特定进程**（而非按 exe 名全杀）；
2. 弹出用户确认对话框（镜像旧 `MB_YESNO` 流程）；
3. 用户确认后，**结束该特定进程**，释放其占用的 SOCKS 端口；
4. 再继续正常启动新代理。

**不做**：

- ✗ 不引入 Job Object；
- ✗ 不改动"否/取消保留（leave alive）"分支；
- ✗ 不按 exe 名 `killProcessByName` 全量清理。

---

## 三、方案设计

### 3.1 定位进程（复用 `proc::ProcessInspector`）

参考 `AppController::adoptDanglingStandaloneProxies()`（line 1331）的定位模式：

```cpp
const char* exeNames[] = { "xray.exe", "sing-box.exe" };
for (const char* exe : exeNames) {
    std::vector<proc::ProcessInfo> procs = proc::ProcessInspector::enumerateByName(exe);
    for (const proc::ProcessInfo& p : procs) {
        if (p.commandLine.empty()) continue;
        std::string cfg = proc::ProcessInspector::extractConfigFileName(p.commandLine);
        if (cfg == configFileName) {
            // 命中：该进程正在使用当前 indexId 的独立代理配置
        }
    }
}
```

- `configFileName = "standalone_" + indexId + (useSingBox ? "-singbox.json" : "-xray.json")`（与 R1 段计算一致，line 666）。

### 3.2 结束进程（仅该 PID）

```cpp
HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
if (h) {
    TerminateProcess(h, 1);
    WaitForSingleObject(h, 5000);   // 等待退出，释放端口
    CloseHandle(h);
}
```

### 3.3 触发时机与确认对话框

在 `startStandaloneProxy` 的 **R1 段（line ~666-672）**：

- 替换当前"直接软拒绝 `return false`"的逻辑；
- 新逻辑：
  1. 定位所有命令行匹配 `configFileName` 的既有进程；
  2. 若**存在**：
     - 记录日志；
     - 弹出 `wxMessageBox`（镜像旧 `MB_YESNO`，如 `"发现已有独立代理进程使用该配置，是否关闭旧进程后重新启动？"`，按钮 `wxYES_NO | wxCANCEL`）；
     - **是(YES)**：结束该特定进程 → 继续启动新代理；
     - **否/取消**：保持现状，`return false`（软拒绝，行为与当前一致）；
  3. 若**不存在**：直接继续启动（`CreateProcessA`，line 842-843）。

> 说明：R1 段持锁 `standaloneMutex_`（line 644），弹模态对话框前需先解锁、再重锁，避免死锁 —— 与既有失败门（gates）的解锁/重锁模式保持一致。

### 3.4 可测试性

- `ProcessInspector` 提供 `setEnumeratorForTesting(EnumeratorFn)`：可注入返回伪造 `ProcessInfo` 的枚举器，断言定位/决策逻辑。
- 建议把"定位 + 决策"封装为**纯逻辑辅助函数**（不弹 UI、不做真实 Terminate），如 `foundExistingStandalone(configFileName)` / 返回命中 PID 列表，便于单测。
- 真实 `TerminateProcess` + 端口释放属**集成级**，可建一个用临时命令行动参数模拟 `standalone_<X>-xray.json` 的 dummy 进程做端到端验证。

---

## 四、影响范围与边界

| 项 | 说明 |
| --- | --- |
| 行为变化 | 启动独立代理前若发现同配置遗留进程，由"直接拒绝"变为"询问后清理旧进程再启动" |
| 不变化 | "否/取消"保留分支、批量池路径、Job Object 设计、`isPortAvailable`（已修复 GetExtendedTcpTable） |
| 公共 API | 不改变既有公开签名；可能新增私有辅助函数 |
| 平台 | Windows（`ProcessInspector`/`TerminateProcess`） |
| 风险 | 用户确认对话框为新增交互；需保证解锁/重锁不引入死锁 |

---

## 五、验证计划

1. **单元测试**：利用 `ProcessInspector::setEnumeratorForTesting` 注入枚举器，覆盖：
   - 无遗留进程 → 不弹窗、直接进入启动； 
   - 存在同配置进程 → 定位命中；
   - 用户选择是 → 触发结束 + 继续启动；
   - 用户选择否 → 保持软拒绝。
2. **构建 + ctest**：`cmake --build build --parallel 8` + `ctest -V` + 相关依赖套件（`test_standalone_proxy_pool`）。
3. **集成/手动**：GUI 下制造一个遗留独立代理进程，再次启动同一 `indexId`，验证提示关闭 → 关闭后端口释放 → 新代理正常启动。

---

## 六、受影响文件

- `docs/specs/2026-08-31-Spec-StandaloneProxyPreStartCleanup-v1.0.md`（本文档）
- `src/ui/AppController.cpp` — `startStandaloneProxy` R1 段（line ~666-672）；`adoptDanglingStandaloneProxies`（~1331）为定位参考
- `include/ProcessInspector.h` / `src/ProcessInspector.cpp` — 复用 `enumerateByName` / `extractConfigFileName`
- `src/Utils.cpp`（`killProcessByName` 297-318，保持死代码或后续决定删除）
- `tests/*` — 新增/调整测试
- `docs/INDEX.md`、`docs/plans/project-plans-tracker.md` — 登记
