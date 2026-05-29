# clangd LSP 配置修复

## Issue
开发环境中 LSP（clangd）无法正常工作，IDE 界面显示 "LSPs are disabled"，所有 `.cpp` 文件满屏红色下划线。`lsp_diagnostics` 返回 515 条错误，几乎全部为 `pp_file_not_found`（找不到标准库头文件如 `<sstream>`、`<string>` 等）。

## Root Cause Analysis

**三个层面的问题叠加：**

### 1. `.clangd` CompilationDatabase 路径错误

`.clangd` 配置指向不存在的目录：
```
CompilationDatabase: build\cmake.debug.win32.x86_64.Local    ← ❌ 不存在
```

实际编译数据库位置：
```
build\compile_commands.json    ← ✅ 实际存在（34 个编译条目）
```

> 路径 `cmake.debug.win32.x86_64.Local` 可能是早期多配置生成的产物，但实际构建仅输出到 `build/`。

### 2. clangd 与编译器不匹配

- clangd 版本：**22.1.0 (x86_64-pc-windows-msvc)** — MSVC 目标
- 项目编译器：**w64devkit\bin\c++.exe (GCC 14.2.0, MinGW)** — MinGW 目标

`compile_commands.json` 中编译器为 `E:\w64devkit\bin\c++.exe`（含 MinGW 特有的 include 路径）。MSVC 构建的 clangd 无法自动发现 MinGW 的标准库路径，导致报 `pp_file_not_found`。

### 3. vcpkg/Boost 路径被解析但 MinGW stdlib 路径缺失

`compile_commands.json` 中包含 `-isystem E:/vcpkg/...` 和 `-ID:/boost_1_88_0/...`，这些路径被 clangd 正确使用（诊断中出现了 Boost 错误提示），但 MinGW 的内建 include 路径未被传递：

```
E:/w64devkit/lib/gcc/x86_64-w64-mingw32/14.2.0/include/c++/           ← 缺失
E:/w64devkit/lib/gcc/x86_64-w64-mingw32/14.2.0/include/c++/x86_64-w64-mingw32/  ← 缺失
E:/w64devkit/lib/gcc/x86_64-w64-mingw32/14.2.0/include/               ← 缺失
E:/w64devkit/x86_64-w64-mingw32/include/                               ← 缺失
```

这些路径在 GCC 编译时是内建的（无需在命令行指定），但 clangd 不知道它们的存在。

## Solution

### 修复 1：修正 CompilationDatabase 路径

```
- CompilationDatabase: build\cmake.debug.win32.x86_64.Local
+ CompilationDatabase: build
```

### 修复 2：显式添加 MinGW 标准库路径

```yaml
CompileFlags:
  CompilationDatabase: build
  Add:
    - -cxx-isystem
    - E:/w64devkit/lib/gcc/x86_64-w64-mingw32/14.2.0/include/c++
    - -cxx-isystem
    - E:/w64devkit/lib/gcc/x86_64-w64-mingw32/14.2.0/include/c++/x86_64-w64-mingw32
    - -cxx-isystem
    - E:/w64devkit/lib/gcc/x86_64-w64-mingw32/14.2.0/include/c++/backward
    - -isystem
    - E:/w64devkit/lib/gcc/x86_64-w64-mingw32/14.2.0/include
    - -isystem
    - E:/w64devkit/lib/gcc/x86_64-w64-mingw32/14.2.0/include-fixed
    - -isystem
    - E:/w64devkit/x86_64-w64-mingw32/include
```

> **注意**：`QueryDriver: E:/w64devkit/bin/c++.exe` 作为备选方案尝试过，但 MSVC 版 clangd 无法正确解析 MinGW 编译器的内建路径。显式 `Add:` 为可靠方案。

## Result

| 指标 | 修复前 | 修复后 |
|------|--------|--------|
| 总诊断数 | 515 | 32 |
| `pp_file_not_found` | 20+ | **0** |
| 受影响文件 | 全部 29 个 `.cpp` | 仅 `Logger.cpp`（真实代码问题）|
| 剩余错误性质 | 配置性误报 | 真实 C++ 代码类型兼容性问题 |

剩余的 32 条诊断为代码本身的问题（wxWidgets 跨类 `static_cast`、`std::filesystem::path` 构造函数签名不匹配等），与 LSP 配置无关。

## Changed File

`/.clangd` — clangd 配置文件
