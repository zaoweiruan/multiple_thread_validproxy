# Stability Hardening Plan — based on Debug Tools Assessment

> **Source:** `docs/reports/2026-06-11-Debug-Tools-Assessment.md`  
> **Status:** draft  
> **Created:** 2026-06-11

---

## 1. Objective

根据 `2026-06-11-Debug-Tools-Assessment.md` 中的风险缺口（内存安全 / 崩溃诊断 / 静态分析 / 覆盖率），制定可落地的稳定性加固计划，分 Immediate / Short-term / Long-term 三阶段推进。

---

## 2. Gap Summary

| Gap | Priority | Risk | Proposal |
|-----|----------|------|----------|
| AddressSanitizer | HIGH | 内存泄漏、越界 | Debug 构建启用 `-fsanitize=address` |
| UndefinedBehaviorSanitizer | HIGH | UB | Debug 构建启用 `-fsanitize=undefined` |
| Stack Unwinding / MiniDump | HIGH | 崩溃诊断 | 增加 Windows MiniDumpWriteDump + SEH |
| Static Analysis | MEDIUM | 代码质量 | 集成 clang-tidy / cppcheck |
| Coverage Analysis | MEDIUM | 测试盲区 | 增加 gcov/lcov 插桩 |

---

## 3. Task Breakdown

### T1 — CMake Sanitizer 选项（Immediate）

- 在 `CMakeLists.txt` 增加 `ENABLE_SANITIZERS` 选项（默认 OFF）
- 非 MSVC 时追加：
  - `-fsanitize=address,undefined -fno-omit-frame-pointer`
- Debug 标志强制 `-g3 -O0`
- 验证：`cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON`

### T2 — MiniDump 崩溃兜底（Short-term）

- 新增 `include/CrashHandler.h` / `src/CrashHandler.cpp`
- 注册 Windows SEH（`__try/__except`）或 `SetUnhandledExceptionFilter`
- 崩溃时调用 `MiniDumpWriteDump` 写入 `temp/crash_<timestamp>.dmp`
- 调用路径：`main_gui.cpp` 启动阶段注册

### T3 — 静态分析集成（Long-term）

- 在 `docs/` 新增 `clang-tidy` / `cppcheck` 规则清单
- CI 流水线增加静态分析步骤（如未来迁移到 GitHub Actions）
- 本地提供 `scripts/run_static_analysis.ps1` 辅助脚本

### T4 — 覆盖率插桩（Long-term）

- CMake 增加 `ENABLE_COVERAGE` 选项
- 使用 gcov/lcov 生成覆盖率报告
- 提供 `scripts/run_coverage.ps1` 运行 + 打开 HTML 报告

---

## 4. Verification

| Task | Verification Steps |
|------|-------------------|
| T1 | 以 `-DENABLE_SANITIZERS=ON` 构建并运行单元测试，观察 ASAN 输出 |
| T2 | 构造非法指针访问（临时埋点），验证 `.dmp` 生成 |
| T3 | 运行 `scripts/run_static_analysis.ps1`，确认 0 个新警告 |
| T4 | 运行 `scripts/run_coverage.ps1`，确认覆盖率报告可生成 |

---

## 5. Rollout Order

1. T1（Immediate）
2. T2（Short-term）
3. T3（Long-term）
4. T4（Long-term）

---

## 6. References

- `docs/reports/2026-06-11-Debug-Tools-Assessment.md`
- `CMakeLists.txt`
- `include/Logger.h`
- `src/main_gui.cpp`
