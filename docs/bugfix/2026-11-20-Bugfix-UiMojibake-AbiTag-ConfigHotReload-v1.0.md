---
title: "bugfix: 配置对话框「任务链」乱码 / 版本信息缺 ABI 标签 / xray 路径改后不生效"
type: bugfix
status: completed
date: 2026-11-20
module: ui-config
version: v1.0
---

# 配置对话框「任务链」乱码、版本缺 ABI、xray 路径热更新失效

## 1. 现象

| # | 现象 | 触发 |
|---|---|---|
| 1 | 配置对话框「自动任务」页的**「任务链」一行显示乱码**（随机汉字/方块） | 打开 `配置` → 自动任务页 |
| 2 | `--version` / 日志 / 关于对话框只显示编译器版本，看不出二进制是 **msvcrt** 还是 **ucrt** | 两套工具链产物同目录比较时无法区分 |
| 3 | 在配置对话框里改了 **xray 可执行文件路径**，点「确定」后跑测试**仍用旧路径**，必须重启进程 | 配置热更新 |

## 2. 根因

### 2.1 「任务链」乱码 —— 悬垂指针，不是编码问题

`src/ui/ConfigDialog.cpp` 旧实现：

```cpp
static const wchar_t* stepDisplayName(const std::string& step) {
    if (step == "update_all") return i18n::wx(i18n::StrId::ConfigDialog_UpdateAll);
    ...
}

void ConfigDialog::refreshAutoTaskChainDisplay() {
    display += stepDisplayName(stepOrder_[i]);
}
```

`i18n::wx()` 返回的是**临时 `wxString`**；`return` 语句结束，临时对象立即析构，
函数返回的是它内部缓冲区的指针（悬垂）。属性网格随后读这块已释放内存，
得到堆上的垃圾字节 —— 界面表现为乱码。编译器不会告警（`wxString` 有
隐式 `operator const wchar_t*()`）。

同一函数内 `sqlSchemeDisplayValue()` / `scopeDisplayName()` 返回类型是 `wxString`
（按值），所以只有「任务链」坏了。

### 2.2 版本信息缺 ABI

源码同一份、两套工具链（`clion-msvcrt` GCC 15.2 / `mingw-ucrt` GCC 16.2）产出的
exe **DLL 依赖不兼容**（混用即 `0xC0000139`，参见
`2026-11-20-Bugfix-TestRuntime-CrossToolchainDllPath-v1.0.md`）。原来的
`Compiler: 15.2.0` 只能区分编译器版本，无法一眼判断 ABI。

### 2.3 xray 路径改后不生效

`XrayManager` 是进程级单例，`getInstance(path, configDir, workers, db)` 旧实现：

```cpp
if (!instance_) instance_ = new XrayManager(xrayPath, configDir, workers, db);
return instance_;
```

第一个调用者的 `xrayPath_` / `workers_` 被永久缓存；之后 `AppController::saveConfig()`
虽然更新了 `config_`，但下一次批量测试拿到的仍是旧路径的池 —— 只有重启能生效。

### 2.4 配置值的中文（顺带修掉的同类问题）

排查「乱码」时发现 `ConfigDialog` 与配置文件的编码约定不一致：

- `ConfigReader::save()` 用 `boost::json::serialize()` 直接写出 `std::string` 字节 —— 配置
  文件是 **UTF-8** 契约。
- 但对话框读取用 `wxString(std::string)`、回写用 `wxString::ToStdString()`，二者都按
  **当前 ANSI 代码页**（中文 Windows = CP936）转换。于是中文路径/备注在界面上显示为
  乱码，回写时又变成非法 UTF-8 字节写进 JSON，下次 `load()` 解析失败。
- 该缺陷与「任务链」乱码表象相同、根因不同（编码 vs 悬垂指针），一并收口。

## 3. 修复

### 3.1 任务链（消除悬垂指针）

- 新增 `src/ui/TaskChainText.{h,cpp}`：`ui::taskChainStepName()` /
  `ui::taskChainDisplayText()`，**按值返回 `wxString`**，接口上不允许再返回裸指针。
- `ConfigDialog::refreshAutoTaskChainDisplay()` 改为：
  `propGrid_->SetPropertyValue("autotask_chain_display", ui::taskChainDisplayText(stepOrder_));`
- 删除 `stepDisplayName()`。

### 3.2 版本 ABI 标签

- `include/version.h.in` 增加编译期 ABI 宏：

  ```c
  #if defined(_UCRT)                // MinGW-w64 UCRT
  #define APP_CRT_ABI "ucrt"
  #elif defined(__MSVCRT_VERSION__) // MinGW-w64 MSVCRT
  #define APP_CRT_ABI "msvcrt"
  #else
  #define APP_CRT_ABI "unknown"
  #endif
  ```

  已实测：`E:\mingw64`（GCC 16.2）= `_UCRT` 已定义、`__MSVCRT_VERSION__=0xe00`；
  `E:\clion\bin\mingw\bin`（GCC 15.2）= 只有 `__MSVCRT_VERSION__=0x600`。

- 展示位置（三处一致）：
  - `validproxy-cli.exe -v` → `Runtime ABI: ucrt (UCRT)` / `msvcrt (MSVCRT)`
  - 启动 REPORT 日志 → `Compiler: 16.2.0 | ABI: ucrt`
  - 关于对话框 → 新增一行「运行时 ABI: {}」（`MainFrame.Dialog.AboutText`，中英同步，9 个占位符）

### 3.3 配置热更新（xray 池按配置失效）

`XrayManager::getInstance()` 现在比较 `xrayPath_` / `workers_`：

- **池空闲**（`getInstanceCount()==0`）：立即删除旧单例并按新配置重建 —— 改完配置
  下一次测试即用新路径，无需重启。
- **有实例在跑**：保留旧池并写 `WARN` 日志（不能拔掉正在使用它的任务），任务结束后
  下一次取值自然重建。GUI 在任务运行期间本就禁止保存配置，正常不会走到该分支。
- 新增只读访问器 `getXrayPath()`，供回归测试断言「新池确实带新路径」
  （不能比较裸指针：删除后分配器可能复用同一地址）。

### 3.4 配置值统一按 UTF-8 转换

`src/ui/ConfigDialog.cpp`：

- 读取配置 → 界面：`wxString(...)` → `wxString::FromUTF8(...)`（含 `pool_balancer_strategy`、
  日志级别、`accelerator_url`、`proxy_xray_executable` / `xray_asset_dir` /
  `singbox_*`、`network_monitor.checkUrls` 拼接、校验提示里的 URL）。
- 界面 → 配置：`.ToStdString()` → `.ToStdString(wxConvUTF8)`（18 处写回点）。

## 4. 回归测试

| 测试 | 位置 | 覆盖 |
|---|---|---|
| `TaskChainTextTest`（新增，gtest + wxString） | `tests/test_task_chain_text.cpp` | 中文/英文完整任务链文本、空链、未知步骤、200 次重复调用结果稳定（旧实现会读到不同垃圾字节） |
| `XrayManagerStartupTest.PathOrWorkerChangeInvalidatesIdlePool`（新增） | `tests/test_xray_manager_startup.cpp` | 路径不变复用；路径变 → 新池带新路径；worker 数变 → 池失效 |
| `I18nTest.HoleCountMatchesAcrossLanguages`（既有） | `tests/test_i18n.cpp` | 关于对话框 9 个占位符中英一致 |
| `ConfigReaderTest.SaveRoundTrip_KeepsUtf8NonAsciiValues`（新增） | `tests/test_config_reader.cpp` | 中文路径经 `save()`/`load()` 字节级往返，钉住「配置文件是 UTF-8」契约 |
| `UI_CHAINPREVIEW`（既有） | `tests/ui/TestChainPreview.cpp` | 真实 GUI 打开配置对话框不崩、预览框可见 |

## 5. 验证记录

| 项 | 命令 | 结果 |
|---|---|---|
| `clion-msvcrt` 构建 | `cmake --build build --parallel 6` | 成功 |
| `clion-msvcrt` ABI 输出 | `bin\validproxy-cli.exe -v` | `Compiler: 15.2.0` / `Runtime ABI: msvcrt (MSVCRT)` |
| `mingw-ucrt` 构建 | `cmake --build build-ucrt --parallel 6` | 成功（295 目标） |
| `mingw-ucrt` ABI 输出 | `build-ucrt\out\bin\validproxy-cli.exe -v` | `Compiler: 16.2.0` / `Runtime ABI: ucrt (UCRT)` |
| 新测试 | `ctest -R "TaskChainTextTest|XrayManagerStartupTest"` | 两套工具链均通过 |

> 注意：`XrayManagerStartupTest.FlashCrashDoesNotHang` 有 ~9.5 s 上限，依赖进程
> 启动速度；首次运行新复制的 fake helper 时若被杀软扫描拖慢可达 12.6 s
> （单跑复验通过），属环境抖动，与本次改动无关。

## 6. 变更文件

- `src/ui/TaskChainText.h` / `src/ui/TaskChainText.cpp`（新增）
- `src/ui/ConfigDialog.cpp`
- `include/version.h.in`、`src/main_cli.cpp`、`src/main_gui.cpp`、`src/ui/MainFrame.cpp`
- `resources/i18n/strings.json`、`src/i18n/StringTable.cpp`（生成）
- `include/XrayManager.h`、`src/XrayManager.cpp`
- `tests/test_task_chain_text.cpp`（新增）、`tests/test_xray_manager_startup.cpp`
- `CMakeLists.txt`（新源文件 + `TaskChainTextTest` 目标）
