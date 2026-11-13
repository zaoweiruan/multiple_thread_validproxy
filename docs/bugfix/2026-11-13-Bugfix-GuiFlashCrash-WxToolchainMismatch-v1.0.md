---
title: "bugfix: GUI 启动闪退（0xC0000374 堆损坏）根因 = wxWidgets DLL 工具链/CRT 与 exe 不匹配"
type: bugfix
status: completed
date: 2026-11-13
module: build-system
version: v1.0
---

# GUI 启动闪退根因：wx DLL（GCC 14.2 / msvcrt）与 exe（GCC 16.2 / UCRT）不匹配

## 1. 症状

`validproxy.exe`（GUI）启动后闪退，未显示主窗口。CLI（`validproxy-cli.exe`）运行正常。

## 2. 复现（安全、不触碰生产库）

```powershell
# 1) 复制空测试库 + 生成测试配置（database.path → 测试库，auto_task/探针/监控禁用）
#    temp/guidebug/test.db  ← test/guiNDB_empty.db 副本
#    temp/guidebug/config.test.json ← bin/config.json 副本并改写 database.path、禁用 auto_task/network_monitor/proxy_process_monitor/independent_probe/standalone_pool

# 2) 启动 GUI（测试配置）
bin\validproxy.exe -c <abs>\temp\guidebug\config.test.json
# → 退出码 0xC0000374（STATUS_HEAP_CORRUPTION），约数秒内
```

> 注意：直接跑 `bin\validproxy.exe`（默认 `bin\config.json`）会打开 `bin\guiNDB.db`（生产库，162 MB，WAL 会被触碰）。复现必须用测试配置 + 测试库。

## 3. 根因（已确认）

**exe 与 wxWidgets DLL 使用不同的工具链与 CRT**，跨 CRT 边界分配/释放导致堆损坏：

| 组件 | 编译器 | CRT（导入表） | 证据 |
|---|---|---|---|
| `validproxy.exe` | GCC **16.2.0** | **UCRT**（`api-ms-win-crt-*`） | 构建日志 `Compiler: 16.2.0`；`objdump -p` 导入 `api-ms-win-crt-*.dll`，无 `msvcrt.dll` |
| `libstdc++-6.dll` / `libgcc_s_seh-1.dll` | 同 exe | **UCRT** | `objdump -p` 导入 `api-ms-win-crt-*` |
| `wxbase333ud_*` / `wxmsw333ud_{core,aui,propgrid}_*` | GCC **14.2.0** | **msvcrt.dll** | DLL 内字符串 `GCC: (GNU) 14.2.0`、`mingw-w64-v12.0.0`；`objdump -p` 导入 `msvcrt.dll`，无 `api-ms-win-crt-*` |

- 唯一跨 CRT 边界 = **wx DLL**（动态）。exe 的静态依赖（boost/sqlite3/curl，vcpkg `x64-mingw-static`）在链接期解析到 exe 的 UCRT，无独立 CRT。
- exe 内出现的 `GCC: (GNU) 14.2.0` 字符串来自**静态**依赖库（GCC 14.2 编译），其 CRT 调用已在链接期绑定到 exe 的 UCRT，非独立边界。

## 4. 崩溃定位（CLion 调试器）

`xdebug_*` 断点闭环抓到的栈（`main → UIApp::OnInit → MainFrame::MainFrame → 堆管理器`）：

```
RtlRegisterSecureMemoryCacheCallback        ← 堆管理器检测到损坏并抛 0xC0000374
MainFrame::MainFrame  MainFrame.cpp:127     ← wxFrame 基类构造期间触发检测
UIApp::OnInit          UIApp.cpp:98          ← frame_ = new MainFrame(...)
main                   main_gui.cpp:558      ← wxEntry(...)
```

- 崩溃发生在 wx 初始化（wxFrame 基类构造），早于任何业务/自动任务。
- `main()` 的引导代码（ConfigReader::load、DB 打开、模板/SQL 种子、`registerAllStepHandlers`）被 CLI 以**完全相同**顺序执行且无崩溃 → 项目引导代码干净，损坏仅在 wx 路径。

## 5. 证据链

1. **CLI 无崩溃**：`validproxy-cli.exe`（不含 wx）运行相同引导代码 → `-show-sub` 退出码 0。
2. **GUI 仅在 wx 初始化崩溃**：栈固定在 `MainFrame::MainFrame`；11-13 11:55 日志在 `[FullConfigTemplateDAO] Table already has templates` 后中断（即模板种子之后、wxEntry/MainFrame 期间）。
3. **CRT 不匹配（objdump 导入表）**：exe 侧 `api-ms-win-crt-*`（UCRT）；wx 侧 `msvcrt.dll`（旧 CRT）；无一例交叉。
4. **GCC 版本不匹配**：wx DLL 内 `GCC: (GNU) 14.2.0`；exe `Compiler: 16.2.0`。
5. **vcpkg 事实**：`D:\vcpkg\ports\wxwidgets\vcpkg.json` 版本 3.3.3（与现有 `wxbase333ud` 一致）；`x64-mingw-dynamic` 由 vcpkg 社区 triplet 构建，编译器来自 PATH（`VCPKG_ENV_PASSTHROUGH PATH`），当时 PATH 上为 GCC 14.2（msvcrt）的 MinGW。

## 6. 修复（进行中）

**方案**：用 `E:\mingw64`（GCC 16.2.0，UCRT rt_v14）重建 wxWidgets 3.3.3 + 依赖（libjpeg-turbo/libpng/tiff/webp/expat/nanosvg/pcre2/zlib/opengl），使 wx DLL 与 exe 同为 GCC 16.2 / UCRT。

- 新增自定义 triplet `temp/vcpkg-triplets/x64-mingw-dynamic-ucrt.cmake`（拷贝 `community/x64-mingw-dynamic.cmake`，工具链由 PATH 解析）。
- 隔离安装根 `temp/vcpkg-ucrt-root`，不覆盖现有 `D:\vcpkg\installed\x64-mingw-dynamic`。
- `vcpkg install wxwidgets[debug-support] --triplet x64-mingw-dynamic-ucrt --overlay-triplets=<dir> --x-install-root=<root>`，PATH 前置 `E:\mingw64\bin`。
- 特性集 `[debug-support]`（classic 模式精确特性集）匹配现有 wx：无 `sound`/sdl2（现有 `x64-mingw-dynamic` 无 sdl2 包）。
- 冒烟验证：`zlib` 用该 triplet 构建产出 `api-ms-win-crt-*`（UCRT）DLL，证明工具链生效。

**CMake 集成（已准备）**：`CMakeLists.txt` 新增缓存变量 `WX_VCPKG_DIR`（默认 `\${VCPKG_ROOT}/installed/x64-mingw-dynamic`），wx 的 find_package/include 全部改用它；对 UCRT 重建传 `-DWX_VCPKG_DIR=<abs>/temp/vcpkg-ucrt-root/x64-mingw-dynamic-ucrt` 即可把 exe 链到 UCRT wx，不动现有 msvcrt 配置。wx CMake config 已验证可重定位（`_IMPORT_PREFIX` 由 config 文件位置推导），DLL 名与 msvcrt 版相同（`wxbase333ud_gcc_x64_custom.dll` 等）。

**待办**：构建完成 → 校验 wx DLL 导入表为 UCRT → 以 `-DWX_VCPKG_DIR=<ucrt root>` 重配置 `build-tc-mingw64-Debug` → 重链 `validproxy.exe` → 用测试配置 + PATH 指向 UCRT wx bin 跑 GUI 验证不再 `0xC0000374`。

## 7. 验收（已通过）

- [x] `objdump -p wxbase333ud_*.dll / wxmsw333ud_*.dll` 显示 `api-ms-win-crt-*`（UCRT），无 `msvcrt.dll`；传递依赖（libjpeg/libpng/libtiff/libwebp/libpcre2/libz）全部 UCRT。
- [x] `bin\validproxy.exe` 用 `-DWX_VCPKG_DIR=temp/vcpkg-ucrt-root/x64-mingw-dynamic-ucrt` 重链成功（15:26:02，256.9 MB）；`objdump -p` 导入 12 个 `api-ms-win-crt-*`、0 个 `msvcrt.dll`。
- [x] `bin\validproxy.exe -c temp\guidebug\config.test.json`（PATH 前置 `temp/vcpkg-ucrt-root/x64-mingw-dynamic-ucrt/{,debug/}bin`）运行 ≥12 s 不崩溃（曾 2-5 s 内 `0xC0000374`）；`Get-Process .Modules` 证实进程加载的 wx + 编解码 DLL 全部来自 UCRT 根，MinGW 运行时来自 `bin\`（GCC 16.2）。
- [x] GUI 日志越过旧崩溃点：旧崩溃运行（12:48）仅 4 行后中断；新运行（15:27）打出 `[MainFrame] StandaloneProxyEvent received` 并进入事件循环（窗口正常显示）。
- [x] 生产库未受本次验证改动：验证使用测试库 `temp\guidebug\test.db`（其 `-wal/-shm` 时间戳与本次运行一致）；`bin\guiNDB.db` / `bin\worker\guiNDB.db` 无 `-wal/-shm`（无活动连接）。
- [x] **双击启动（默认 PATH，无 UCRT 覆盖）**：UCRT wx + 依赖 DLL 复制到 `bin\`（exe 目录）后，`validproxy.exe -c` 测试配置运行 ≥10 s 存活（此前同样双击路径 8 s 内 `0xC0000374`）；`Get-Process .Modules` 证实 wx 全部从 `bin\`（UCRT）加载，GUI 日志越过旧崩溃点进入事件循环。

## 8. 影响面 / 后续

- **`bin\` 陈旧 msvcrt wx DLL 已移出**：`bin\` 原有 46 个 msvcrt wx/编解码 DLL（GCC 14.2，违反 Rule 5 且会使加载器优先选中 msvcrt 版而重演崩溃）已移至 `temp\guidebug\bin-wx-backup\`（可回滚）。
- **部署级修复（双击/无 PATH 覆盖的关键）**：Windows 加载器**先搜 exe 目录**再搜 PATH；默认 PATH 上的 `D:\vcpkg\installed\x64-mingw-dynamic\debug\bin` 仍是 msvcrt wx，双击会先加载它 → 重演 `0xC0000374`。故把 `${WX_VCPKG_DIR}/debug/bin/*.dll`（UCRT wx + 编解码依赖）放入 `bin\`（exe 目录），加载器命中 UCRT 版，与 PATH 无关。已由 CMake 新增 `VALIDPROXY_COPY_WX_RUNTIME`（默认 OFF 保持 Rule 5；UCRT 构建置 ON）经 POST_BUILD `copy_if_different` 固化，重链不丢。
- **CMake 集成**：`CMakeLists.txt` 新增缓存变量 `WX_VCPKG_DIR`（默认 `\${VCPKG_ROOT}/installed/x64-mingw-dynamic`），wx 的 find_package/include 全部改用它；UCRT 重建用 `-DWX_VCPKG_DIR=<ucrt root>`。默认值保留原 msvcrt 行为，不影响其它配置。
- **跨工具链静态依赖树（同一根因的另一面，已修复）**：`libCatch2d.a`（及整个 vcpkg `x64-mingw-static` 树）由 **GCC 14.2 / msvcrt** 构建，其 C++ ABI 与 GCC 16.2 / UCRT 的 `libstdc++` 不同——GCC 14.2 的 `std::streampos`/`fpos` 形态导致 Catch2 的 `StreamBufImpl` 基类虚表引用 `std::basic_streambuf<char>::seekpos(std::fpos<int>,...)`，而 GCC 16.2 `libstdc++` 只提供 `seekpos(std::fpos<mbstate_t>,...)` → UITests 链接 `undefined reference`。修复=`vcpkg install catch2 --triplet x64-mingw-static-ucrt`（GCC 16.2 UCRT，隔离根 `temp/vcpkg-ucrt-root`）+ CMake 新增 `CATCH2_VCPKG_DIR` 缓存变量重定向；`UITests.exe` 已成功链接。
- **观察（非本次修复所致）**：构建窗口内 `bin\guiNDB.db`（15:16:14）与 `bin\worker\guiNDB.db`（15:10:08）各有一次写入，时间早于本次重链（15:26），无对应 `ui_*.log`/crash dump，疑为先前遗留/手工启动（默认配置打开生产库后崩溃或退出）；本次验证全程使用测试库，未触碰生产库。
- 考虑把"exe 与依赖树工具链/CRT 一致"固化为配置期 ABI 闸门（见 `docs/specs/2026-11-11-Spec-MultiToolchainBuildMatrix-v1.0.md` §4.2），防止同类静默混链回归。
