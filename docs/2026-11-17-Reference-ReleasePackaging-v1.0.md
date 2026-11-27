---
title: "reference: 发布打包规范（Release Assets & 工具链口径）"
type: reference
status: maintained
date: 2026-11-17
updated: 2026-11-27
module: build-system
version: v1.1
---

# 发布打包规范 v1.1

> 目标读者：发布构建的维护者 / AI Agent。**后续所有版本发布一律遵照本文件执行。**
> 执行脚本：`scripts/package-win7-release.ps1`（本规范的可执行版本）。
> 变更本规范需同步更新脚本与本文件。

---

## 1. 发布资产（固定 2 个）

| 资产名 | 来源 | 说明 |
| --- | --- | --- |
| `validproxy.exe` | `bin/validproxy.exe` | GUI 主程序（msvcrt-win32 工具链，全静态） |
| `validproxy-cli.exe` | `bin/validproxy-cli.exe` | CLI 入口（msvcrt-win32 工具链，全静态） |

**没有 zip 资产**（v1.0 曾规定一个只含 `libwinpthread-1.dll` 的 `validproxy-<ver>-win64.zip`，
自 2026-11-26 默认工具链改为 `msvcrt-win32`（win32 线程 + `-static`）后已取消）。

## 2. 为什么不再需要 DLL 压缩包

默认工具链 `msvcrt-win32`（MinGW-Builds GCC 16.2 / MSVCRT / win32 threads）下：

- 线程模型为 win32，**根本没有 libwinpthread**；
- GUI/CLI 用 `-static` 链接，libgcc / libstdc++ / wx 全部链入 EXE；
- 实测 `objdump -p bin\validproxy.exe` / `validproxy-cli.exe` 的导入表只有 KERNEL32 / msvcrt 与系统 DLL
  （USER32 / GDI32 / ADVAPI32 / ws2_32 / gdiplus 等），**无任何 MinGW 运行时 DLL**。

因此 EXE 双击即可运行，无需额外文件；历史版本所需的 MSVCRT 版
`libwinpthread-1.dll`（见 `docs/bugfix/2026-11-17-Bugfix-Win7-UCRT-libwinpthread-v1.0.md`）
仅对旧 posix-threads 工具链有意义，新工具链不再使用。

### 2.1 历史 DLL（仅作背景）

旧工具链（clion-msvcrt / mingw-ucrt，posix 线程）曾需随附 MSVCRT 版 `libwinpthread-1.dll`
（`resources/win7/libwinpthread-1.dll`，65,442 B，sha256 `B3AD954E…`，导入仅 KERNEL32+msvcrt），
因为它自带的版本依赖 UCRT API set，未装 KB2999226 的 Win7 SP1 无法加载。
该文件仍保留在仓库中，供维护旧工具链构建时使用，但**不进入发布资产**。

## 3. 打包 / 发布步骤（标准流程）

```powershell
# 1) 构建（Release / msvcrt-win32 工具链 / 全静态 EXE）
cmake --preset release
cmake --build --preset release --parallel 6 --target validproxy validproxy-cli
#    确认版本戳干净（无 -dirty）：工作区需先提交，必要时重新 configure

# 2) 产物核验：导入表只有系统 DLL，无 MinGW 运行时 DLL
objdump -p bin\validproxy.exe | findstr "DLL Name"
objdump -p bin\validproxy-cli.exe | findstr "DLL Name"
powershell -File scripts\check-win7-deps.ps1 -Path bin -IncludeDlls

# 3) 打包（仅自检，不再生成 zip 资产）
powershell -File scripts\package-win7-release.ps1

#   → 校验 EXE 无 MinGW DLL 依赖后输出 "[package] OK — no runtime DLLs required"

# 4) 上传 / 替换资产（只有 2 个）
gh release upload v1.6.2 bin\validproxy.exe bin\validproxy-cli.exe --clobber

# 5) 核对远端
gh release view v1.6.2 --json assets
```

## 4. 验收清单

- [ ] `bin/validproxy.exe` 导入表**无** `libwinpthread-1.dll` / `libstdc++-6.dll` / `libgcc_s_seh-1.dll` / `wx*.dll`。
- [ ] `scripts/check-win7-deps.ps1` 返回 0。
- [ ] `scripts/package-win7-release.ps1` 返回 0 且不生成任何 zip。
- [ ] 远端**只有两个资产**：`validproxy.exe`（30 MB 级）与 `validproxy-cli.exe`（9 MB 级）。
- [ ] 发布说明中「包内容」描述与本规范一致（仅两个独立 EXE，无压缩包）。

## 5. 反面案例（2026-11-16 → 2026-11-27）

| 版本 | 情况 | 结果 |
| --- | --- | --- |
| 2026-11-16 首发 1.6.1 |  zip 里 DLL 是 UCRT 版 | Win7 启动即报缺失 `api-ms-win-crt-convert-l1-1-0.dll` |
| 2026-11-17 第一次替换 | zip 里塞了 exe + DLL + 模板 + README | 违反“压缩包只放 DLL”口径，用户指出 |
| 2026-11-17 第二次替换 | zip 仅含 MSVCRT `libwinpthread-1.dll` | 符合 v1.0 规范（当时正确） |
| 2026-11-27 v1.6.2 资产替换 | 新工具链已全静态，却仍按 v1.0 上传了 DLL zip | 用户指出多余；改 v1.1 规范，删除 zip，只留两个 EXE |

## 6. 关联文档

- `docs/reference/2026-11-26-Reference-MsvcrtWin32DefaultToolchain-v1.0.md`（新默认工具链）
- `docs/bugfix/2026-11-17-Bugfix-Win7-UCRT-libwinpthread-v1.0.md`（历史 DLL 的背景）
- `resources/win7/README.md`（历史 DLL 来源、sha256）
- `scripts/package-win7-release.ps1`（本规范的校验实现）
- `scripts/check-win7-deps.ps1`（部署闸门）
- `docs/2026-11-16-Reference-MinGW-Toolchain-Usage-v1.0.md`（工具链历史）
