# Spec：上传功能环境变量闸门（Upload Gate）

- 文档编号：Spec-UploadGate-v1.0
- 日期：2026-11-16
- 状态：已实现
- 关联：`docs/specs/2026-11-13-Spec-WebDavCloudSync-v1.0.md`（云端同步主体）

## 1. 需求

> 程序启动时，环境变量有 `VALIDPROXY_UPLOAD_TOKEN` 且为当前值，上传配置功能启用，否则置灰。

拆解：

1. 「上传配置功能」= 会把本地 `config.json` 与订阅列表**写向远端**的操作。
   读方向（`从云端同步订阅`、启动时拉取 `config.json`）不受影响。
2. 「当前值」= 构建时编入二进制的期望值（不是运行时任意非空值，
   否则闸门形同虚设）。
3. 「置灰」= 菜单项**保留可见但禁用**，让用户知道功能存在且被策略关闭。

## 2. 目标

| 编号 | 目标 |
|---|---|
| G1 | 未设置 / 值不匹配时，GUI「上传配置与订阅到云端」置灰且不可触发 |
| G2 | 值匹配时，菜单项启用，行为与改动前一致 |
| G3 | CLI `--cloud-push` 使用同一闸门（非交互环境无法"置灰"，改为拒绝执行） |
| G4 | 未配置期望值（默认构建）时，上传**永久禁用**，且不影响构建与其余功能 |
| G5 | 闸门状态可观测（日志），便于排障与验收 |

## 3. 输入

| 输入 | 来源 | 说明 |
|---|---|---|
| 期望 token | 构建期，优先级：`-DVALIDPROXY_UPLOAD_TOKEN=…` > `$ENV{VALIDPROXY_UPLOAD_TOKEN}` > `resources/upload_token.inc` > 空 | 空值 = 未配置 = 永久禁用 |
| 运行值 | 进程环境变量 `VALIDPROXY_UPLOAD_TOKEN` | 仅在 GUI 启动时读取一次决定菜单状态；执行前再次校验 |

## 4. 输出

| 输出 | 说明 |
|---|---|
| `build/include/UploadGateToken.h` | `#define VP_EXPECTED_UPLOAD_TOKEN "…"`，由 `resources/UploadGateToken.h.in` 生成 |
| GUI 菜单状态 | `ID_MENU_CLOUD_PUSH` 启用/置灰 |
| 启动日志 | `Upload gate: enabled` / `Upload gate: disabled (…)` |
| CLI 退出码 | 闸门关闭时 `--cloud-push` 返回 `3`，且不发起任何网络请求 |

## 5. 前置条件

- 构建期期望值已确定（否则功能不可用，属预期行为）。
- 运行程序的环境与设置期望值时的环境一致（同一 token 字符串）。

## 6. 边界条件与判定规则

| 场景 | 判定 |
|---|---|
| 期望值为空（未配置） | 一律禁用，**即使**运行值也非空 |
| 运行值缺失 | 禁用 |
| 运行值空串 | 禁用 |
| 运行值等于期望值 | 启用（大小写敏感、逐字节相等） |
| 前缀/后缀匹配 | 禁用（`strcmp` 全串比较，不做前缀匹配） |
| 大小写不同 | 禁用 |
| 首尾空白 | 禁用（不做 trim） |
| 运行值长度 ≥ 256 | 禁用（不截断，避免截断后误匹配） |
| 启动后修改环境变量 | 菜单状态不自动刷新（启动时求值）；但处理器与 CLI 执行前会重新求值，因此仍不会误放行 |

## 7. 设计

### 7.1 模块

- `include/service/UploadGate.h` / `src/service/UploadGate.cpp`
  - `expectedUploadToken()`：返回编入的期望值（空串表示未配置）。
  - `tokenEquals(expected, provided)`：**纯函数**，可单测；任一侧为空/`nullptr` 返回 `false`。
  - `uploadTokenMatches(provided)`：与期望值比较。
  - `isUploadEnabled()`：Windows 用 `GetEnvironmentVariableA`（不依赖 CRT `getenv`），
    其他平台用 `std::getenv`。
- `resources/UploadGateToken.h.in`：`configure_file` 模板。
- `scripts/provision_upload_token.py`：可选辅助脚本（`--token` / `--generate` / `--from-env`）。

### 7.2 接线

| 位置 | 行为 |
|---|---|
| `CMakeLists.txt` | 解析期望值 → `configure_file` 生成头文件；`UploadGate.cpp` 加入 `CORE_SOURCES` |
| `src/ui/MainFrame.cpp::initMenuBar()` | `fileMenu->Enable(ID_MENU_CLOUD_PUSH, service::isUploadEnabled())` |
| `src/ui/MainFrame.cpp::onMenuCloudPush()` | 执行前再校验，未启用则弹提示并 return（纵深防御） |
| `src/main_cli.cpp::runCloudSync()` | `mode == "cloudpush"` 且未启用 → 记 ERROR、返回 `3`，不触碰网络 |
| `src/main_gui.cpp` | 启动写一行 `Upload gate: …` 日志 |

### 7.3 为什么不复用 WebDAV 凭据

WebDAV 凭据是"访问远端所需"，上传闸门是"是否允许写远端"，语义不同：
即使凭据齐备，也应当能在不重新编译的情况下由环境决定是否开放写入。

### 7.4 安全定位

这是**能力开关**，不是安全边界：期望值可从二进制中还原（与 WebDAV 凭据同等），
目的是防止误操作上传，不用于对抗逆向。需要真正的权限控制时应放在服务端。

## 8. 非目标

- 不做运行期热更新菜单状态（启动时求值即可，符合需求原文）。
- 不改变拉取方向的行为。
- 不引入新的凭据存储机制。

## 9. 验收标准

| 编号 | 验收项 | 判定方式 |
|---|---|---|
| A1 | 未设置运行值时 GUI 菜单置灰 | 启动日志 `Upload gate: disabled …` |
| A2 | 值匹配时 GUI 菜单启用 | 启动日志 `Upload gate: enabled` |
| A3 | 未设置/不匹配时 CLI `--cloud-push` 拒绝 | 退出码 `3`，无网络请求 |
| A4 | 纯函数边界（空/`nullptr`/前缀/大小写/空白） | `UploadGateTest` 全绿 |
| A5 | 未配置期望值时构建成功且功能禁用 | 配置日志 `Upload gate: NOT provisioned`，测试中相关用例 skip |
| A6 | 既有功能不回归 | `ctest -E "UI_"` 通过率不低于基线 |

## 10. 测试计划

`tests/test_upload_gate.cpp`（14 例）：

| # | 用例 | 断言 |
|---|---|---|
| 1 | 期望值为空 | 不匹配 |
| 2 | 期望值为 `nullptr` | 不匹配 |
| 3 | 运行值为空串 | 不匹配 |
| 4 | 运行值为 `nullptr` | 不匹配 |
| 5 | 完全相等 | 匹配 |
| 6 | 前缀/后缀 | 不匹配 |
| 7 | 大小写不同 | 不匹配 |
| 8 | 首尾空白 | 不匹配 |
| 9 | `expectedUploadToken()` 非空指针 | 恒成立 |
| 10 | `uploadTokenMatches` 使用编入值 | 未配置时任何值都不匹配；已配置时仅精确值匹配 |
| 11 | 环境变量缺失 | `isUploadEnabled()` 为 `false` |
| 12 | 环境变量为空串 | `false` |
| 13 | 环境变量不匹配 | `false` |
| 14 | 环境变量等于编入值 | `true`；改大小写后 `false`（未配置构建则 skip） |

环境变量操作在 Windows 上使用 `SetEnvironmentVariableA`（与生产代码读取的
Win32 环境块一致）；该 MinGW 运行时的 `_putenv_s(name, "")` 会崩溃，故不使用。

## 11. 风险

| 风险 | 处置 |
|---|---|
| 期望值可被逆向还原 | 已在 §7.4 声明为能力开关；真正权限控制应在服务端 |
| 用户设了环境变量但未重新 configure，导致期望值未更新 | 配置期打印 `Upload gate: provisioned from <来源>`；未配置时明确打印 `NOT provisioned` |
| `resources/upload_token.inc` 误入库 | 已加入 `.gitignore`；`.inc` 仅作后备来源 |
| 启动后环境变量被改，菜单状态不刷新 | 处理器/CLI 执行前重新求值，不会误放行；菜单状态以启动时为准（符合需求） |

## 12. 实现与验证记录（2026-11-16）

构建：`cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug` + `cmake --build build --target validproxy validproxy-cli test_upload_gate` → EXIT=0。

| 验证项 | 结果 |
|---|---|
| 配置期从环境变量取值 | 设置 `VALIDPROXY_UPLOAD_TOKEN` 后 configure → `build/include/UploadGateToken.h` 内含该值 |
| 未配置构建 | `Upload gate: NOT provisioned (upload disabled)`；头文件为空串 |
| `UploadGateTest`（已配置） | **14/14 通过** |
| `UploadGateTest`（未配置） | 13 通过 + 1 skip（`IsUploadEnabledTracksTheCompiledToken`） |
| GUI 启动 + 匹配值 | 日志 `Upload gate: enabled`，主窗口正常 |
| GUI 启动 + 无环境变量 | 日志 `Upload gate: disabled (VALIDPROXY_UPLOAD_TOKEN absent or mismatched)`，主窗口正常 |
| CLI `--cloud-push` + 无环境变量 | `[cloud] upload disabled: …`，**退出码 3**，无网络请求 |
| CLI `--cloud-push` + 错误值 | 同上，退出码 3 |

### 12.1 本次 token 供给方式

按用户要求「生成强随机 token，同时写入 User 环境变量并重新配置构建」执行：

1. 用 `System.Security.Cryptography.RandomNumberGenerator` 取 32 字节熵，
   映射到 `[A-Za-z0-9]` 得到 **32 字符** token（非 `Random`、非时间种子）。
2. `[Environment]::SetEnvironmentVariable("VALIDPROXY_UPLOAD_TOKEN", <token>, "User")`
   写入 `HKCU\Environment`（用户级，非机器级）。
3. 在设置了该变量的 shell 中重新 `cmake -B build …`，
   CMake 走 `$ENV{VALIDPROXY_UPLOAD_TOKEN}` 分支编入。
4. **未**生成 `resources/upload_token.inc`：环境变量是唯一来源，避免两处真值。

已知限制：若在**没有该环境变量**的进程里重新 configure（例如变更前启动的 IDE），
CMake 会打印 `Upload gate: NOT provisioned (upload disabled)`，产物回到禁用状态。
此时在带变量的终端重新 configure 即可恢复，不需要改代码。

**未验证**：GUI 菜单项置灰的像素级/自动化断言（受 wx CRT 与 UI 自动化限制），
当前以启动日志 + 处理器二次校验作为证据。
