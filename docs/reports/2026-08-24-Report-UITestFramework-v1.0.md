# UI 自动化测试体系落地报告

> **日期**: 2026-08-24 ｜ **类型**: Delivery Report ｜ **状态**: completed ｜ **修订**: v1.1（补录架构师独立验收/偏差裁决/规范集成，见 §六/§七）
> **依据**: `docs/plans/2026-08-24-Plan-UITestFramework-v1.0.md`（承接 Spec-install-test-Framework）
> **红线达成**: 零业务代码改动（src/ 仅只读）；沙箱隔离，生产库零触碰

## 一、最终验证结果（证据）

### 一键流水线（Step 10.1）

```text
scripts\build-and-test.bat
[3/5] Building... [PASS] Application + UITests
[4/5] Running UI Tests...
1/3 Test #33: UI_MAINWINDOW   Passed  1.74 sec
2/3 Test #34: UI_SEARCH       Passed  1.85 sec
3/3 Test #35: UI_CLEAR        Passed  1.25 sec
100% tests passed, 0 tests failed out of 3
======================================== ALL TESTS PASSED ========================================
exit=0
```

### 全量回归（Step 10.3）

```text
ctest --test-dir build --output-on-failure
97% tests passed, 1 tests failed out of 35
FAILED: 17 - NetworkMonitorTest（已知环境抖动白名单项：全量负载下偶发失败，
单独复跑 Passed 7.88s。既有 GTest 32 项全部不受影响）
```

### 破坏性演练（Step 7.3）

```text
临时改坏 MainWindowName → [mainwindow] FAIL exit=42 → fixture 析构自动生成
test-results/ui-artifacts/failure_<ts>.png(68KB 截图) + .txt(50KB 控件树 UTF-8 dump)
→ 还原常量 → 3 用例全绿（16 assertions）。
```

## 二、交付清单（12 项）

| # | 项 | 内容 |
|---|-----|------|
| 1 | 新增框架 | `tests/ui/framework/`: Application(.h/.cpp 进程生命周期+WM_CLOSE 优雅关闭+Terminate 兜底+setUiTargetPid)、UIAutomation(.h/.cpp COM RAII 单例+init/shutdown 配对)、ComPtr.h、UIElement(.h/.cpp findBy/click/setText/getText/dumpTree)、Wait(.h/.cpp 轮询)、Diagnostics(.h/.cpp 截图+树dump+JUnit)、ArtifactsListener.h、Screenshot(.h/.cpp GDI+)、Fixtures.h(AppFixture: 沙箱启动/失败工件采集/PID 注册)、Paths(.h/.cpp GetModuleFileNameW 绝对根推导) |
| 2 | 新增用例 | `tests/ui/TestFrameworkUnit.cpp`(fw-unit)、TestDiscovery.cpp(discovery+dumptree)、TestMainWindow.cpp(mainwindow 冒烟)、TestSearch.cpp(search ValuePattern 写读回验)、TestClear.cpp(clear InvokePattern 真实按钮)、TestMain.cpp(Catch2 入口+Uia init/shutdown+listener 注册) |
| 3 | 标识固化 | `tests/ui/UIIds.h` — 实测钉死：主窗 Name=`validproxy - Proxy Manager` class=wxWindowNR；searchCtrl/dataviewCtrl；Log 按钮=`清空日志窗口`；**布局事实=垂直 splitter 无标签页**（计划假设修正，Tab* 常量标注 no-tabs） |
| 4 | 构建 | CMakeLists.txt 追加 `UITests` target（仅 vcpkg toolchain 存在时）+ 3 条 add_test(UI_MAINWINDOW/UI_SEARCH/UI_CLEAR, TIMEOUT 120, WORKING_DIRECTORY=repo root)；RUNTIME 输出至 tests/ |
| 5 | 依赖 | Catch2 3.x 经 `vcpkg install catch2:x64-mingw-static` classic 模式（manifest 偏差 D1 已记录于计划）；UIA 链接 uiautomationcore+uuid |
| 6 | 沙箱脚本 | `scripts/prepare-ui-sandbox.bat` — guiNDB_empty.db→ui-test.db 复制重置 + config.json 生成（绝对路径 database.path、network_monitor 关闭、check_auto_update_interval=false）+ xray/exe 存在性防弹窗守卫 D8 |
| 7 | 流水线脚本 | `scripts/build.bat`（标准 cmake -B build -G Ninja Debug，无 preset）、`test-ui.bat`（沙箱重置+ctest -R ^UI_）、`build-and-test.bat`（Spec §十四 9 步 5 段一键） |
| 8 | 忽略规则 | .gitignore += `/test-results/`、`/test/ui-sandbox/` |
| 9 | 沙箱数据 | `test/ui-sandbox/config.json` + `ui-test.db`（运行时生成，不入库） |
| 10 | 工件产物 | 失败时自动：`test-results/ui-artifacts/failure_<ts>.png/.txt`（截图+UTF-8 树）；成功时 JUnit XML 可选开关 |
| 11 | 文档 | 本报告 + 计划文档 checkbox/status 更新 + INDEX.md §8.2/§9 登记 + tracker 状态行 |
| 12 | 遗留/偏差 | 见下节 |

## 三、实施期关键修复（分类 A/B/C/D）

| 类别 | 问题 → 修复 |
|------|------------|
| B 框架 | Uia::init() 从未调用 → TestMain 显式初始化；静态析构晚于 CoUninitialize 致退出 AV → 新增 Uia::shutdown() 显式配对释放 |
| B 框架 | ArtifactsListener 枚举无关进程窗口崩溃 → setUiTargetPid 全局 PID 过滤 + saveArtifacts 改 ElementFromHandle 精准绑定 |
| B 框架 | Catch2 listener 在 fixture 析构杀 GUI 之后才触发，失败工件永远采不到 → 工件采集移入 AppFixture 析构（uncaught_exceptions 递增且窗口存活时立即截图+dump），破坏性演练实证生效 |
| B 框架 | MinGW libstdc++ wofstream 默认 locale 下写中文宽字符静默 badbit→0 字节文件 → 树 dump 统一 narrow() 转 UTF-8 后经窄 ofstream 写出（50KB 完整内容实证） |
| A 业务侧发现 | GUI 只解析空格分隔 `-c/--config <path>`（等号格式被静默忽略回退生产配置）→ 测试侧传参改空格分隔；相对路径被 GUI 以 exeDir 解析致 Configuration Error 弹窗 → Paths::root() 改 GetModuleFileNameW 绝对化（tests/ui 框架内修复，未动业务代码） |
| C 用例 | searchCtrl 为容器无 ValuePattern → 定位 wrapper 后子树内 ClassName="Edit" 操作（Edit Name 为空不能全局找，防误中日志区 Edit） |
| D 环境 | NetworkMonitorTest 全量负载偶发失败单独复跑通过（已知白名单项，与本次零业务改动无关） |

## 四、使用命令速查

```powershell
# 一键全流程（构建+沙箱重置+UI 三用例）
cmd /c scripts\build-and-test.bat

# 仅 UI 测试（先重置沙箱）
cmd /c scripts\test-ui.bat

# 手工单跑（需先 prepare-ui-sandbox.bat）
.\tests\UITests.exe "[mainwindow],[search],[clear]"

# 全量回归（含 GTest 32 项）
ctest --test-dir build --output-on-failure
```

## 五、遗留事项（二期候选）

1. DataView 自绘内容断言列（范围裁剪 D5）：当前仅断言控件存在性，单元格级文本校验留二期。
2. `/scripts/` 整目录在 .gitignore 中——三个 bat 入库需 `git add -f scripts/*.bat`（提交前用户确认）。
3. listener 的自动截图职责实际由 fixture 析构承担（设计偏差已记录于会话笔记），二期可清理 listener 或改造为进程存活期钩子。
4. NetworkMonitorTest 时序抖动为既有问题，建议另行专项治理。
5. 沙箱 config.json 未显式写 `xray.executable`（D8）：弹窗由 prepare-ui-sandbox.bat 存在性守卫兜底，实测两轮零弹窗；二期补显式路径。

## 六、架构师独立验收（v1.1 补录）

Coder 交付声明经 Architect 独立复核，不采信单方报告：

| 验收项 | 独立证据 | 结论 |
|--------|----------|------|
| CTest 注册 | `ctest -N`：共 35 项，UI_MAINWINDOW / UI_SEARCH / UI_CLEAR 列 #33–35 | ✅ |
| UI 三用例独立复跑 | `ctest -R "UI_"`：3/3 Passed（0.80s / 1.37s / 1.32s） | ✅ |
| 一键端到端实跑 | Architect 本机重跑 `scripts\build-and-test.bat`：工具链→依赖→构建→沙箱→测试五段全绿，ALL TESTS PASSED（三用例 5.08s），exit 0 | ✅ |
| 数据隔离红线 | 沙箱目录仅 `config.json + ui-test.db`；prepare 脚本注释显式声明红线；生产库零触碰 | ✅ |
| 文档登记持久化 | 报告/INDEX/tracker 逐处回读确认（会话期间 tracker 曾出现一次写入丢失异常，已重写并复核通过） | ✅ |

## 七、偏差裁决与规范集成（v1.1 补录）

### 偏差裁决

| 偏差 | 裁决 |
|------|------|
| bat 四件套实际平铺 `scripts\` 根，而非计划的 `scripts/ui_tests/` 子目录 | **接受** — 随项目既有平铺惯例（run_coverage.ps1 等）；bat 以 `%~dp0..` 锚定互调免改；`/scripts/` 整体 gitignore 下子目录不改变入库方式 |

### 规范集成（后续强制执行）

| 落点 | 内容 |
|------|------|
| `docs/DEV-PROCESS.md` v1.0 → **v1.1** | 新增「UI 自动化测试规范」章节：适用范围强制表 / TDD 定位先行 / UIIds.h 唯一定位源 / A·B·C·D 失败分类禁削断言 / 数据隔离红线 / 一键验收门禁 |
| `AGENTS.md` §4.1 | 新增命令 #6 `.\scripts\build-and-test.bat`（UI 变更交付前必须全绿）——AI 会话上下文自动注入，形成执行闭环 |
| `docs/INDEX.md` §3 | DEV-PROCESS 描述行同步 v1.1 摘要，保证总图可检索 |

> 执行链路：AI 会话启动注入 AGENTS.md（命令+路由）→ 检索 INDEX.md 定位 DEV-PROCESS.md v1.1 → 按规范开发。
