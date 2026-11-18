---
title: "spec: Win10 虚机调试环境搭建方案（方案 C：仅复制构建产物）"
type: spec
status: maintained
date: 2026-11-18
module: build-system
version: v1.0
---

# Win10 虚机调试环境搭建方案 v1.0

> 目标读者：需在 `win10-ltsc` 虚机上独立运行/调试本项目的开发者与 AI Agent。
> 决策背景：虚机磁盘仅剩 1.4GB（C:），无法容纳完整构建环境（工具链+依赖+源码 ≈ 2GB+）；经评估选定**方案 C（仅复制构建产物）**，用户确认「节省空间且不影响运行/调试效率」。
> 关联：`docs/specs/2026-11-11-Spec-MultiToolchainBuildMatrix-v1.0.md`、`docs/2026-11-16-Reference-MinGW-Toolchain-Usage-v1.0.md`。

---

## 1. 目标

在 `win10-ltsc` 虚机上建立**可独立运行与调试**本项目 GUI 应用的最小环境：复制已构建产物（exe + 运行时 DLL + xray + 测试库 + 改写配置），使虚机能启动 `validproxy.exe` 并调试界面/逻辑。**不**在虚机内重建（无工具链，见 §8 边界）。

## 2. 背景与现状（2026-11-18 实测）

| 项 | 值 |
|---|---|
| 虚机 | `win10-ltsc`，**运行中**，Gen 1，2 CPU，~3.4GB RAM |
| OS | Microsoft Windows 10 IoT 企业版 LTSC，Build 19044（PS 5.1.19041） |
| 网络 | 内网 `192.168.10.21`（主机 `.1`），hyper-v internal switch |
| 远程通道 | **PowerShell Direct 实测可用**（`Invoke-Command -VMName win10-ltsc -Credential` 成功；凭据经 `$env:win10_ltsc_user`/`$env:win10_ltsc_user_password` 提供） |
| 磁盘 | **C: 仅 1.4GB 空闲**（17.9GB 已用）；D: 无空间；VHDX Dynamic 31GB/物理 21.5GB（主机 E: 可扩容，但本次不扩容） |
| 已有工具 | **无**（cmake/ninja/gcc/git/clang 全部缺失），`C:\dev`/`C:\mingw64`/`C:\vcpkg` 均不存在 |
| 可回收空间 | WinSxS 7.21GB（不可直接删，DISM 有风险）、Temp+更新缓存 ~0.28GB —— 不足以补足 2GB+ |

**为何方案 C**：完整构建环境需 ~2GB+（mingw64 0.73 + UCRT 依赖 0.68 + CMake/Ninja 0.2 + 源码 7.4GB 中构建集 + 构建产物），远超 1.4GB；扩容 VHD（需关机）或挂第二盘（Gen1 SCSI 热插不稳）或清 WinSxS（有风险）均不如只复制产物直接、零风险、即刻可用。

## 3. 方案（C：只复制已构建产物）

- **原理**：虚机仅作为**运行/调试目标**，不承载构建。主机已构建产物（`mingw-ucrt` 链）经 PSDirect 复制到虚机，虚机内直接启动。
- **空间**：复制集 ≈ **853MB**（见 §4），远小于完整环境 2GB+，可放入 1.4GB 空闲。
- **效率**：运行时/调试**零损失**——同一 exe + 同一 DLL 集，行为与主机一致。唯一差异：虚机内**不能重编译**（无工具链）；如需迭代改码须在主机构建后增量复制，或后续扩容 VHD 建全环境。
- **CRT 一致性**：产物为 **UCRT**（`mingw-ucrt` 链），wx DLL 必须取 UCRT 树 `temp/vcpkg-ucrt-root/.../debug/bin`（**禁止**取 msvcrt 树 `D:\vcpkg\installed\x64-mingw-dynamic`），否则 `0xC0000374` 闪退。

## 4. 输入（复制集明细）

| 来源（主机） | 目标（虚机） | 体积 |
|---|---|---|
| `bin\validproxy.exe` | `C:\validproxy\` | 29.4 MB |
| `bin\validproxy-cli.exe` | `C:\validproxy\` | 9.0 MB |
| `bin\{libstdc++-6.dll, libgcc_s_seh-1.dll, libwinpthread-1.dll}` | `C:\validproxy\` | ~26.6 MB |
| `temp\vcpkg-ucrt-root\x64-mingw-dynamic-ucrt\debug\bin\*.dll`（29 个 UCRT wx/依赖 DLL） | `C:\validproxy\` | ~564.8 MB |
| `E:\v2rayN-windows-64\bin\xray\xray.exe` | `C:\Xray-windows-64\` | 34 MB |
| `E:\v2rayN-windows-64\bin\{geoip.dat, geosite.dat, geoip.metadb, geoip-only-cn-private.dat}` | `C:\Xray-windows-64\` | ~36.1 MB |
| `test\guiNDB.db`（**测试库**，非生产 `bin\guiNDB.db`） | `C:\validproxy\data\guiNDB.db` | 153 MB |
| `bin\config.json`（**改写路径后**） | `C:\validproxy\config.json` | ~3.5 KB |

> 体积合计 ≈ **853 MB**，1.4GB 空闲内留有 ~550MB 余量（含后续产物增量）。

## 5. 输出（虚机目标布局）

> **运行目录 = `C:\validproxy\`（=exeDir）**：exe、全部运行时 DLL、`config.json` 同目录。依据 `ConfigReader.cpp:41`（默认 config = `<exeDir>\config.json`，**自动被找到**，无需 `-c`）+ Windows 加载器**先搜 exe 目录**（DLL 命中，无 PATH 依赖）。config 内路径以 exeDir 为基准解析（`ConfigReader.cpp:67`），故采用**绝对虚机路径**避免歧义。

```
C:\validproxy\                        ← ① 产出物保存/运行目录（=exeDir）
  validproxy.exe  validproxy-cli.exe  config.json
  libstdc++-6.dll  libgcc_s_seh-1.dll  libwinpthread-1.dll
  <29 个 UCRT wx DLL>.dll           ← 全部 DLL 与 exe 同目录（加载器优先 exe 目录）
  worker\                            ← 运行时状态（exeDir\worker\autotask_state.json，默认可移植）
  data\
    guiNDB.db

C:\Xray-windows-64\                   ← ② xray 目录（独立，与运行目录同级）
  xray.exe  geoip.dat  geosite.dat  geoip.metadb  geoip-only-cn-private.dat
```

**config.json 改写**（主机绝对路径 → **虚机绝对路径**）：

| 键 | 原值（主机） | 新值（虚机） |
|---|---|---|
| `database.path` | `E:\eclipse_workspace\...\bin\guiNDB.db` | `C:\validproxy\data\guiNDB.db` |
| `proxy.xray_executable` | `E:\v2rayN-windows-64\bin\xray\xray.exe` | `C:\Xray-windows-64\xray.exe` |
| `proxy.xray_asset_dir` | `E:\v2rayN-windows-64\bin` | `C:\Xray-windows-64` |
| `auto_task.state_file` | `E:\eclipse_workspace\...\bin\` | `C:\validproxy\`（或留空走默认 `exeDir\worker`） |
| `proxy.singbox_executable` / `singbox_asset_dir` | 主机路径（`use_singbox=false`） | 可保留或清空（未启用） |

> 运行入口：`C:\validproxy\validproxy.exe`（GUI，config 自动在 `C:\validproxy\config.json`）；CLI `C:\validproxy\validproxy-cli.exe`。`test_config.json` 仅用于 CTest 测试，GUI 调试用 `config.json`；如需虚机内跑单测再另行复制/改写。

## 6. 前置条件

1. 虚机 `win10-ltsc` 运行中；PowerShell Direct 凭据可用（`win10_ltsc_user`/`win10_ltsc_user_password`，仅本会话使用）。
2. 主机已构建 `mingw-ucrt` 链产物（`bin\validproxy.exe` 存在）与 UCRT wx 树。
3. 虚机 C: 空闲 ≥ 1GB（当前 1.4GB，满足 853MB 复制集）。
4. 测试库 `test\guiNDB.db`（153MB）存在；**不使用**生产 `bin\guiNDB.db`。

## 7. 实施步骤（PSDirect 全自动）

```powershell
# 1. 建立会话
$cred = New-Object System.Management.Automation.PSCredential(
  [Environment]::GetEnvironmentVariable('win10_ltsc_user','User'),
  (ConvertTo-SecureString [Environment]::GetEnvironmentVariable('win10_ltsc_user_password','User') -AsPlainText -Force))
$s = New-PSSession -VMName win10-ltsc -Credential $cred

# 2. 虚机内建目录 + 复制（Copy-Item -ToSession，一次一目录）
Invoke-Command -Session $s -ScriptBlock { New-Item -ItemType Directory -Force -Path 'C:\validproxy\data','C:\Xray-windows-64' | Out-Null }
Copy-Item 'bin\validproxy.exe','bin\validproxy-cli.exe' -Destination 'C:\validproxy' -ToSession $s -Force
Copy-Item 'bin\libstdc++-6.dll','bin\libgcc_s_seh-1.dll','bin\libwinpthread-1.dll' -Destination 'C:\validproxy' -ToSession $s -Force
Copy-Item 'temp\vcpkg-ucrt-root\x64-mingw-dynamic-ucrt\debug\bin\*.dll' -Destination 'C:\validproxy' -ToSession $s -Force
Copy-Item 'E:\v2rayN-windows-64\bin\xray\xray.exe','E:\v2rayN-windows-64\bin\geoip.dat','E:\v2rayN-windows-64\bin\geosite.dat','E:\v2rayN-windows-64\bin\geoip.metadb','E:\v2rayN-windows-64\bin\geoip-only-cn-private.dat' -Destination 'C:\Xray-windows-64' -ToSession $s -Force
Copy-Item 'test\guiNDB.db' -Destination 'C:\validproxy\data\guiNDB.db' -ToSession $s -Force
# 改写 config.json（§5 表）后复制到 C:\validproxy\config.json

# 3. 验证
Invoke-Command -Session $s -ScriptBlock {
  Get-ChildItem 'C:\validproxy' | Select-Object Name,Length
  & 'C:\validproxy\validproxy-cli.exe' --help   # CLI 冒烟；GUI 需交互（RDP/控制台）
}
Remove-PSSession $s
```

## 8. 边界条件与风险

| 项 | 说明 | 等级 |
|---|---|---|
| 虚机内不能重编译 | 无工具链/依赖；改码须在主机构建后增量复制 `validproxy.exe`（+ 变更 DLL） | 中 |
| CRT 一致性 | **必须用 UCRT wx**（`temp/vcpkg-ucrt-root`）；误用 msvcrt wx → `0xC0000374` 闪退 | 高 |
| 配置路径改写 | 主机绝对路径在虚机无效，未改写则启动即路径错误 | 高 |
| 数据库 | 用**测试库** `test\guiNDB.db`，**禁止**生产 `bin\guiNDB.db`（项目数据安全红线） | 高 |
| xray 资产 | 需 `geoip.dat`/`geosite.dat`/`geoip.metadb`；`xray_asset_dir` 指向虚机 xray 目录 | 中 |
| 磁盘余量 | 853MB 复制集 + 后续增量；1.4GB 空闲足够但**无大冗余**，扩容需关机 | 中 |
| GUI 交互 | `validproxy.exe` 为 GUI，需 RDP(3389)/Hyper-V 控制台交互，非纯命令行 | 低 |
| 凭据安全 | 仅会话内使用，不入库/文档/记忆 | 低 |

## 9. 验收标准

1. 虚机内 `C:\validproxy\validproxy-cli.exe --help` 正常输出（CLI 冒烟）。
2. 虚机内启动 `C:\validproxy\validproxy.exe`，GUI 主窗口出现，无 `0xC0000374` 闪退（UCRT wx 一致性验证）。
3. 配置加载成功（数据库路径指向 `C:\validproxy\data\guiNDB.db`，xray 路径指向 `C:\Xray-windows-64\xray.exe`）。
4. 代理列表能读取（测试库有数据）；连接性测试可触发（xray 可启动）。
5. 复制集总体积 ≤ 1.0GB；虚机 C: 仍留 ≥ 200MB 空闲。

## 10. 关联文档

- `docs/specs/2026-11-11-Spec-MultiToolchainBuildMatrix-v1.0.md`（工具链矩阵，`mingw-ucrt` 链来源）
- `docs/2026-11-16-Reference-MinGW-Toolchain-Usage-v1.0.md`（工具链使用规范 v1.1：ABI 闸门/产物隔离/wx 运行时）
- `docs/bugfix/2026-11-13-Bugfix-GuiFlashCrash-WxToolchainMismatch-v1.0.md`（`0xC0000374` 根因）
- `docs/reports/2026-11-16-Report-ClangToolchainFeasibility-v1.0.md`（依赖树背景）
