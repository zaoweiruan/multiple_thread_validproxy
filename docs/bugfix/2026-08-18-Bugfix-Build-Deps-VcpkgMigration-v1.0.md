# Bugfix: 构建依赖迁移 E:/vcpkg → D:/vcpkg + Boost 改用 vcpkg 管理 + wxWidgets 3.3.3 适配

- 日期: 2026-08-18
- 类型: Bugfix / 构建配置迁移
- 模块: CMakeLists.txt / CMakePresets.json / ConfigDialog.cpp
- 版本: v1.0
- 状态: ✅ completed

---

## 1. 背景

构建依赖包从 `E:\vcpkg` 迁移至 `D:\vcpkg`，同时将 Boost 由独立安装包
（`D:\boost_1_88_0`，MinGW 工具链命名 `mgw14`）改为 vcpkg 管理
（`D:\vcpkg\installed\x64-mingw-static`，GCC 14 命名 `gcc14`）。

## 2. 根因 / 差异清单

### 2.1 依赖路径与库命名差异

| 项 | 旧 (E:/vcpkg + D:/boost_1_88_0) | 新 (D:/vcpkg) |
|---|---|---|
| VCPKG_ROOT | `E:/vcpkg` | `D:/vcpkg` |
| Boost 来源 | 独立安装包 `D:/boost_1_88_0` | vcpkg `x64-mingw-static`（头文件与库均在 vcpkg installed 目录内） |
| boost_json 库 | `${BOOST_ROOT}/lib/libboost_json-mgw14-mt-x64-1_88.a` | `${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a` |
| wxWidgets | 3.2.5 (DLL 名 `wxbase32u_*`) | 3.3.3 (DLL 名 `wxbase333u_gcc_x64_*`) |

### 2.2 boost-json 需单独安装

vcpkg 的 `boost` 包默认不含 `json` 组件，`boost-json` 为独立端口。安装：

```powershell
$env:HTTP_PROXY="socks5h://127.0.0.1:10809"; $env:HTTPS_PROXY="socks5h://127.0.0.1:10809"
& "D:\vcpkg\vcpkg.exe" install "boost-json:x64-mingw-static"
```

产物：`D:\vcpkg\installed\x64-mingw-static\lib\libboost_json-gcc14-mt-x64-1_91.a`
（直连 GitHub 会 SSL error code 35，必须走代理）。

### 2.3 zlib 库名不一致（vcpkg 包缺陷）

`D:\vcpkg\installed\x64-mingw-static\share\zlib\zlibTargets-release.cmake` 指向
`libzlib.a`，但实际文件为 `libzs.a`（`libzlib.a` 不存在）。CMake 的
`find_dependency(ZLIB)` 失败。配置时必须显式指定：

```
-DZLIB_LIBRARY="D:/vcpkg/installed/x64-mingw-static/lib/libzs.a"
-DZLIB_INCLUDE_DIR="D:/vcpkg/installed/x64-mingw-static/include"
```

### 2.4 wxWidgets 3.3.3 API 变更

`wxPG_FILE_DIALOG_TITLE` 在 3.3.3 中被移入
`#if WXWIN_COMPATIBILITY_3_0` 条件块（默认关闭），导致 GCC 报
`'wxPG_FILE_DIALOG_TITLE' was not declared`。替代宏为
`wxPG_DIALOG_TITLE`（语义完全相同，同为 `"DialogTitle"` 属性）。

### 2.5 wxWidgets DLL 复制列表需同步

CMakeLists.txt `copy_wx_dlls()` 函数硬编码 3.2.5 DLL 名（`wxbase32u_*`），
3.3.3 命名为 `wxbase333u_gcc_x64_*` 且 `libzlibd1.dll`→`libzd.dll`、
`libtiffd.dll`→`libtiffd-6.dll`、新增 `libwebp*.dll`。旧列表全部 `if(EXISTS)`
跳过，导致 `bin/` 与 `tests/` 缺 3.3.3 DLL，GUI 与
`test_log_statistics_event`（链接 wxbase/wxcore）运行报
`0xc0000135` (STATUS_DLL_NOT_FOUND)。

### 2.6 wxcore 3.3.3 依赖 comctl32 v6（GetWindowSubclass）

wxWidgets 3.3.x `wxmsw333u_core`/`wxmsw333ud_core` 的导入表
**延迟加载（delay-load）** `COMCTL32.dll!GetWindowSubclass`（comctl32 v6
专属导出，v5.82 无）。Windows 上 comctl32 v6 位于 WinSxS，只有可执行文件
内嵌 `Microsoft.Windows.Common-Controls 6.0.0.0` manifest 依赖时才激活；
否则回退加载 System32 的 comctl32 **v5.82**。GUI 启动创建控件时解析
延迟导入失败，弹窗：

```
无法定位程序输入点 GetWindowSubclass 于动态链接库
...\bin\wxmsw333u_core_gcc_x64_custom.dll
```

Debug 单测不受影响（`test_log_statistics_event` 只测 wxEvent 不建窗口，
不触发延迟加载）。wxWidgets 自带 `wx/msw/wx.rc` 仅在定义
`wxUSE_RC_MANIFEST` 时嵌入 manifest（默认关闭），本项目也未启用。

修复：新建 `src/ui/app.manifest`（common-controls v6 依赖），并在
`src/ui/icons.rc` 追加 `1 24 "src/ui/app.manifest"` 资源
（`1` = CREATEPROCESS_MANIFEST_RESOURCE_ID，`24` = RT_MANIFEST）。
嵌入后 Windows 加载 WinSxS comctl32 v6，`GetWindowSubclass` 可解析。

### 2.7 libsharpyuv.dll 传递依赖缺失

vcpkg 的 `libwebp.dll`（2.x）新增硬依赖 `libsharpyuv.dll`（sharpyuv 库），
而 `copy_wx_dlls()` 列表未包含 → GUI 启动报
`由于找不到 libsharpyuv.dll，无法继续执行代码`（0xC0000135）。
源文件存在于 `D:\vcpkg\installed\x64-mingw-dynamic\bin\libsharpyuv.dll`
（Debug 为 `debug\bin\libsharpyuv.dll`），Debug/Release 分支列表各补一项即可。

## 3. 修改内容

### 3.1 CMakeLists.txt

1. 删除 `BOOST_ROOT` CACHE 定义与 `D:/boost_1_88_0` 默认值；
2. `VCPKG_ROOT` 默认值 `E:/vcpkg` → `D:/vcpkg`；
3. 删除 `include_directories(${BOOST_ROOT}/include)` 与
   `link_directories(${BOOST_ROOT}/lib)`（vcpkg installed 目录已含 Boost 头文件与库）；
4. 9 处 `${BOOST_ROOT}/lib/libboost_json-mgw14-mt-x64-1_88.a` →
   `${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a`
   （validproxy-cli / validproxy / test_autotask / test_config_reader /
   test_config_generator / test_subscription_parser / test_proxy_batch_query /
   test_singbox_vless_builder / test_xray_api_direct）；
5. `copy_wx_dlls()` DLL 列表更新为 3.3.3 命名（Debug/Release），
   Debug/Release 分支各补 `libsharpyuv.dll`（libwebp 2.x 新增依赖）；
6. `test_log_statistics_event` 目标追加 `copy_wx_dlls(test_log_statistics_event)`；
7. 注释内残留 `E:/vcpkg` 路径（test_model 禁用块）同步为 `D:/vcpkg`。

### 3.2 CMakePresets.json

- 删除 `"BOOST_ROOT": "D:/boost_1_88_0"`；
- `"VCPKG_ROOT": "E:/vcpkg"` → `"D:/vcpkg"`。

### 3.3 src/ui/ConfigDialog.cpp

4 处 `wxPG_FILE_DIALOG_TITLE` → `wxPG_DIALOG_TITLE`（wxWidgets 3.3.3 兼容）。

### 3.4 src/ui/app.manifest + src/ui/icons.rc（GUI 运行时修复）

- 新建 `src/ui/app.manifest`：声明 `Microsoft.Windows.Common-Controls`
  v6.0.0.0 依赖（comctl32 v6 SxS 激活，修复 GetWindowSubclass 解析失败）；
- `src/ui/icons.rc` 末尾追加 manifest 资源：`1 24 "src/ui/app.manifest"`
  （ID=1 CREATEPROCESS_MANIFEST_RESOURCE_ID，type=24 RT_MANIFEST，
  路径经 `-I${CMAKE_SOURCE_DIR}` 解析，与 PNG 图标资源一致）。

## 4. 配置命令（PowerShell，Debug + Ninja）

```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_TOOLCHAIN_FILE="D:/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-mingw-static `
  -DCMAKE_C_COMPILER="E:/w64devkit/bin/gcc.exe" `
  -DCMAKE_CXX_COMPILER="E:/w64devkit/bin/g++.exe" `
  -DZLIB_LIBRARY="D:/vcpkg/installed/x64-mingw-static/lib/libzs.a" `
  -DZLIB_INCLUDE_DIR="D:/vcpkg/installed/x64-mingw-static/include" `
  -DCMAKE_CXX_FLAGS_DEBUG="-g3 -O0" `
  -DCMAKE_RC_FLAGS="-O coff"
```

### 4.1 Release 配置（PowerShell，Ninja）

Release 使用独立构建目录 `build-release`（与 Debug 的 `build/` 隔离），
除 `-DCMAKE_BUILD_TYPE=Release` 外参数相同，且**无需**覆盖
`CMAKE_CXX_FLAGS_DEBUG`（CMake 的 CodeView 探测只污染 DEBUG 标志）：

```powershell
# 全新配置（旧缓存仍引用 E:/vcpkg/libzlib.a，必须删除后重建）
Remove-Item -Recurse -Force build-release
# 预生成 version.h（ALL custom target 无 output edge，全新构建需先手动生成）
New-Item -ItemType Directory -Force -Path "build-release\include" | Out-Null
python scripts/generate_version_h.py include/version.h.in build-release/include/version.h Release 1 4 9 v1.4.9 78dae00 dirty

cmake -B build-release -G "Ninja" -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="D:/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-mingw-static `
  -DCMAKE_C_COMPILER="E:/w64devkit/bin/gcc.exe" `
  -DCMAKE_CXX_COMPILER="E:/w64devkit/bin/g++.exe" `
  -DZLIB_LIBRARY="D:/vcpkg/installed/x64-mingw-static/lib/libzs.a" `
  -DZLIB_INCLUDE_DIR="D:/vcpkg/installed/x64-mingw-static/include" `
  -DCMAKE_RC_FLAGS="-O coff"

cmake --build build-release --parallel 8
ctest --test-dir build-release -V
```

注：exe 输出目录与 Debug 相同（`bin/`），Release 构建会覆盖
`bin/validproxy.exe`/`bin/validproxy-cli.exe`；Debug/Release DLL 因命名不同
（`wxbase333ud_*` vs `wxbase333u_*`）在 `bin/` 下共存不冲突。

注：
- 必须显式指定 `CMAKE_C_COMPILER`/`CMAKE_CXX_COMPILER`，否则 CMake 可能选中
  clang++（lld-link 报 `oldnames.lib`/`msvcrtd.lib` 缺失）；
- 必须覆盖 `CMAKE_CXX_FLAGS_DEBUG`，否则 CMake 自动注入 clang 专属
  `-Xclang -gcodeview`（g++ 报 `unrecognized command-line option`），
  此为 2026-08-05 已知环境障碍；
- 全新构建需先手动生成 `build/include/version.h`（custom target 无 output
  edge，首次构建报 `include/version.h ... no known rule`）：
  `python scripts/generate_version_h.py include/version.h.in build/include/version.h Debug 1 4 9 v1.4.9 78dae00 dirty`

## 5. 验证

- 配置：`wxWidgets found: 3.3.3`、`Found SQLite3: D:/vcpkg/...` ✅
- 构建：全量编译通过（464 目标）✅
- ctest：`100% tests passed, 0 tests failed out of 29` ✅
- CLI：`validproxy-cli.exe -h` 正常输出帮助 ✅
- `compile_commands.json`：`E:/vcpkg` 引用 0、`boost_1_88`/`mgw14` 引用 0、
  `D:/vcpkg` 引用 427 ✅
- **Release 构建（build-release/）**：`wxWidgets found: 3.3.3` + `ZLIB libzs.a` +
  `SQLite3 3.53.4` ✅；构建 464/464 ✅；`ctest --test-dir build-release` 29/29 ✅；
  `validproxy-cli.exe -show-sub` 正常读取数据库 ✅；version.h 内嵌
  `APP_BUILD_TYPE "Release"`（`build-release/include/version.h`）✅
- **GUI 启动（Release）**：`bin/validproxy.exe` 启动后存活（10s 无退出），日志
  `Build: Release | 2026-08-18 10:11:24` ✅；manifest 已嵌入
  （exe 内可检索 `Microsoft.Windows.Common-Controls`）✅；
  `libsharpyuv.dll` 已复制至 `bin/` 与 `tests/` ✅
- **bin/ 全量 DLL 传递依赖**：`objdump` 枚举 bin 下全部 DLL 导入，
  相对 bin+System32 无缺失 ✅