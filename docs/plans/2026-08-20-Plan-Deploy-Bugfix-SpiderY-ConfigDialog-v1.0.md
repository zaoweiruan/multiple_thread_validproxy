# 部署/安装方案：SpiderY nil-panic 修复 + ConfigDialog 全路径显示修复

- **日期**：2026-08-20
- **类型**：部署方案（Deploy Plan）
- **关联 bugfix**：
  - `docs/bugfix/2026-08-11-Bugfix-Reality-SpiderY-nil-Panic-v1.0.md`
  - `docs/bugfix/2026-08-19-Bugfix-ConfigDialog-FilePropertyShowFullPath-v1.0.md`
- **适用版本**：v1.0.3（当前 HEAD）
- **制作者**：Architect

---

## 1. 修复总览

| 项 | 模块 | 修复类型 | 重建产物 | 数据/配置影响 | 外部依赖 |
|----|------|----------|----------|---------------|----------|
| SpiderY nil-panic | `XrayApi`（gRPC 注入） / `ProxyBatchTester`（U4 移除） | 源码（protobuf 编码补全） | `validproxy.exe` + `validproxy-cli.exe` | 无（仅 wire 编码） | 外部 `xray.exe`（已用方案 B 规避升级） |
| ConfigDialog 全路径显示 | `ConfigDialog`（UI） | 源码（绕过 wxWidgets 3.3.3 bug） | `validproxy.exe`（仅 GUI） | 无（仅显示层） | 本地 wxWidgets 3.3.3（编译进 exe） |

**共性结论**：两个修复均为**纯重新编译生效**类修复，**不涉及**数据库 schema 迁移、`config.json` 格式变更、外部二进制下载。部署动作 = 重新构建 → 覆盖运行时 exe（及配套 wxWidgets DLL）→ 验证。

---

## 2. 架构与影响面分析

### 2.1 SpiderY（核心引擎层）

- **代码落地确认**：
  - `include/XrayApi.h` L109 `encodePackedInt64Field`、L179 `parseSpiderYParams` 声明存在。
  - `src/XrayApi.cpp` L330 `encodePackedInt64Field` 实现；L936–951 `encodeRealitySettings` **无条件编码 field 27**（10 元素数组，spiderX 无参时全 0）；L970 `parseSpiderYParams` 实现。
  - `src/ProxyBatchTester.cpp` 的 `preGenerateConfigs`（L463/527/570/622）已**无任何 REALITY/gRPC 降级 skip 分支**（U4 已移除）。
- **运行时影响**：REALITY 节点经 gRPC `AddOutbound` 注入后，`SpiderY` 恒为 10 元素数组，`reality.go:273` 等 10 处索引访问不再越界 → 批量测试中死节点触发验证失败分支时 xray 进程不再崩溃。
- **回归测试**：`tests/test_xray_api_direct.cpp` 新增 `ParseSpiderYParams` / `EncodeRealitySettingsSpiderY` / `EncodeRealitySettingsSpiderYAlwaysPresent` 3 个用例（随 `XrayApiDirectTest` 运行）。

### 2.2 ConfigDialog（UI 层）

- **代码落地确认**：`src/ui/ConfigDialog.cpp` L42–159 对全部 8 个 `wxFileProperty`（`database_path` / `proxy_xray_executable` / `proxy_xray_asset_dir` / `proxy_template_config_path` / `proxy_singbox_executable` / `proxy_singbox_template_config_path` / `sync_source_db` / `sync_target_db`）均调用 `ChangeFlag(wxPGFlags::ShowFullFileName, true)`，并保留原 `wxPG_FILE_SHOW_FULL_PATH` 的 `SetPropertyAttribute` 调用（向后兼容）。
- **运行时影响**：仅配置编辑窗口显示行为变化，`config.json` 序列化值不变（绝对路径原样写入）。不影响 CLI（`validproxy-cli.exe` 不启动 GUI）。
- **回归测试**：无自动化 GUI 测试（涉及渲染）；靠构建通过 + 手动 UI 验收。

### 2.3 影响边界

- **不受影响**：`DatabaseHelper` / `SubitemUpdaterV2` / `ShareLink` / 网络监控 / 日志系统 / `SubitemUpdaterV2::isValidProxy` 等数据治理链路。
- **无跨模块耦合变更**：两处修复均局部闭环，无公共 API 签名变更（文档1 的 `encodePackedInt64Field` / `parseSpiderYParams` 为 `XrayApi` 新增静态方法，仅内部调用）。

---

## 3. 环境与依赖约束（前置条件）

| 约束 | 说明 | 处理 |
|------|------|------|
| 工具链 | Windows MinGW/GCC + CMake + Ninja | 遵循 AGENTS.md §4.1 |
| **wxWidgets 版本锁定** | 文档2 修复直接依赖 `wxPGFlags::ShowFullFileName`（0x00100000），该枚举值来自**本地 wxWidgets 3.3.3 源码**（`include/wx/propgrid/property.h:395`）。若 wxWidgets 升级或 DLL 版本不一致，该 bit 行为可能变化 | **部署机器必须使用与编译机一致的 wxWidgets 3.3.3 构建产物**（exe + 运行 DLL）。若静态链接则仅 exe 即可；若动态链接须同步 `bin/` 下 wxWidgets DLL |
| vcpkg 路径 | 2026-08-18 迁移至 `D:/vcpkg`，Boost 由 vcpkg 管理 | 配置命令须显式 gcc + 覆盖 `CMAKE_CXX_FLAGS_DEBUG` + `-O coff`（详见 `docs/bugfix/2026-08-18-Bugfix-Build-Deps-VcpkgMigration-v1.0.md`） |
| 外部 `xray.exe` | v2rayN 外部二进制（`E:\v2rayN-windows-64\bin\xray\xray.exe`） | 文档1 方案 B 已规避升级；保持现状即可 |
| 运行时目录 | `bin/worker/`（存放 `guindb.db`、`validproxy.exe` 副本） | AGENTS.md §4.1 红线：**CMake 构建不得自动写入 `bin/worker/`**，部署为手动/脚本步骤 |

---

## 4. 构建方案（PowerShell）

> 默认终端 PowerShell；并行度按机器内存调整（内存受限时降为 2）。

```powershell
# 1. 配置（显式 gcc + Debug；遵循 2026-08-18 vcpkg 迁移备注的特殊 flag）
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug

# 2. 编译（文档1 文档基线 327/327；文档2 基线 30/30 单测；以当前 HEAD 实际为准）
cmake --build build --parallel 8
```

- 预期：`0 error`，生成 `build/validproxy.exe` 与 `build/validproxy-cli.exe`。
- 若动态链接 wxWidgets，构建期 `copy_wx_dlls()` 已将 3.3.3 运行 DLL 拷贝至 `bin/`（**非** `bin/worker/`）。

---

## 5. 部署方案

### 5.1 目标目录关系

```
build/validproxy.exe            →  编译产物（GUI）
build/validproxy-cli.exe        →  编译产物（CLI）
bin/                           →  构建期 DLL/资源输出（含 wxWidgets 3.3.3 DLL）
bin/worker/validproxy.exe      →  生产运行时副本（实际被用户启动）
bin/worker/guindb.db           →  生产数据库（本次不触碰）
bin/config.json                →  生产配置（本次不触碰）
```

### 5.2 实施步骤

```powershell
# 0. 停止运行中的实例（避免文件锁定导致覆盖失败）
#    （任务管理器结束 validproxy.exe，或脚本 taskkill /IM validproxy.exe）

# 1. 备份当前运行时 exe（回滚用）
Copy-Item bin/worker/validproxy.exe bin/worker/validproxy.exe.bak-20260820

# 2. 覆盖 GUI 主程序（含 SpiderY + ConfigDialog 双修复）
Copy-Item build/validproxy.exe bin/worker/validproxy.exe -Force

# 3. 覆盖 CLI（含 SpiderY 修复；CLI 不涉及 ConfigDialog）
Copy-Item build/validproxy-cli.exe bin/worker/validproxy-cli.exe -Force

# 4. 若动态链接 wxWidgets：同步运行 DLL（版本必须与编译机 3.3.3 一致）
#    copy_wx_dlls() 已输出到 bin/，按需拷至 bin/worker/
#    Get-ChildItem bin/*.dll | Copy-Item -Destination bin/worker/ -Force
```

### 5.3 红线注意

- **禁止**在 CMake `POST_BUILD` 中自动写入 `bin/worker/`（AGENTS.md §4.1）：运行中的 `validproxy.exe` 文件锁会导致 `POST_BUILD` 阶段失败。部署统一走上述手动/脚本步骤。
- **禁止**覆盖 `bin/worker/guindb.db` 与 `bin/config.json`：本次修复无数据/配置变更，误覆盖将丢失生产状态。

---

## 6. 验证方案

### 6.1 单元 / 集成（ctest）

```powershell
# 全量回归（参考基线：文档1 22/22，文档2 30/30；以当前 HEAD 实际为准）
ctest --test-dir build

# 文档1 定向：SpiderY 3 个新用例归属 XrayApiDirectTest
ctest --test-dir build -R XrayApiDirectTest -V
```

断言要点：
- `ParseSpiderYParams`：`/?p=3-5` → `[0]=3 [1]=5` 等；`/` → 全 0；非法值忽略。
- `EncodeRealitySettingsSpiderY`：含 field 27（tag `0xDA 0x01`，len `0x0A`），payload `[3,5,0,0,0,0,0,0,0,0]`。
- `EncodeRealitySettingsSpiderYAlwaysPresent`：无 `spiderX` 时 field 27 仍存在且 10 字节全 0。

### 6.2 端到端（CLI 批量 REALITY）

```powershell
# 对含 REALITY 节点的订阅做连通性测试，观察 xray 日志无 reality.go:273 panic
.\build\validproxy-cli.exe -T <reality-sub-id>
# 或默认全量批量
.\build\validproxy-cli.exe
```

成功判据：`bin/worker/xray_stderr_<socksPort>.log` 中**不再出现** `reality.go:273` 越界 panic；批量测试正常收尾（实例不崩溃）。

### 6.3 GUI 手动验收（文档2）

1. 启动 `bin/worker/validproxy.exe` → 打开「设置 / 配置」窗口（`ConfigDialog`）。
2. 确认 8 个文件路径字段（数据库路径、xray 可执行文件、资产目录、模板路径、sing-box 可执行文件/模板、同步源/目标库）**显示完整绝对路径**（如 `E:/eclipse_workspace/.../bin/worker/guiNDB.db`），而非仅文件名。
3. 修改某路径并保存，确认 `bin/config.json` 写入值不变（仅显示层修复）。

---

## 7. 回滚方案

因无数据/配置变更，回滚 = 恢复旧 exe：

```powershell
# 停止运行实例
# 恢复备份
Copy-Item bin/worker/validproxy.exe.bak-20260820 bin/worker/validproxy.exe -Force
# （CLI 同法，或保留新 CLI——CLI 未引入回归风险，可不必回滚）
```

回滚后重跑 §6.1 ctest 非必需（旧产物本身已通过历史基线）；如不确定，可重跑全量 ctest 确认旧产物环境状态。

---

## 8. 上游依赖说明（后续可选）

- **文档1 方案 A（Xray-core 补丁）未部署**：`reality.go` `UClient` 入口对 `len(SpiderY) < 10` 补 `make([]int64,10)`。部署前提 = 本机装 Go 工具链并重编译 `xray.exe`。当前方案 B（应用侧编码 field 27）已足够，方案 A 作为后续增强（同时保护其他不发送 `spider_y` 的 gRPC 客户端）。
- **文档2 wxWidgets 上游 bug 未修复**：`src/propgrid/props.cpp:2007` `DoSetAttribute` 中 `wxPGPropertyFlags_ShowFullFileName`（`Reserved_1`=0x10000000）与 `ValueToString` 检查的 `wxPGFlags::ShowFullFileName`（0x00100000）bit 不匹配。本机直接 `ChangeFlag` 绕过。若升级 wxWidgets 版本，需重新确认该 flag 行为（届时 `SetPropertyAttribute(wxPG_FILE_SHOW_FULL_PATH,...)` 可能已自愈，两处设置叠加仍安全）。

---

## 9. 实施检查清单

- [ ] 确认当前 HEAD 已含两处修复源码（见 §2 行号）
- [ ] 环境满足 §3 约束（wxWidgets 3.3.3、vcpkg `D:/vcpkg`、gcc 配置）
- [ ] 执行 §4 构建，`0 error`
- [ ] 执行 §6.1 ctest 全绿（记录实际 N/N）
- [ ] 停止运行时实例
- [ ] 备份 `bin/worker/validproxy.exe` → `.bak-20260820`
- [ ] 覆盖 `bin/worker/validproxy.exe` + `validproxy-cli.exe`
- [ ] （动态链接）同步 wxWidgets 3.3.3 DLL
- [ ] §6.2 CLI 批量 REALITY 验证：xray 日志无 `reality.go:273` panic
- [ ] §6.3 GUI 手动验收：ConfigDialog 8 路径全路径显示
- [ ] 登记本文档路径至 `docs/INDEX.md`
