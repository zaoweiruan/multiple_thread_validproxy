# Bugfix: bin\validproxy.exe 启动挂起（Tesseract-OCR 旧版 MinGW 运行时抢载 → ntdll 死锁）

- 日期: 2026-11-06
- 版本: v1.0
- 类型: Bugfix（RCA + 修复）
- 模块: 构建系统 / 运行时 DLL 解析（CMakeLists.txt / PATH）
- 状态: ✅ completed
- 根因归类: 构建与运行时环境（PATH 优先级 + MinGW 运行时版本不匹配）

---

## 1. 现象（Symptom）

- 用户报告 `bin\validproxy.exe` 启动后「运行出错」。
- 复现：17:26 起以系统 PATH 启动，进程驻留约 5 秒后进入死锁（GUI 窗口不出现、日志文件不创建），进程不崩溃不退出（挂起而非 AV）。
- 关键特征：**挂起时 `bin\log\ui_*.log` 不创建** → 死锁发生在 `Logger::init` 之前（加载器阶段）。

## 2. 根因（Root Cause）

validproxy.exe 由 **CLion 内置 MinGW GCC 15.2.0** 构建（`E:/clion/bin/mingw/bin/c++.exe`，`CMAKE_CXX_COMPILER`），导入以下编译器运行时 DLL：

- `libstdc++-6.dll`（26 MB，GCC 15.2.0 版）
- `libgcc_s_seh-1.dll`
- `libwinpthread-1.dll`

**PATH 解析链冲突**：

| PATH 项 | 位置 | 内容 |
|------|------|------|
| `C:\Program Files (x86)\Tesseract-OCR\` | **第 7 位** | 旧版 MinGW 运行时（`libstdc++-6.dll` 仅 2.4 MB，GCC 版本更低） |
| `e:\w64devkit\bin` | 33/58 | MinGW 运行时 |
| `d:\vcpkg\installed\x64-mingw-dynamic\debug\bin` | 49/50 | wxWidgets DLL（非 MinGW 运行时） |
| `E:\clion\bin\mingw\bin` | **不在 PATH** | 正确 GCC 15.2.0 运行时 |

**机制**：R2（禁用 z-applocal `VCPKG_APPLOCAL_DEPS=OFF`）后 `bin\` 不再被复制任何 DLL → 加载器按 PATH 顺序解析 `libstdc++-6.dll` → 命中 **Tesseract-OCR 的旧版运行时**（GCC 版本与 15.2.0 不匹配）→ 缺失入口点 → 加载器调用 `ntdll!NtRaiseHardError` 弹出「procedure entry point not found」硬错误对话框 → 该对话框在 GUI 未创建时阻塞 → 进程死锁（加载器锁被持有，任何调试器/中断均无法进入）。

**为何时好时坏**：用户某些启动环境 PATH 中正确 MinGW 目录靠前（17:18-17:20 正常）；以纯系统 PATH 启动时 Tesseract 靠前（17:26+ 挂起）。

## 3. 取证（Evidence）

### 3.1 调试器无法中断

CLion 两个 GDB（MinGW-w64 + 捆绑）**均无法中断挂起进程**（加载器锁 / 内核等待，`NtWaitForSingleObject` 持有 loader lock）。改用 **`MiniDumpWriteDump` 抓取 705 MB 转储**（`C:\Users\dsm\AppData\Local\Temp\kilo\hang_validproxy.dmp`），经 Python 脚本解析（GDB 不认 Windows 转储，无 WinDbg/cdb）。

### 3.2 转储分析结论

- **3 个线程全部位于 ntdll.dll**：
  - 2× `NtWaitForSingleObject`（等待锁）
  - 1× `NtRaiseHardError`（**正在弹出硬错误对话框**——「入口点未找到」）
- **模块列表加载了 Tesseract-OCR 的 MinGW 运行时**：
  - `libstdc++-6.dll`（2.4 MB，来自 `C:\Program Files (x86)\Tesseract-OCR\`）
  - `libgcc_s_seh-1.dll` / `libwinpthread-1.dll`（同目录）

→ 根因确证：**旧版 MinGW 运行时被 PATH 解析命中 → 入口点不匹配 → NtRaiseHardError → 死锁**。

## 4. 修复（Fix）

### 4.1 主修复：CMake POST_BUILD 复制正确 MinGW 运行时到 `bin\`

`CMakeLists.txt` 为 **validproxy**（265-276 行）与 **validproxy-cli**（404-423 行）各添加 `add_custom_command(POST_BUILD)`：

```cmake
set(MINGW_RUNTIME_DIR "${CMAKE_CXX_COMPILER}" )
get_filename_component(MINGW_RUNTIME_DIR "${MINGW_RUNTIME_DIR}" DIRECTORY)   # 编译器 bin 目录
add_custom_command(TARGET validproxy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${MINGW_RUNTIME_DIR}/libstdc++-6.dll"
        "${MINGW_RUNTIME_DIR}/libgcc_s_seh-1.dll"
        "${MINGW_RUNTIME_DIR}/libwinpthread-1.dll"
        "${CMAKE_SOURCE_DIR}/bin/"
    COMMENT "Copy MinGW runtime DLLs to bin/ (PATH has Tesseract-OCR before MinGW)")
```

**原理**：加载器优先搜索 exe 所在目录，复制后的正确 GCC 15.2.0 运行时覆盖 PATH 解析，不受 PATH 顺序影响。`copy_if_different` 幂等，增量构建零开销。

### 4.2 配套：规则 5 例外

`docs/DEV-PROCESS.md` 规则 5（禁用复制依赖 DLL）补充例外说明：MinGW 编译器运行时（`libstdc++`/`libgcc`/`libwinpthread`）是**编译器自身 C++ 运行时**，非可复制的依赖 DLL；若 PATH 早于 MinGW/vcpkg 出现**不兼容旧版**运行时，POST_BUILD 复制为必要。

## 5. 验证（Verification）

- **重配置**：`clion_load_cmake` EXIT=0；`cmake --build ... --target validproxy` → `[12/12] Linking ...; Copy MinGW runtime DLLs to bin/`。
- **bin\ 产物**：3 个运行时 DLL 就位（`libstdc++-6.dll` 26 MB / `libgcc_s_seh-1.dll` 937 KB / `libwinpthread-1.dll` 72 KB，源自 CLion MinGW bin）。
- **普通启动**：validproxy.exe → 日志创建（`bin\log\ui_20261106_175255.log`，`Build: Debug | 2026-11-06 17:52:02`），GUI 初始化 + StandaloneProxy 收养既有 xray，WS=495 MB → **死锁解除**。
- **内置 GDB 调试**（`clion_xdebug_*`）：断点 `main()`（`src/main_gui.cpp:37`）命中 → 调用栈确认 → 恢复运行 8 s 无挂起 → 日志 `ui_20261106_175638.log`（17:56:58）显示 `Logger::init completed` + `[MainFrame] StandaloneProxyEvent received, started=true`。**修复后调试器可正常控制进程**（此前挂起时完全无法中断）。

## 6. 变更文件（Files Changed）

| 文件 | 变更 |
|------|------|
| `CMakeLists.txt` | 9 行 `set(VCPKG_APPLOCAL_DEPS OFF ...)`（R2 既有）；validproxy + validproxy-cli 各一段 MinGW 运行时 DLL `POST_BUILD`（R4 新增，265-276 / 404-423 行） |
| `docs/DEV-PROCESS.md` | 规则 5 补充「MinGW 运行时例外」注释（95-117 行区域） |
| `docs/plans/2026-11-06-Plan-Cleanup-CopyWxDllsDeadCode-v1.0.md` | 追加 R1/R2（z-applocal 根因+修复）/ R3（Tesseract 死锁根因）/ R4（POST_BUILD 修复）章节 |

## 7. 关联与已知事项

- **R2 前置**：`VCPKG_APPLOCAL_DEPS=OFF` 强制禁用 z-applocal（wx 依赖 DLL 不再自动复制，规则 5 已写为 v1.3）——此变更使 `bin\` 不再有 wx DLL，是 PATH 冲突暴露的前提；wx DLL 仍经 PATH 解析（`d:\vcpkg\...\debug\bin` 第 49 位）。
- **`XrayManagerStartupTest.RealXrayStartsBounded` 失败**（xray API 端口 29601 超时、xray exit 1）——**既知环境问题**，与本次 DLL 修复无关（xray.exe 二进制本身 `-version` 正常，26.3.27）。
- **临时产物**：`C:\Users\dsm\AppData\Local\Temp\kilo\hang_validproxy.dmp`（705 MB 转储）与 `parse_dump.py` 可清理；`bin_dll_backup_20261106_debug` 备份目录已不存在（临时区丢失，不影响构建）。
- **3 个 xray 孤儿进程**（pid 1340/11224/16324）为历史遗留，与本次修复无关。
