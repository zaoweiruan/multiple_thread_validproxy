可以。下面这段我建议**直接完整复制给 OpenCode**。重点是要求它先检查现有项目，不要擅自破坏原有 wxWidgets 代码；同时要求测试失败后自动定位、修改、重新编译、重新测试，直到通过或明确报告无法解决。

你现在负责为当前 Windows C++ 项目建立一套完整的、可自动执行的 UI 自动化测试体系。

## 一、项目技术栈

当前项目：

* Windows
* C++
* MinGW-w64 GCC
* wxWidgets
* CMake
* vcpkg
* Catch2 3.x
* Windows UI Automation（UIA）
* CTest
* 批处理脚本（.bat）

目标不是只写测试代码，而是建立一套可以被 OpenCode 自动执行的完整开发闭环：

```text
修改代码
   ↓
vcpkg 安装/检查依赖
   ↓
CMake Configure
   ↓
GCC Build
   ↓
启动 wxWidgets Application
   ↓
Windows UI Automation
   ↓
执行 Catch2 UI Tests
   ↓
PASS / FAIL
   ↓
失败时分析日志、源码和测试结果
   ↓
自动修改代码
   ↓
重新 Build
   ↓
重新执行 UI Test
   ↓
直到 PASS，或者确认无法继续修复
```

---

# 二、首先检查当前项目

不要立即创建大量文件。

先检查当前项目：

1. 当前目录结构
2. 现有 CMakeLists.txt
3. vcpkg.json（如果存在）
4. wxWidgets 配置
5. GCC / MinGW-w64 配置
6. 现有 source / include / tests
7. 当前 Application 的启动方式
8. 当前 Application 的主窗口
9. 当前 wxWidgets 控件结构
10. 当前已有测试
11. 当前是否已经存在 CTest
12. 当前是否已经存在 Catch2
13. 当前是否已经存在 UI Automation 代码

先理解项目，再决定需要新增或修改哪些文件。

**禁止因为建立测试框架而重写现有 Application。**

除非确实为了 UI 自动化测试需要，否则不要修改业务逻辑。

---

# 三、依赖管理

使用 vcpkg 管理 Catch2 3.x。

优先采用 manifest 模式。

如果项目没有 vcpkg.json，则创建。

依赖至少包含：

```text
catch2
```

如果当前项目已有其他 vcpkg 依赖，不要删除。

确保 Catch2 使用 3.x。

不要使用 Catch2 2.x。

不要要求用户手工下载 Catch2 源码。

---

# 四、CMake 构建系统

建立清晰的 CMake 测试结构。

目标至少包括：

```text
Application
UITests
```

其中：

```text
Application
    ↓
真正的 wxWidgets GUI 程序

UITests
    ↓
Catch2 3.x
    ↓
Windows UI Automation
    ↓
操作 Application
```

CMake 应支持：

```text
cmake configure
cmake build
ctest
```

必须能够通过 CTest 运行 UI 测试。

例如最终应该可以做到：

```bat
cmake --preset ...
cmake --build ...
ctest --test-dir build --output-on-failure
```

如果当前项目没有 CMake Presets，可以建立合理的：

```text
CMakePresets.json
```

并区分：

```text
Debug
Release
```

优先保证 Debug UI Test 可以稳定运行。

---

# 五、Windows UI Automation

UI 自动化必须使用 Windows UI Automation API。

核心接口包括：

```text
IUIAutomation
IUIAutomationElement
IUIAutomationCondition
IUIAutomationInvokePattern
IUIAutomationValuePattern
IUIAutomationTextPattern
```

使用：

```cpp
#include <windows.h>
#include <UIAutomation.h>
```

并正确链接：

```text
uiautomationcore
ole32
oleaut32
```

不要使用纯坐标点击作为主要测试方法。

禁止测试依赖：

```text
屏幕绝对坐标
鼠标移动到固定坐标
固定窗口位置
固定分辨率
```

UI Automation 必须优先通过：

```text
AutomationId
Name
ControlType
ClassName
```

查找控件。

---

# 六、为 wxWidgets 建立稳定的 UI 测试标识

检查当前 wxWidgets Application。

对于需要测试的控件，尽可能建立稳定、唯一、可维护的标识。

例如：

```text
MainWindow
FilePathEdit
KeywordEdit
SearchButton
ClearButton
ResultList
StatusLabel
```

不要让测试依赖：

```text
第一个 Button
第二个 Edit
屏幕坐标
控件在窗口中的位置
```

如果 wxWidgets 的原生 UI Automation 暴露信息不足，研究并采用适合当前项目的稳定定位方式。

原则：

```text
业务代码稳定
测试标识稳定
UI 布局可以改变
测试不应该因为按钮移动几十像素而失败
```

---

# 七、建立 UI Test Framework

不要让每个测试直接重复 COM/UI Automation 底层代码。

建立独立的 UI Test Framework。

建议结构：

```text
tests/
└── ui/
    ├── framework/
    │   ├── UIAutomation.h
    │   ├── UIAutomation.cpp
    │   ├── Application.h
    │   ├── Application.cpp
    │   ├── UIElement.h
    │   ├── UIElement.cpp
    │   ├── Assertions.h
    │   ├── Assertions.cpp
    │   ├── Wait.h
    │   └── Wait.cpp
    │
    ├── TestMain.cpp
    ├── TestMainWindow.cpp
    ├── TestSearch.cpp
    └── TestClear.cpp
```

封装至少提供：

```text
StartApplication()
StopApplication()

FindWindow()
FindElement()
FindButton()
FindEdit()
FindText()
FindList()

Click()
SetText()
GetText()

WaitForWindow()
WaitForElement()
WaitForText()

Exists()
IsEnabled()
IsVisible()

Screenshot()

KillApplication()

AssertExists()
AssertText()
AssertValue()
```

---

# 八、Catch2 测试

使用 Catch2 3.x。

测试应该类似：

```cpp
TEST_CASE("Application starts")
{
    REQUIRE(app.Start());

    auto window = app.FindWindow("...");
    REQUIRE(window.Exists());
}
```

按钮测试：

```cpp
TEST_CASE("Search button")
{
    REQUIRE(app.Start());

    auto window = app.MainWindow();

    auto button =
        window.FindButton("SearchButton");

    REQUIRE(button.Exists());
    REQUIRE(button.IsEnabled());

    REQUIRE(button.Click());
}
```

输入测试：

```cpp
TEST_CASE("Search keyword")
{
    REQUIRE(app.Start());

    auto edit =
        app.MainWindow()
           .FindEdit("KeywordEdit");

    REQUIRE(edit.Exists());

    REQUIRE(
        edit.SetText("test")
    );

    REQUIRE(
        edit.GetText() == "test"
    );
}
```

---

# 九、测试必须是真实 UI 测试

不要把 UI 测试偷偷改成调用业务函数。

例如：

错误：

```cpp
SearchFiles();
```

这不是 UI 测试。

正确：

```text
找到 KeywordEdit
        ↓
SetText()
        ↓
找到 SearchButton
        ↓
Click()
        ↓
等待 ResultList
        ↓
检查 UI 显示结果
```

必须从真实 Application 的 GUI 入口执行。

---

# 十、测试 Application 生命周期

测试框架必须能够：

```text
启动 Application
    ↓
等待主窗口
    ↓
执行测试
    ↓
关闭 Application
```

测试失败时必须尝试：

```text
关闭 Application
```

如果 Application 卡死：

```text
TerminateProcess
```

防止 CTest 永久挂起。

所有 UI 测试都必须有 timeout。

禁止无限等待。

例如：

```text
启动超时：10 秒
窗口查找：10 秒
控件查找：5 秒
普通 UI 操作：5 秒
整个测试：60 秒
```

根据实际项目合理调整。

---

# 十一、测试失败时自动截图

任何重要 UI 测试失败时，自动保存截图。

目录：

```text
test-results/
└── screenshots/
```

例如：

```text
test-results/
├── screenshots/
│   ├── TestSearch_failed.png
│   ├── TestClear_failed.png
│   └── TestMainWindow_failed.png
│
├── logs/
│   └── ui-test.log
│
└── results/
    └── junit.xml
```

如果 Windows UI Automation 可以获取控件树信息，也输出失败时的：

```text
Window Name
AutomationId
ControlType
ClassName
ProcessId
```

方便 OpenCode 分析。

---

# 十二、CTest

将 UI 测试注册到 CTest。

例如：

```text
ctest --test-dir build --output-on-failure
```

必须能够看到：

```text
Test #1: UI_MainWindow
Test #2: UI_Search
Test #3: UI_Clear
```

最终输出：

```text
100% tests passed
```

或者：

```text
Failed tests:
...
```

CTest 返回非 0 时，整个测试命令必须失败。

---

# 十三、生成测试结果

Catch2 使用 JUnit 或其他适合 CI 的格式输出结果。

建议：

```text
test-results/results.xml
```

同时保留控制台输出。

测试失败时不要只输出：

```text
FAILED
```

必须尽可能输出：

```text
测试名称
失败步骤
控件名称
AutomationId
ControlType
期望值
实际值
异常
Application 状态
截图路径
```

---

# 十四、自动化批处理

建立：

```text
scripts/
    build.bat
    test-ui.bat
    build-and-test.bat
```

最终必须可以通过一个命令执行完整流程：

```bat
scripts\build-and-test.bat
```

流程：

```text
[1] 检查工具链
[2] 检查 vcpkg
[3] 检查依赖
[4] CMake Configure
[5] Build
[6] 启动/执行 UI Tests
[7] CTest
[8] 输出结果
[9] 返回正确 exit code
```

成功：

```text
exit code 0
```

失败：

```text
exit code != 0
```

---

# 十五、OpenCode 自动修复循环

建立一个明确的自动测试循环。

每次修改 Application 后：

```text
1. Build
2. 如果 Build 失败：
   - 分析编译错误
   - 修改代码
   - 回到 1

3. Build 成功：
   - 执行 CTest UI Tests

4. 如果 UI Test PASS：
   - 结束本轮

5. 如果 UI Test FAIL：
   - 分析 Catch2 输出
   - 分析 CTest 输出
   - 分析 UI Automation 日志
   - 查看截图
   - 定位失败控件
   - 判断是：
       A. Application bug
       B. UI Automation 定位问题
       C. 测试本身错误
       D. 环境问题

6. 如果属于 Application bug：
   - 修改 Application
   - 回到 1

7. 如果属于 Test Framework bug：
   - 修改测试框架
   - 回到 1

8. 如果属于 Test Case bug：
   - 修正 Test Case
   - 回到 1

9. 如果属于环境问题：
   - 不要无限修改业务代码
   - 输出明确诊断
   - 停止自动修复
```

---

# 十六、自动修复必须有限制

不要无限循环。

默认：

```text
MAX_RETRY = 5
```

即：

```text
Build
 ↓
Test
 ↓
Fix
 ↓
Build
 ↓
Test
```

最多自动循环 5 次。

如果 5 次仍然失败：

```text
AUTO_FIX_FAILED
```

并输出：

```text
最后一次 Build 输出
最后一次 Test 输出
失败测试
失败控件
截图
推测原因
已经修改的文件
建议下一步
```

禁止无限修改项目。

---

# 十七、不要为了让测试通过而修改测试断言

这是非常重要的规则。

例如原测试：

```cpp
REQUIRE(result == "100");
```

Application 实际错误返回：

```text
50
```

不要简单修改测试为：

```cpp
REQUIRE(result == "50");
```

除非经过代码和需求分析确认原测试预期错误。

原则：

```text
测试失败
    ↓
先检查 Application
    ↓
再检查测试
```

不能通过降低测试标准来制造 PASS。

---

# 十八、运行环境

UI Test 必须运行在真实 Windows Desktop Session 中。

不要设计成：

```text
Windows Service
```

也不要依赖：

```text
RDP 最小化后的不可见 GUI
```

除非当前项目已经有明确的特殊运行环境。

UI Automation 需要真实桌面环境。

---

# 十九、OpenCode 操作规则

你现在不仅是代码生成器，还需要负责测试闭环。

每次完成代码修改后，主动执行：

```bat
scripts\build-and-test.bat
```

不要只告诉我：

```text
“代码应该可以运行”
```

必须实际 Build。

必须实际执行测试。

必须根据真实输出判断：

```text
PASS
```

或者：

```text
FAIL
```

如果失败，分析并继续修复。

---

# 二十、最终目标

完成后，我应该可以在项目根目录执行：

```bat
scripts\build-and-test.bat
```

看到类似：

```text
========================================
 wxWidgets UI Test
========================================

[1/5] Checking toolchain...
[PASS] MinGW-w64
[PASS] CMake
[PASS] vcpkg

[2/5] Installing dependencies...
[PASS] Catch2 3.x

[3/5] Building...
[PASS] Application
[PASS] UITests

[4/5] Running UI Tests...

[PASS] Application starts
[PASS] Main window
[PASS] Search input
[PASS] Search button
[PASS] Search operation
[PASS] Clear button

[5/5] CTest...

100% tests passed

========================================
 ALL TESTS PASSED
========================================
```

失败时：

```text
========================================
 UI TEST FAILED
========================================

Test:
    Search operation

Step:
    EXPECT_TEXT(ResultLabel)

Expected:
    Search completed

Actual:
    Search failed

Screenshot:
    test-results/screenshots/SearchOperation_failed.png

Log:
    test-results/logs/ui-test.log

CTest exit code:
    8

========================================
```

---

# 二十一、实施顺序

严格按照下面顺序实施：

```text
Phase 1
检查现有项目
        ↓
Phase 2
配置 vcpkg + Catch2 3.x
        ↓
Phase 3
配置 CMake + CTest
        ↓
Phase 4
实现 UI Automation Framework
        ↓
Phase 5
实现 Application 生命周期管理
        ↓
Phase 6
实现第一个 UI Test
        ↓
Phase 7
实现截图、日志、JUnit XML
        ↓
Phase 8
实现 build-and-test.bat
        ↓
Phase 9
实际运行
        ↓
Phase 10
修复所有失败
        ↓
Phase 11
再次完整运行
        ↓
Phase 12
最终报告
```

---

# 二十二、最终交付

完成后输出：

1. 新增文件列表
2. 修改文件列表
3. vcpkg 依赖
4. CMake 配置说明
5. Catch2 配置
6. CTest 配置
7. UI Automation Framework 说明
8. UI Test 用例列表
9. 构建命令
10. 完整测试命令
11. 最终测试结果
12. 如果存在失败，说明失败原因和未解决问题

最重要的是：

**不要停留在“代码已经写好”的状态。**

必须实际执行：

```bat
build
    ↓
Application
    ↓
UI Automation
    ↓
Catch2
    ↓
CTest
```

直到测试真正通过。

如果当前环境缺少某个工具，先检查 PATH、vcpkg、CMake、GCC、Windows SDK 等实际状态。

不要凭猜测报告成功。

**只有实际执行并得到成功结果，才能报告 PASS。**
