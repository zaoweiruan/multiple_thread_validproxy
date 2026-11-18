---
title: "reference: 发布打包规范（Release Assets & 压缩包口径）"
type: reference
status: maintained
date: 2026-11-17
module: build-system
version: v1.0
---

# 发布打包规范 v1.0

> 目标读者：发布构建的维护者 / AI Agent。**后续所有版本发布一律遵照本文件执行。**
> 执行脚本：`scripts/package-win7-release.ps1`（本规范的可执行版本）。
> 变更本规范需同步更新脚本与本文件。

---

## 1. 发布资产（固定 3 个）

每个 release（tag 形如 `v1.6.1`）只上传这三项，不增不减：

| 资产名 | 来源 | 说明 |
| --- | --- | --- |
| `validproxy.exe` | `bin/validproxy.exe` | GUI 主程序（静态 wx / libgcc / libstdc++） |
| `validproxy-cli.exe` | `bin/validproxy-cli.exe` | CLI 入口 |
| `validproxy-<ver>-win64.zip` | `scripts/package-win7-release.ps1` 产出 | **只含 `libwinpthread-1.dll`**（见 §2） |

## 2. 压缩包口径（硬约束）

**压缩包内只放一个文件：`libwinpthread-1.dll`（MSVCRT 版，位于根目录、无子目录）。**

明确**不放**以下内容（历史版本曾误放，已纠正）：

- `validproxy.exe` / `validproxy-cli.exe`（已是独立资产，压缩包里重复会让用户拿到旧 exe）
- `config.json`、数据库（含用户绝对路径与 SQL，属于用户私有运行状态）
- `xray-config-template.json` / `singbox-config-template.json`（随仓库提供，不随包分发）
- `*.dll` 中除 `libwinpthread-1.dll` 之外的任何 DLL（`libgcc_s_seh-1.dll`、
  `libstdc++-6.dll`、`wx*.dll` 等，本版 exe 已静态链接）
- `README.txt`、`icons/`（说明与图标以仓库 README 和 exe 内嵌资源为准）

### 2.1 为什么只放这一个 DLL

`-static-libgcc -static-libstdc++` 之后，exe 的唯一动态依赖就是 `libwinpthread-1.dll`；
而两个工具链自带的该 DLL 都是 **UCRT 构建**（导入 `api-ms-win-crt-*`），未装
KB2999226 的 Win7 SP1 会加载失败（见
`docs/bugfix/2026-11-17-Bugfix-Win7-UCRT-libwinpthread-v1.0.md`）。
因此压缩包只承载这一枚 **MSVCRT 版** DLL，随 exe 放在同一目录即可运行。

### 2.2 DLL 来源与校验

| 项 | 值 |
| --- | --- |
| 文件 | `resources/win7/libwinpthread-1.dll`（来源与替代条件见 `resources/win7/README.md`） |
| 大小 | 65,442 字节 |
| sha256 | `B3AD954EC35305895E2370D0A1E3CA6A8203DB1D37C7C49CF5D06E72A5F0DB69` |
| 导入表 | 仅 `KERNEL32.dll` + `msvcrt.dll`（**不得**出现 `api-ms-win-crt-*` / `ucrtbase`） |
| 导出覆盖 | exe 需要的 23 个符号，含 `clock_gettime64` / `nanosleep64` / `pthread_cond_timedwait64` |

脚本在打包前会校验 sha256 与导入表；不满足即拒绝打包（退出码 1）。

## 3. 打包步骤（标准流程）

```powershell
# 1) 构建（Release / 静态 wx / MSVCRT 工具链）
cmake --build build-tc-clion-msvcrt-Release --parallel 6 --target validproxy validproxy-cli
#    确认版本戳干净（无 -dirty）：工作区需先提交，必要时重跑 cmake -S . -B <builddir>

# 2) 生成发布压缩包（只含 libwinpthread-1.dll）
powershell -File scripts/package-win7-release.ps1 -Version 1.6.1
#   → bin/validproxy-1.6.1-win64.zip（约 28 KB），脚本自检“条目数 == 1”

# 3) 部署闸门（Win7 可运行性）
powershell -File scripts/check-win7-deps.ps1 -Path bin -IncludeDlls

# 4) 上传 / 替换资产
gh release upload v1.6.1 bin\validproxy.exe bin\validproxy-cli.exe `
    bin\validproxy-1.6.1-win64.zip --clobber

# 5) 核对远端
gh release view v1.6.1 --json assets
```

## 4. 验收清单

- [ ] `bin/validproxy-<ver>-win64.zip` 大小约 28 KB（不是 10 MB 级；>1 MB 说明又打包了 exe）。
- [ ] 压缩包条目数 **1**，名称 `libwinpthread-1.dll`。
- [ ] `scripts/check-win7-deps.ps1` 返回 0。
- [ ] 远端三个资产的大小与本地一致（`validproxy.exe` 30 MB 级、`validproxy-cli.exe` 9 MB 级、zip 28 KB 级）。
- [ ] 远端 zip 可下载且 sha256 与脚本输出一致。
- [ ] 发布说明中「包内容」描述与本规范一致（只含 libwinpthread）。
- [ ] 全新目录解压 zip + 放入 exe 后能启动（或直接 `bin/` 目录运行验证）。

## 5. 反面案例（2026-11-16 → 2026-11-17）

| 版本 | 压缩包内容 | 结果 |
| --- | --- | --- |
| 2026-11-16 首发 1.6.1 | 仅 `libwinpthread-1.dll`（35,913 B） | 内容正确，但该 DLL 是 **UCRT 版**，Win7 启动即报缺失 `api-ms-win-crt-convert-l1-1-0.dll` |
| 2026-11-17 第一次替换 | exe + DLL + 模板 + README（10,945,613 B） | DLL 修好了，但违反“压缩包只放 DLL”口径（用户指出） |
| 2026-11-17 第二次替换 | 仅 MSVCRT `libwinpthread-1.dll`（28,721 B） | ✅ 符合本规范 |

## 6. 关联文档

- `docs/bugfix/2026-11-17-Bugfix-Win7-UCRT-libwinpthread-v1.0.md`（为什么必须 MSVCRT 版）
- `resources/win7/README.md`（DLL 来源、sha256、替换条件）
- `scripts/package-win7-release.ps1`（本规范的可执行实现）
- `scripts/check-win7-deps.ps1`（部署闸门）
- `docs/2026-11-16-Reference-MinGW-Toolchain-Usage-v1.0.md`（工具链与静态 wx）
