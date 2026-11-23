---
title: "bugfix: GUI 启动崩溃（0xC0000374 堆损坏）根因 = 运行期从 PATH 加载 CRT 不匹配的 wx DLL（UCRT exe + MSVCRT wx）；2026-11-13 同类复发新变体"
type: bugfix
status: completed
date: 2026-11-23
module: build-system
version: v1.0
---

# GUI 启动崩溃根因：运行期 PATH 兜底加载 CRT 不匹配 wx DLL（UCRT exe + MSVCRT wx → 0xC0000374）

## 1. 症状

`bin\validproxy.exe`（GUI，UCRT Debug 构建）启动后崩溃，未显示主窗口。2026-11-23 多次复现（11:59:48、13:59:55、14:00:08）。CLI（`validproxy-cli.exe`）运行正常。退出码 `0xC0000374`（STATUS_HEAP_CORRUPTION）。

## 2. 复现与排除（安全，未触碰生产库外数据）

对**三种数据库**分别启动 GUI，全部 `0xC0000374` 崩溃：

| 数据库 | 订阅数 | 结果 |
|---|---|---|
| `bin/guiNDB.db`（13:59 新建，含 2 订阅） | 2 | 崩溃 |
| `bin/guiNDB.db`（清空订阅后） | 0 | 崩溃 |
| `bin/guiNDB1.db`（164 MB 旧库） | 大量 | 崩溃 |

→ **与数据库内容/订阅数量无关**。12:02 曾有 11 次正常启动/退出（UI 自动化环境），14:00 复现用同一 exe 崩溃，差异不在业务数据。

## 3. 崩溃定位（minidump，11:59:48）

`bin\temp\crash_20261123_115948.dmp`（CrashHandler 产物）：

- cdb `.ecxr`/`.exr`：**Access violation (c0000005)**，位于 `RtlpFreeHeapInternal`（堆释放），`rbx = 0x0061007200740078` = UTF-16 宽字符 **"artx"** —— `free()` 收到一个**被当作堆指针的宽字符串内容**。
- `addr2line`（DWARF，ImageBase `0x140000000`）解析栈帧：
  ```
  wxPrivate::UntypedBufferData::~UntypedBufferData()      wx/buffer.h:48
  wxScopedCharTypeBuffer<wchar_t>::Data::~Data()          wx/buffer.h:233
  wxScopedCharTypeBuffer<wchar_t>::DecRef()               wx/buffer.h:266
  wxScopedCharTypeBuffer<wchar_t>::~wxScopedCharTypeBuffer()  wx/buffer.h:187
  std::allocator<wchar_t>::~allocator()                   allocator.h:189
  ```
  → `wxString` 缓冲区析构时 `free` 了无效指针，堆损坏。

## 4. 根因（已确认）

**UCRT exe 在运行期从 PATH 加载了 MSVCRT 的 wx DLL**，跨 CRT 边界分配/释放导致堆损坏：

| 组件 | CRT（导入表） | 证据 |
|---|---|---|
| `bin\validproxy.exe` | **UCRT**（`api-ms-win-crt-*` 12 个） | `objdump -p`：12 个 `api-ms-win-crt-*` 导入；动态导入 4 个 wx DLL（`wxbase333ud_gcc_x64_custom.dll`、`wxmsw333ud_{aui,core,propgrid}_gcc_x64_custom.dll`）；无 `msvcrt.dll` |
| `bin\` 目录 | **无任何 wx DLL** | `bin\` 仅 `libgcc_s_seh-1.dll`/`libstdc++-6.dll`/`libwinpthread-1.dll`（MinGW 运行时），无 `wx*` |
| PATH 上的 `D:\vcpkg\installed\x64-mingw-dynamic\debug\bin` | **MSVCRT** wx | 该目录含 `wxbase333ud_*` 等（vcpkg 社区 triplet，msvcrt） |

**链条**：
1. exe 构建时链到 **UCRT 动态 wx 树**（`temp/vcpkg-ucrt-root`，`WX_VCPKG_DIR` 正确），AbiGuard 编译期闸门通过（编译器 CRT == 构建树 wx DLL CRT = UCRT）。
2. 但 `VALIDPROXY_COPY_WX_RUNTIME=OFF`（默认，保持 Rule 5）→ **不把 UCRT wx DLL 复制到 `bin\`（exe 目录）**。
3. `bin\` 下 UCRT wx DLL 缺失（其他会话清理/中断构建所致；用户提示"其他会话编译过，非正常终止"）。
4. 运行期 Windows 加载器**先搜 exe 目录（无 wx）再搜 PATH** → PATH 命中 `D:\vcpkg\...\x64-mingw-dynamic\debug\bin`（**MSVCRT wx**）。
5. UCRT exe + MSVCRT wx 跨 CRT → wxString 析构 `free` 无效指针 → `0xC0000374`。

**为何 AbiGuard 管不住**：`cmake/AbiGuard.cmake` 只在 **configure 时**比较"编译器 CRT vs 构建树 wx DLL CRT"。当 wx DLL 不在 exe 目录、靠 **PATH 运行期解析**时，PATH 上的 wx DLL CRT 不受 AbiGuard 控制 —— 这是 2026-11-13 同类混链事故的**运行期变体**（11-13 是编译期链错 wx，AbiGuard 挡得住；本次编译期链对、运行期加载错）。

## 5. 修复（用户指示 + 已实施）

**方案**：用 `clion-msvcrt`（GCC 15.2 / MSVCRT）+ `VALIDPROXY_STATIC_WX=ON` 构建 **Release 静态**版本（`CMakePresets.json` 的 `release-static` 预设）。

- **静态 wx 链入 exe** → exe **不再依赖任何 wx DLL**（导入表无 `wxbase/wxmsw`），运行期 PATH 加载 wx 的漏洞从根上消除。
- exe 依赖 `msvcrt.dll`（MSVCRT，与静态 wx 的 MSVCRT 匹配）+ `libwinpthread-1.dll`（MSVCRT-only Win7 兼容版，`resources/win7`，CMake `VALIDPROXY_MSVCRT_WINPTHREAD_DLL` 自动部署）。
- `-j 2` 构建（用户指定），静态 wx 大库链接。

## 6. 验收（已通过）

- [x] `cmake --preset release-static` **configure EXIT 0**：`[wx] using STATIC tree: D:/vcpkg/installed/x64-mingw-static`；`clion-gcc15-msvcrt`（MSVCRT）；静态 wx 无 DLL → AbiGuard 跳过 wx CRT 检查；IPO/LTO 按预期跳过（clion-msvcrt 无 LTO 插件，不报错）；`wxWidgets found: 3.3.3`。
- [x] `cmake --build build-tc-clion-msvcrt-Release --parallel 2` **EXIT 0**，**946/946** 链接成功（含 `UITests.exe` 与全部 test exe）。
- [x] `bin\validproxy.exe` 更新为 **15:31:30，30 MB**（原 Debug 285 MB）；`objdump -p` 导入表**无任何 `wxbase/wxmsw` DLL**（静态 wx 成功）、**无 `api-ms-win-crt-*` UCRT 导入**（仅 `msvcrt.dll` + 系统 DLL + `libwinpthread-1.dll`）。
- [x] `bin\libwinpthread-1.dll` = **65442 B，与 `resources/win7/libwinpthread-1.dll` 逐字节一致**（MSVCRT-only Win7 兼容版，构建 15:31:26 复制）——exe + winpthread CRT 匹配。
- [x] **运行验证**：`bin\validproxy.exe`（release-static）启动后 **30 s 未退出，不崩溃**（原 UCRT Debug 版此处 2-5 s 内 `0xC0000374`）。
- [x] 全量 ctest 回归：`ctest --test-dir build-tc-clion-msvcrt-Release -V` **EXIT 0，79/79 通过（100%）**，Total 259.31 s；含全部 UI 用例（`UI_CLEAR`/`UI_FLOATINGWIDGET`/`UI_LANGUAGEMENU` 等），UI 测试均以 `bin\validproxy.exe`（release-static）启动并正常通过 —— 无 `0xC0000374`（原 UCRT Debug 版在 GUI 初始化即崩溃）。

## 7. 与 2026-11-13 同类事故对比

| | 2026-11-13 | 2026-11-23（本次） |
|---|---|---|
| 根因 | 编译期链错：exe(UCRT) + wx DLL(GCC14.2/msvcrt) | 编译期链对：exe(UCRT)+UCRT wx 树；**运行期 PATH 兜底加载 MSVCRT wx** |
| 防护 | AbiGuard 编译期闸门（configure FATAL） | AbiGuard 管不住运行期 PATH 加载 |
| 触发 | 构建配置 wx 树错 | `bin\` 无 wx DLL（COPY_WX_RUNTIME=OFF + 清理）+ PATH 有 msvcrt wx |
| 根治 | UCRT 重建 wx + COPY_WX_RUNTIME=ON 复制到 `bin\` | **静态 wx（无 wx DLL 依赖）** |

**动态 wx 构建的持续风险**：只要 exe 依赖 wx DLL 且 `VALIDPROXY_COPY_WX_RUNTIME=OFF`（默认），一旦 `bin\` 的 wx DLL 缺失，运行期 PATH 就可能加载到 CRT 不匹配的 wx → 复发。**静态 wx（release-static）从根上消除该风险**，是首选交付形态。

## 8. 影响面 / 后续

- 本次 `bin\validproxy.exe` 已替换为 **release-static**（用户指示），无 wx DLL 依赖，双击/任意 PATH 均安全。
- 动态 wx Debug 构建（`build-tc-mingw64-Debug`）仍存在该风险；如需保留动态 wx 交付，必须显式 `-DVALIDPROXY_COPY_WX_RUNTIME=ON` 或确保 `bin\` 有 CRT 匹配 wx。
- 建议：把"exe 目录必须存在 CRT 匹配的 wx DLL（动态 wx）或静态链接（静态 wx）"纳入发布检查；`scripts/check-win7-deps.ps1` 可扩展该项。
