# multiple_thread_validproxy — 长期记忆

> **角色**: 项目深度知识参考手册。记录 AGENTS.md（概览）、`docs/context.md`（会话上下文）和 `docs/bugfix/`（事件日志）不覆盖的知识。
>
> **边界说明**:
> - `docs/context.md` → 轻量上下文锚点（外部项目路径、配置位置、当前会话引用）
> - `docs/project-knowledge.md` → 深度参考（测试规范、错误标准、架构决策、工具模式）
> - AGENTS.md → 概览与入口（CLI 参数、模块映射、核心规则）
> - `docs/bugfix/` → 单次 Bug 修复记录（移出 context.md §三 后归档至此）
>
> **更新机制**: 每次会话结束时，检查是否有新的架构决策/跨模块知识需要追加至 §7。单模块修复不进此文件。确保不引入 AGENTS.md 和 `context.md` 已有内容。

---

## 1. 测试规范与数据库配置（简要）

> 完整测试规范（数据库、回归命令、日志级别）见 **AGENTS.md §构建与测试** 和 **AGENTS.md §CLI 参数速查**。

**核心要点**:
- 回归测试库: `test/guiNDB_empty.db`（711 profiles, 8 SubIDs）
- 大规模验证: `test/guindb.db`（53,837 profiles, 44 SubIDs）
- 测试配置: `bin/test_config_empty.json` → `test/guiNDB_empty.db`
- 测试代码始终使用 `test/` 下数据库，**不得操作** `bin/worker/guindb.db`

---

## 2. 代理测试错误级别分类标准

| 分类 | LogLevel | 说明 | 示例 |
|------|----------|------|------|
| **网络错误** | `INFO` | 代理连通性测试失败（预期内） | curl 超时、DNS 解析失败 |
| **配置生成错误** | `ERR` | 代理配置不完整 | `checkRequired` 失败、无效协议 |
| **注入 outbound 错误** | `ERR` | xray 注入失败 | `addOutbound` 非零退出码 |

**原则**: 网络错误是正常行为（代理无效），不视为异常。配置/注入错误表示系统自身问题，使用 `ERR`。

---

## 3. Google Test 规范

### 源码位置
- 本地路径: `E:\eclipse_workspace\googletest`
- 集成方式: CMake `add_subdirectory(E:/eclipse_workspace/googletest)`

### 断言对照
| 非致命 | 致命 | 说明 |
|--------|------|------|
| `EXPECT_EQ` | `ASSERT_EQ` | 相等 |
| `EXPECT_NE` | `ASSERT_NE` | 不等 |
| `EXPECT_TRUE` | `ASSERT_TRUE` | 条件真 |
| `EXPECT_GT` | `ASSERT_GT` | 大于 |
| `EXPECT_LT` | `ASSERT_LT` | 小于 |

- 优先使用非致命 `EXPECT_*` 收集所有失败
- 致命 `ASSERT_*` 用于前置条件检查（nullptr、DB 连接）
- `tests/` 下非 GTest 文件（如 `test_curl_easy_handle.cpp`）使用 `cassert` + 自包含 `main()`

### 测试数据库约定
- 测试代码始终使用 `test/guindb.db` 或 `test/guiNDB_empty.db`
- 不得操作 `bin/worker/guindb.db`（生产数据库）

---

## 4. 错误分析记录（2026-05-14）

> 完整分析（4 类错误模式、根因、核心结论）见 **`docs/reports/error-report_20260514.md`**。

**摘要**: 日志扫描发现 4 类错误 — REALITY 字段缺失、Network 数据污染、校验策略不一致。根因在导入阶段校验不足。

---

## 5. 计划跟踪 — 三文档协同模型

| 文档 | 用途 | 生命周期 |
|------|------|---------|
| `docs/plans/YYYY-...-plan.md` | 单个计划详细描述 | draft → completed/cancelled |
| `docs/plans/project-plans-tracker.md` | 全量索引 + 进度总览 | 长期 in_progress |
| `docs/project-knowledge.md` | 长期记忆、关键决策、架构知识 | 持久存在 |

### 生命周期流程
```
创建计划文档 (draft)
  → 更新跟踪文档（添加索引行）
  → 审核计划
  → 执行代码变更
  → 更新计划文档 (completed/cancelled)
  → 更新跟踪文档（同步索引状态）
  → 更新本文件（记录关键决策）
```

---

## 6. 常见工具模式

### ID 生成 (`Utils.cpp:21-32`)
```cpp
std::string utils::generateUniqueId();  // 19 位数字, 4|5 开头
```
用于 `Profileitem::indexid`、`Subitem::id`（批量导入）。

### URL 工具 (SubitemUpdaterV2)
- `isValidUrlFormat(url)` — http/https + 有效域名
- `hasValidPath(url)` — 检查路径部分
- `extractRemarksFromUrl(url)` — 从 URL 提取 remarks

### 弃用提示
- `SubitemUpdaterV2::log()` 已弃用，新代码使用 `Logger::write()`

---

## 7. 最近关键决策记录

> 仅记录 **影响架构或跨模块** 的决策。单模块 Bug 修复见 `docs/bugfix/`。

| 日期 | 决策 | 影响 |
|------|------|------|
| 2026-06-11 | 稳定性加固：启用 ASAN/UBSan、MiniDump 崩溃兜底、cppcheck 静态分析、gcov 覆盖率 | 调试与构建基础设施增强 |
| 2026-06-18 | SubitemUpdaterV2 重构：2422→1198 行，提取 SubscriptionParser/Deduplicator/Importer/SubscriptionUpdater 四个类 | 代码组织改善，单一责任原则，维护性提升 |
| 2026-05-05 | 移除 `blacklisted` 冗余字段，改用 `consecutive_failures < threshold` 实时计算 | ProfileExItem 简化 |
| 2026-05-05 | 去重统一为 5 字段键：`Address+Port+ConfigType+Id+Network` | 避免 VMess/VLESS 误判 |
| 2026-04-16 | XrayManager 改为单例模式 | ProxyFinder/AppController 复用同一实例 |
| 2026-04-28 | CoreType NULL 处理：空值时 `sqlite3_bind_null()` | v2rayN 兼容性 |
| 2026-05-06 | 废弃 `update_subscription` 和 `check_auto_update_interval` 字段 | ConfigReader 清理 |

---

## 8. 调试与稳定性规则

> 基于 `docs/reports/2026-06-11-Debug-Tools-Assessment.md` 和 `docs/plans/2026-06-11-Plan-Stability-Hardening-v1.0.md`。  
> 快速命令速查见 **AGENTS.md §4.2 调试命令**。

### 8.1 Sanitizer 构建（调试内存/UB 问题）

```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build --parallel 8
# 运行单元测试，ASAN/UBSan 会拦截内存泄漏、越界、UB
ctest -V
```

- `ENABLE_SANITIZERS=ON` 启用 `-fsanitize=address,undefined -fno-omit-frame-pointer`
- 仅非 MSVC（MinGW）生效
- 默认 OFF，只在 Debug 调试时按需开启

### 8.2 Crash Dump（MiniDumpWriteDump）

- `include/CrashHandler.h` + `src/CrashHandler.cpp`
- 在 `main_gui.cpp` 入口处通过 `crash::installHandler()` 注册
- 发生未处理异常时自动写入 `bin/temp/crash_<YYYYMMDD_HHMMSS>.dmp`
- 使用 `MiniDumpWithDataSegs` 级别（含全局数据段用于诊断）
- 依赖 `dbghelp.lib`（已通过 CMake `-ldbghelp` 链接）

### 8.3 静态分析（cppcheck）

```powershell
.\scripts\run_static_analysis.ps1
```

- 扫描 `src/` 下全部 .cpp 文件
- 启用 `warning,style,performance,portability` 检查集
- C++17 标准、win64 平台
- 需预先安装 cppcheck：`choco install cppcheck` 或 `scoop install cppcheck`

### 8.4 覆盖率（gcov/gcovr）

```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
cmake --build build --parallel 8
.\scripts\run_coverage.ps1
```

- `ENABLE_COVERAGE=ON` 启用 `--coverage`（gcov 插桩）
- 脚本先尝试 `gcovr`，回退至 `lcov + genhtml`
- 报告输出至 `temp/coverage/coverage.html`
- 需预先安装：`pip install gcovr`

### 8.5 调试工具选择矩阵

| 症状 | 工具 | 构建命令 |
|------|------|---------|
| 段错误 / 崩溃 | MiniDump → `bin/temp/crash_*.dmp`（自动） | 普通 Debug 即可 |
| 内存泄漏 / 越界 | ASAN (`-fsanitize=address`) | `cmake -B build -DENABLE_SANITIZERS=ON` |
| 未定义行为 | UBSan (`-fsanitize=undefined`) | 同上（与 ASAN 同时启用） |
| 条件竞争 | Logger TRACE 级别 + 代码审查 | `config.json`: `log.file_level: "TRACE"` |
| 逻辑错误 | 单元测试 + Logger DEBUG | `ctest -V` |
| 代码质量 | cppcheck | `.\scripts\run_static_analysis.ps1` |
| 测试盲区 | gcov/gcovr 覆盖率 | `cmake -B build -DENABLE_COVERAGE=ON` |

### 8.6 ASAN 输出解读

ASAN 捕获到错误时会打印调用栈并终止进程，典型输出模式：

```
==PID==ERROR: AddressSanitizer: heap-use-after-free on address ...
    #0 0x... in Foo::bar() src/Foo.cpp:42
    #1 0x... in main src/main.cpp:10
0x... is located 0 bytes inside of 4-byte region [...]
freed by thread T0 here:
    #0 0x... in operator delete(void*, unsigned long long)
    #1 0x... in Baz::~Baz() src/Baz.cpp:20
previously allocated by thread T0 here:
    #0 0x... in operator new(unsigned long long)
    #1 0x... in Baz::Baz() src/Baz.cpp:10
```

**关键信息**: 错误类型（`heap-use-after-free` / `heap-buffer-overflow` / `stack-buffer-overflow` / `leak`）、调用栈、分配/释放位置。每个 `#N 0x... in Function() file:line` 指向源码位置。

### 8.7 崩溃 Dump 分析流程

1. 启动程序，复现崩溃
2. 检查 `bin/temp/crash_*.dmp` 是否已生成
3. 使用 WinDbg 或 Visual Studio 打开 dump 文件:
   - **Visual Studio**: 双击 .dmp → 运行"仅限本机"调试 → 查看崩溃线程调用栈
   - **WinDbg**: `.ecxr` → `kb` (查看调用栈) → `dv` (查看局部变量)

### 8.8 Logger 深度调试

- 将 `config.json` 中 `log.file_level` 设为 `"TRACE"` 获取最详细日志
- `log.network_failures: true` 输出网络失败日志（默认 `true`）
- 严重性能问题时切回 `"INFO"`，避免日志 I/O 干扰排查
