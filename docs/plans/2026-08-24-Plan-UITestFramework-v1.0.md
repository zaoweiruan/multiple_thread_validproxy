---
title: "Plan: wxWidgets UI 自动化测试体系建设 (Catch2 3.x + Windows UIA + CTest)"
type: plan
module: tests/ui
version: 1.0
status: completed

> **完成证据 (2026-08-24)**: `scripts\build-and-test.bat` → `100% tests passed, 0 tests failed out of 3`
> (UI_MAINWINDOW 1.74s / UI_SEARCH 1.85s / UI_CLEAR 1.25s) + `ALL TESTS PASSED`, exit=0；
> 全量回归 34/35（NetworkMonitorTest 已知环境抖动白名单项，单独复跑通过）；
> 破坏性演练: 改坏 MainWindowName → exit=42 且自动生成 failure_*.png(68KB)+*.txt(50KB) → 还原全绿。
> 交付报告: `docs/reports/2026-08-24-Report-UITestFramework-v1.0.md`
date: 2026-08-24
updated: 2026-08-24
source_spec: docs/specs/2026-08-24-Spec-install-test-Framework.md
---

# wxWidgets UI Automation Test Framework Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 validproxy GUI（wxWidgets）建立一套可被 agent 自动执行的 UI 测试闭环：`Build → 启动真实 Application → Windows UIA 操作 → Catch2 断言 → CTest 收口 → 失败截图/树 dump/JUnit XML → 自动修复循环（≤5 次）`。

**Architecture:** 现有 `bin/validproxy.exe` 零修改。新增独立 `UITests` 可执行目标（Catch2 3.x + UIA 封装框架），通过 `-c` 参数以沙箱配置启动真实 GUI；UIA 按 Name(窗口文本)/ClassName 定位控件并以 PID 过滤防止多实例串扰；失败时由 Catch2 EventListener 自动截图 + 控件树 dump。

**Tech Stack:** C++17 / MinGW-w64 GCC(w64devkit) / CMake+Ninja / vcpkg classic(catch2:x64-mingw-static) / Windows UI Automation(COM) / Catch2 3.x / CTest / Batch。

---

## 一、Phase 1 勘察结论（Spec §二 已执行）

| # | 勘察项 | 结论 |
|---|--------|------|
| 1 | 目录结构 | src/include/src/ui/tests/test/docs/scripts/bin 标准布局 |
| 2 | CMakeLists.txt | CMake 3.20+Ninja，C++17，GCC14，`enable_testing()`+30 个 add_test |
| 3 | vcpkg.json | **不存在**（classic 模式；triplet x64-mingw-static 直链静态库 + x64-mingw-dynamic 提供 wxWidgets CONFIG 包）；VCPKG_ROOT=D:/vcpkg |
| 4 | wxWidgets | 3.3.3 dynamic（vcpkg CONFIG 包），Debug DLL `*ud_gcc_x64_custom.dll` 已复制到 bin/ 与 tests/ |
| 5 | GCC | w64devkit（WX64DEVKIT_ROOT=E:/w64devkit） |
| 6 | 现有 tests | Google Test（本地源码 GTEST_ROOT），32 用例全绿；输出到 `tests/` |
| 7 | 应用启动方式 | `main()` 支持 `-c/--config`（相对路径基于 exeDir 解析）；默认 `<exeDir>/config.json` |
| 8 | 主窗口 | `wxFrame` 标题 **"validproxy - Proxy Manager"** |
| 9 | 控件结构 | 工具栏中文标签工具按钮（更新/测试/取消/同步/查找/去重/导入/自动任务/监控代理/配置/详情）+ wxSearchCtrl(ID_SEARCH_BOX)；LogPanel 3 个真 wxButton（"清空日志窗口"/"日志统计"/"打开日志"）；Subscription/ProxyList 为 wxDataViewCtrl（自绘，UIA 信息有限） |
| 10 | 已有 CTest | ✅ 已启用 |
| 11 | 已有 Catch2 | ❌ 无 |
| 12 | 已有 UIA 代码 | ❌ 无 |
| 13 | 测试沙箱素材 | `bin/test_config.json`(指向 test/guindb.db 全量 53k 行，偏重)、`test/guiNDB_empty.db`(711 profiles，适合 UI 测试) |

## 二、方案决策（含对 Spec 的偏差声明）

| # | Spec 要求 | 项目现实 | **决策** |
|---|-----------|----------|----------|
| D1 | vcpkg **manifest** 模式管理 catch2 | classic 模式 + 9 处硬编码 boost/gcc14 库路径；manifest 化会改变全部依赖解析（curl/sqlite/boost/wx 全部重走 manifest），违反 Spec §二"不要破坏现有项目" | **保持 classic**，`vcpkg install catch2:x64-mingw-static`。偏差理由记录于此，属低风险增量 |
| D2 | 目标 `Application` | `validproxy` target 已存在且 WIN32 subsystem | **复用**，不新建 |
| D3 | UIA 定位 AutomationId | wxMSW 原生控件 AutomationId 弱；但按钮均有窗口文本（中文名）→ 即 UIA Name 属性 | **定位策略**：Name 优先（工具按钮标签/LogPanel 按钮文本）→ ClassName 兜底（EDIT 类搜索框）→ **全部查找按 PID 过滤**防多实例串扰。Task 6 设发现工具先实测再固化常量 |
| D4 | 示例控件 KeywordEdit/SearchButton 等 | 本应用实际控件不同 | 测试针对**本应用真实控件**：主窗口标题、搜索框文本往返、LogPanel 清空按钮 Invoke |
| D5 | wxDataViewCtrl 列表内容断言 | 自绘控件 UIA 暴露有限 | **一期不做** DataVew 内部断言（列入二期扩展）；一期覆盖真实 Win32 控件 |
| D6 | 截图 .png | MinGW 下 GDI+ Flat API 可用（w64devkit 含头与库） | GDI+ 保存 PNG；链接失败时回退方案 stb_image_write（header-only，Task 5 注明） |
| D7 | 测试数据隔离 | 生产库 `bin/worker/guindb.db` 绝不能碰 | 沙箱 `test/ui-sandbox/`：脚本每次运行前重建 `ui-test.db`（复制自 guiNDB_empty.db）+ 生成 `ui-test-config.json`（绝对路径 DB、关网络监控、关订阅自动更新）；exe **原地启动**（DLL 就位）`bin\validproxy.exe -c <沙箱绝对路径>` |
| D8 | xray.executable 校验弹窗风险（历史 bugfix：无效路径加载时弹窗阻塞） | 沙箱 config 必须指向存在的 .exe | 沙箱 config 的 xray.executable 沿用 `E:/v2rayN-windows-64/bin/xray/xray.exe`（脚本检测存在性，不存在则报错终止） |
| D9 | Catch2 与现有 GTest 共存 | GTest 32 用例是既有资产 | **互不影响**：GTest 不动；UITests 独立 target 仅用 Catch2 |
| D10 | `.bat` 批处理 | 默认终端 PowerShell，但 .bat 可直接调用 | 按 Spec 提供 .bat（cmd 语法），内部调 `cmake --preset` |

## 三、影响分析

- **业务代码修改：0 字节**（唯一约束：不改动 src/ 任何文件）
- **构建系统**：CMakeLists.txt 追加一个 `if(wxWidgets_FOUND AND BUILD_UI_TESTS)` 块（新增 target，不动现有行）；`.gitignore` 追加 `test-results/`、`test/ui-sandbox/`
- **新依赖**：vcpkg `catch2:x64-mingw-static`（独立端口，不触碰已装包）
- **运行前提**：真实交互桌面会话（Spec §十八）；UI 测试期间不得有第二个 validproxy 实例干扰判断（PID 过滤已防御）、尽量无残留 xray.exe/sing-box.exe（避免悬垂纳管弹窗噪音）
- **性能**：每次 UI 测试启动一次 GUI 进程（约 2~5s），3 个测试 ≈ 15~20s，可接受

## 四、自动修复循环执行协议（Spec §十五/十六/十七）

```
Build 失败 → 分析编译错误 → 改码 → 重建（计入重试）
Test 失败 → 读 Catch2/CTest 输出 + test-results/logs/ui-test.log + screenshots/*.png + *-tree.txt
  分类 A=业务bug(改src/) B=框架bug(改tests/ui/framework) C=用例错误(改tests/ui/Test*) D=环境问题(停止改码，输出诊断)
MAX_RETRY = 5；超限输出 AUTO_FIX_FAILED + 最后 Build/Test 输出 + 失败控件 + 截图路径 + 已改文件 + 建议下一步
禁止：为使 PASS 而削弱断言（除非经需求分析证明原预期错误，并在提交信息中说明）
```

---

## 五、文件结构规划

```text
tests/ui/
├── framework/
│   ├── ComPtr.h          极简 COM RAII 智能指针（不依赖 MSVC WRL）
│   ├── Wait.h/.cpp       deadline 轮询等待（禁无限等）
│   ├── UIAutomation.h/.cpp  CoInitialize/IUIAutomation 单例封装
│   ├── UIElement.h/.cpp  元素包装：查找(PID过滤)/Click(Value/Invoke)/Text/DumpTree
│   ├── Application.h/.cpp 进程生命周期 CreateProcess/-c/WM_CLOSE/TerminateProcess
│   ├── Paths.h           仓库根/exe/config/test-results 路径推导
│   ├── Diagnostics.h/.cpp utf8 日志 + 失败截图入口 + 控件树落盘
│   └── Screenshot.h/.cpp GDI BitBlt + GDI+ 存 PNG
├── UIIds.h               逻辑标识注册表（Task 6 由实测 dump 固化）
├── Fixtures.h            AppFixture（每测试独占启停）
├── ArtifactsListener.h   失败自动截图+树 dump 的 Catch2 监听器
├── TestMain.cpp          main：COM init + 默认 reporter(console+junit)
├── TestDiscovery.cpp     [--dumptree] 隐藏用例：打印应用 UIA 树
├── TestMainWindow.cpp    [mainwindow]
├── TestSearch.cpp        [search]
└── TestClear.cpp         [clear]
scripts/
├── prepare-ui-sandbox.bat  重建沙箱(config+db)
├── build.bat             配置+构建
├── test-ui.bat           ctest UI_* 子集
└── build-and-test.bat    全流程 9 步
test-results/{screenshots,logs,results}.gitignore 追加
```

---

## 六、实施任务

### Task 0: 环境前置验证

**Files:** 无新建（只读检查）

- [x] **Step 0.1 验证 MinGW 头文件**

```powershell
Test-Path "E:\w64devkit\x86_64-w64-mingw32\include\UIAutomation.h"
Test-Path "E:\w64devkit\x86_64-w64-mingw32\include\gdiplus.h"
```
Expected: 均 True。任一 False → 停止并在报告中说明（触发 D6 回退评估）。

- [x] **Step 0.2 验证工具链与 xray 路径**

```powershell
gcc --version | Select-Object -First 1      # Expected: gcc.exe (…) 14.x
cmake --version | Select-Object -First 1    # Expected: 3.2x
Test-Path "E:\v2rayN-windows-64\bin\xray\xray.exe"   # Expected: True（D8 弹窗规避前提）
```

- [x] **Step 0.3 记录结果** 到最终报告草稿（临时贴在会话内即可）。

### Task 1: vcpkg 安装 Catch2 + UITests 编译骨架

**Files:**
- Modify: `CMakeLists.txt`（文末追加块，不改任何现有行）
- Create: `tests/ui/TestMain.cpp`

- [x] **Step 1.1 安装 catch2**

```powershell
D:\vcpkg\vcpkg.exe install catch2:x64-mingw-static
Test-Path D:\vcpkg\installed\x64-mingw-static\share\catch2\Catch2Config.cmake   # Expected: True
```

- [x] **Step 1.2 写最小 TestMain.cpp**

```cpp
// tests/ui/TestMain.cpp — Catch2 entry for UI automation tests.
#include <catch2/catch_session.hpp>
#include <windows.h>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    // COM apartment for UIA; balanced at process exit.
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Default reporters: console on stdout + JUnit XML for CI, unless caller overrides.
    bool hasReporter = false;
    std::vector<std::string> args(argv, argv + argc);
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-r" || args[i] == "--reporter") { hasReporter = true; break; }
    }
    std::vector<char*> finalArgs(argv, argv + argc);
    std::string junitOpt = "-r junit::out=test-results/results.xml";
    if (!hasReporter) {
        finalArgs.push_back(const_cast<char*>("-r"));
        finalArgs.push_back(const_cast<char*>("console"));
        finalArgs.push_back(const_cast<char*>(junitOpt.c_str()));
    }

    int rc = Catch::Session().run(static_cast<int>(finalArgs.size()), finalArgs.data());
    ::CoUninitialize();
    return rc;
}
```

- [x] **Step 1.3 CMakeLists.txt 文末追加**

```cmake
# ===== UI Automation Tests (Catch2 3.x + Windows UIA, Spec 2026-08-24) =====
option(BUILD_UI_TESTS "Build wxWidgets UI automation tests (real GUI)" ON)
if(wxWidgets_FOUND AND BUILD_UI_TESTS)
    find_package(Catch2 CONFIG QUIET
        PATHS "${VCPKG_ROOT}/installed/x64-mingw-static/share/catch2"
        NO_DEFAULT_PATH)
    if(NOT Catch2_FOUND)
        message(STATUS "Catch2 not installed (run: vcpkg install catch2:x64-mingw-static) - UI tests skipped")
    else()
        set(UI_FRAMEWORK_SOURCES
            tests/ui/framework/Wait.cpp
            tests/ui/framework/UIAutomation.cpp
            tests/ui/framework/UIElement.cpp
            tests/ui/framework/Application.cpp
            tests/ui/framework/Diagnostics.cpp
            tests/ui/framework/Screenshot.cpp
        )
        set(UI_TEST_SOURCES
            tests/ui/TestMain.cpp
            tests/ui/TestDiscovery.cpp
            tests/ui/TestMainWindow.cpp
            tests/ui/TestSearch.cpp
            tests/ui/TestClear.cpp
        )
        add_executable(UITests ${UI_FRAMEWORK_SOURCES} ${UI_TEST_SOURCES})
        target_include_directories(UITests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_link_libraries(UITests PRIVATE
            Catch2::Catch2
            uiautomationcore oleaut32 uuid
            user32 gdi32 gdiplus shell32)
        set_target_properties(UITests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
            RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
            RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests)

        foreach(_tag mainwindow search clear)
            string(TOUPPER ${_tag} _UP)
            add_test(NAME UI_${_UP} COMMAND UITests [${_tag}]
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        endforeach()
        set_tests_properties(UI_MAINWINDOW UI_SEARCH UI_CLEAR PROPERTIES TIMEOUT 120)
    endif()
endif()
```
注：Task 1 先只创建 `TestMain.cpp`，其余源文件在后续任务补齐——期间将未创建的 `.cpp` 暂从 `UI_FRAMEWORK_SOURCES`/`UI_TEST_SOURCES` 中注释掉，随任务逐个解注（保持每步可编译）。

- [x] **Step 1.4 编译冒烟**

```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8 --target UITests
.\tests\UITests.exe --list-tests
```
Expected: `0 selected` / 空列表，退出码 0。

- [x] **Step 1.5 Commit** `feat(tests): add UITests skeleton (Catch2+vcpkg classic)`

### Task 2: Wait + UIAutomation（COM RAII）— TDD

**Files:** `tests/ui/framework/Wait.h/.cpp`、`UIAutomation.h/.cpp`、`ComPtr.h`、`tests/ui/TestFrameworkUnit.cpp`（纯逻辑单测，同 exe）

- [x] **Step 2.1 失败测试先行**

```cpp
// tests/ui/TestFrameworkUnit.cpp
#include <catch2/catch_test_macros.hpp>
#include "framework/Wait.h"

TEST_CASE("waitFor returns true immediately", "[fw-unit]") {
    int calls = 0;
    REQUIRE(uitest::waitFor(1000, 10, [&] { ++calls; return true; }));
    REQUIRE(calls == 1);
}
TEST_CASE("waitFor times out with bounded polls", "[fw-unit]") {
    int calls = 0;
    REQUIRE_FALSE(uitest::waitFor(120, 40, [&] { ++calls; return false; }));
    REQUIRE(calls >= 3);
    REQUIRE(calls <= 6);
}
```
（`--list-tests` 应出现 fw-unit；此组默认不进 CTest 注册，仅供开发期 `UITests.exe "[fw-unit]"` 手动跑。）

- [x] **Step 2.2 实现 Wait**

```cpp
// tests/ui/framework/Wait.h
#pragma once
#include <functional>
namespace uitest {
// Polls condition every pollIntervalMs until true or timeoutMs elapsed.
// Final evaluation happens once more AFTER the last sleep (no infinite waits).
bool waitFor(int timeoutMs, int pollIntervalMs, const std::function<bool()>& condition);
}
```
```cpp
// tests/ui/framework/Wait.cpp
#include "framework/Wait.h"
#include <chrono>
#include <windows.h>
namespace uitest {
bool waitFor(int timeoutMs, int pollIntervalMs, const std::function<bool()>& condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) return true;
        ::Sleep(static_cast<DWORD>(pollIntervalMs));
    }
    return condition();
}
}
```

- [x] **Step 2.3 ComPtr.h**

```cpp
// tests/ui/framework/ComPtr.h — minimal COM RAII (no MSVC WRL dependency).
#pragma once
namespace uitest {
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T** operator&() { reset(); return &ptr_; }          // for COM out-params
    T* operator->() const { return ptr_; }
    T* get() const { return ptr_; }
    T* release() { T* p = ptr_; ptr_ = nullptr; return p; }
    void reset() { if (ptr_) { ptr_->Release(); ptr_ = nullptr; } }
private:
    T* ptr_ = nullptr;
};
}
```

- [x] **Step 2.4 UIAutomation.h/.cpp**

```cpp
// tests/ui/framework/UIAutomation.h
#pragma once
#include <windows.h>
#include <UIAutomation.h>
#include "framework/ComPtr.h"
namespace uitest {
class Uia {
public:
    static Uia& instance();
    bool init();                                   // idempotent
    IUIAutomation* com() const { return automation_.get(); }
    IUIAutomationElement* desktop() { return desktop_.get(); }
private:
    Uia() = default;
    ComPtr<IUIAutomation> automation_;
    ComPtr<IUIAutomationElement> desktop_;
};
}
```
```cpp
// tests/ui/framework/UIAutomation.cpp
#include "framework/UIAutomation.h"
namespace uitest {
Uia& Uia::instance() { static Uia inst; return inst; }
bool Uia::init() {
    if (automation_.get()) return true;
    HRESULT hr = ::CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_IUIAutomation, reinterpret_cast<void**>(&automation_));
    if (FAILED(hr)) return false;
    hr = automation_->GetRootElement(&desktop_);
    return SUCCEEDED(hr);
}
}
```

- [x] **Step 2.5 构建+手动跑单测** `cmake --build build --target UITests && .\tests\UITests.exe "[fw-unit]"` Expected: 2 passed。
- [x] **Step 2.6 Commit** `feat(tests): UIA COM wrapper + bounded wait primitives`

### Task 3: UIElement（查找/操作/树 dump，PID 过滤）

**Files:** `tests/ui/framework/UIElement.h/.cpp`

- [x] **Step 3.1 UIElement.h**

```cpp
#pragma once
#include <windows.h>
#include <UIAutomation.h>
#include <string>
#include "framework/ComPtr.h"

namespace uitest {
struct Locator {
    PROPERTYID property;      // e.g. UIA_NamePropertyId / UIA_ClassNamePropertyId
    std::wstring value;
};

class UiElement {
public:
    UiElement() = default;
    explicit UiElement(IUIAutomationElement* owned) : element_(owned) {}
    UiElement(UiElement&& o) noexcept : element_(o.element_.release()) {}
    UiElement& operator=(UiElement&& o) noexcept {
        if (this != &o) { element_.reset(); element_.reset(o.element_.release()); }
        return *this;
    }
    UiElement(const UiElement&) = delete;
    UiElement& operator=(const UiElement&) = delete;

    bool valid() const { return element_.get() != nullptr; }

    std::wstring name() const;
    std::wstring className() const;
    std::wstring automationId() const;
    bool isEnabled() const;
    bool isOffscreen() const;
    DWORD processId() const;

    // Subtree search under `scope`, polled until timeoutMs. When pidFilter != 0,
    // candidates whose ProcessId differs are rejected (multi-instance safety).
    static UiElement findBy(const UiElement& scope, const Locator& loc,
                            DWORD pidFilter, int timeoutMs, int pollMs = 150);

    bool click();                          // UIA_InvokePattern; false if unsupported
    bool setText(const wchar_t* text);     // UIA_ValuePattern
    std::wstring getText();                // UIA_ValuePattern current value

    std::wstring dumpTree(int maxDepth) const;   // multi-line UTF-16 text

    IUIAutomationElement* raw() const { return element_.get(); }

private:
    ComPtr<IUIAutomationElement> element_;
};
}
```

- [x] **Step 3.2 UIElement.cpp 关键实现**

```cpp
#include "framework/UIElement.h"
#include "framework/UIAutomation.h"
#include "framework/Wait.h"
#include <sstream>

namespace uitest {
std::wstring UiElement::name() const {
    if (!element_.get()) return {};
    BSTR b = nullptr;
    if (FAILED(element_->get_CurrentName(&b))) return {};
    std::wstring s(b ? b : L"");
    ::SysFreeString(b);
    return s;
}
std::wstring UiElement::className() const {
    if (!element_.get()) return {};
    BSTR b = nullptr;
    if (FAILED(element_->get_CurrentClassName(&b))) return {};
    std::wstring s(b ? b : L"");
    ::SysFreeString(b);
    return s;
}
std::wstring UiElement::automationId() const {
    if (!element_.get()) return {};
    BSTR b = nullptr;
    if (FAILED(element_->get_CurrentAutomationId(&b))) return {};
    std::wstring s(b ? b : L"");
    ::SysFreeString(b);
    return s;
}
bool UiElement::isEnabled() const {
    BOOL v = FALSE;
    return element_.get() && SUCCEEDED(element_->get_CurrentIsEnabled(&v)) && v;
}
bool UiElement::isOffscreen() const {
    BOOL v = TRUE;
    return !element_.get() || FAILED(element_->get_CurrentIsOffscreen(&v)) || v;
}
DWORD UiElement::processId() const {
    DWORD v = 0;
    if (element_.get()) element_->get_CurrentProcessId(&v);
    return v;
}

UiElement UiElement::findBy(const UiElement& scope, const Locator& loc,
                            DWORD pidFilter, int timeoutMs, int pollMs) {
    IUIAutomation* ua = Uia::instance().com();
    if (!ua || !scope.raw()) return {};

    VARIANT v; ::VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = ::SysAllocString(loc.value.c_str());
    ComPtr<IUIAutomationCondition> cond;
    HRESULT hr = ua->CreatePropertyCondition(loc.property, v, &cond);
    ::VariantClear(&v);
    if (FAILED(hr)) return {};

    return waitFor(timeoutMs, pollMs, [&]() -> bool {
        IUIAutomationElement* rawFound = nullptr;
        hr = scope.raw()->FindFirst(TreeScope_Subtree, cond.get(), &rawFound);
        if (FAILED(hr) || !rawFound) return false;
        if (pidFilter != 0) {
            DWORD pid = 0; rawFound->get_CurrentProcessId(&pid);
            if (pid != pidFilter) { rawFound->Release(); return false; }
        }
        return true;   // keep rawFound ownership via out-param trick below
    }) ? [&]() {
        IUIAutomationElement* rawFound = nullptr;
        scope.raw()->FindFirst(TreeScope_Subtree, cond.get(), &rawFound);
        return UiElement(rawFound);
    }() : UiElement();
}

bool UiElement::click() {
    if (!element_.get()) return false;
    VARIANT p; ::VariantInit(&p);
    HRESULT hr = element_->GetCurrentPattern(UIA_InvokePatternId, &p);
    bool ok = false;
    if (SUCCEEDED(hr) && p.vt == VT_UNKNOWN && p.punkVal) {
        IUIAutomationInvokePattern* inv = nullptr;
        if (SUCCEEDED(p.punkVal->QueryInterface(IID_IUIAutomationInvokePattern,
                                                reinterpret_cast<void**>(&inv))) && inv) {
            ok = SUCCEEDED(inv->Invoke());
            inv->Release();
        }
    }
    ::VariantClear(&p);
    return ok;
}
bool UiElement::setText(const wchar_t* text) {
    if (!element_.get()) return false;
    VARIANT p; ::VariantInit(&p);
    HRESULT hr = element_->GetCurrentPattern(UIA_ValuePatternId, &p);
    bool ok = false;
    if (SUCCEEDED(hr) && p.vt == VT_UNKNOWN && p.punkVal) {
        IUIAutomationValuePattern* val = nullptr;
        if (SUCCEEDED(p.punkVal->QueryInterface(IID_IUIAutomationValuePattern,
                                                reinterpret_cast<void**>(&val))) && val) {
            ok = SUCCEEDED(val->SetValue(const_cast<BSTR>(text)));
            val->Release();
        }
    }
    ::VariantClear(&p);
    return ok;
}
std::wstring UiElement::getText() {
    if (!element_.get()) return {};
    VARIANT v; ::VariantInit(&v);
    if (FAILED(element_->GetCurrentPropertyValue(UIA_ValueValuePropertyId, &v))) return {};
    std::wstring s = (v.vt == VT_BSTR && v.bstrVal) ? std::wstring(v.bstrVal) : std::wstring();
    ::VariantClear(&v);
    return s;
}

static void appendLine(std::wostringstream& out, IUIAutomationElement* el, int depth) {
    BSTR n = nullptr, c = nullptr;
    CONTROLTYPE_ID ct = UIA_ControlTypeTypeId;
    DWORD pid = 0; BOOL off = TRUE;
    el->get_CurrentName(&n); el->get_CurrentClassName(&c);
    el->get_CurrentControlType(&ct); el->get_CurrentProcessId(&pid);
    el->get_CurrentIsOffscreen(&off);
    for (int i = 0; i < depth; ++i) out << L"  ";
    out << L"- type=" << ct << L" name=\"" << (n ? n : L"") << L"\" class=\""
        << (c ? c : L"") << L"\" pid=" << pid << (off ? L" [offscreen]" : L"") << L"\n";
    ::SysFreeString(n); ::SysFreeString(c);
}
static void walk(IUIAutomation* ua, IUIAutomationElement* el,
                 std::wostringstream& out, int depth, int maxDepth) {
    appendLine(out, el, depth);
    if (depth >= maxDepth) return;
    IUIAutomationTreeWalker* walker = nullptr;
    if (FAILED(ua->get_ControlViewWalker(&walker)) || !walker) return;
    IUIAutomationElement* child = nullptr;
    if (SUCCEEDED(walker->GetFirstChildElement(el, &child)) && child) {
        while (child) {
            walk(ua, child, out, depth + 1, maxDepth);
            IUIAutomationElement* next = nullptr;
            if (FAILED(walker->GetNextSiblingElement(child, &next)) || !next) { child->Release(); child = nullptr; break; }
            child->Release(); child = next;
        }
    }
    if (walker) walker->Release();
}
std::wstring UiElement::dumpTree(int maxDepth) const {
    std::wostringstream out;
    IUIAutomation* ua = Uia::instance().com();
    if (ua && element_.get()) walk(ua, element_.get(), out, 0, maxDepth);
    return out.str();
}
}
```
注：`findBy` 的实现若嫌"二次 FindFirst"绕，可直接在 lambda 内保存 `rawFound` 到局部 `IUIAutomationElement* result` 再于 waitFor 后取用——两种写法等价，实现者择一，**必须保证无泄漏**（失败分支 Release）。

### Task 4: Application 生命周期（AppProcess + Paths + Fixtures）

**Files:** `tests/ui/framework/Application.h/.cpp`、`Paths.h`、`Fixtures.h`

- [x] **Step 4.1 Paths.h**

```cpp
#pragma once
#include <windows.h>
#include <filesystem>
namespace uitest::paths {
inline std::filesystem::path repoRoot() {
    wchar_t buf[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    // <root>\tests\UITests.exe -> <root>
    return std::filesystem::path(buf).parent_path().parent_path();
}
inline std::filesystem::path appExe()       { return repoRoot() / L"bin" / L"validproxy.exe"; }
inline std::wstring appExeW()               { return appExe().wstring(); }
inline std::filesystem::path sandboxDir()   { return repoRoot() / L"test" / L"ui-sandbox"; }
inline std::filesystem::path sandboxCfg()   { return sandboxDir() / L"ui-test-config.json"; }
inline std::wstring sandboxCfgW()           { return sandboxCfg().wstring(); }
inline std::filesystem::path resultsRoot()  { return repoRoot() / L"test-results"; }
}
```

- [x] **Step 4.2 Application.h**

```cpp
#pragma once
#include <windows.h>
namespace uitest {
class AppProcess {
public:
    AppProcess() = default;
    ~AppProcess();                       // RAII: stop(2000) if still running
    AppProcess(const AppProcess&) = delete;
    AppProcess& operator=(const AppProcess&) = delete;

    // Launches "<exe>" -c "<config>"; returns after main window visible or timeout.
    bool start(const std::wstring& exePath, const std::wstring& configPath,
               int startupTimeoutMs = 10000);
    // WM_CLOSE -> wait gracefulMs -> TerminateProcess -> wait 3s. Always safe twice.
    void stop(int gracefulMs = 5000);

    bool running() const;
    DWORD pid() const { return pid_; }
    HWND mainWindowHwnd() const { return hwnd_; }

private:
    static BOOL CALLBACK findWindowProc(HWND hwnd, LPARAM lParam);
    bool refreshMainWindow();            // EnumWindows by pid + title

    HANDLE process_ = nullptr;
    DWORD pid_ = 0;
    mutable HWND hwnd_ = nullptr;
};
}
```

- [x] **Step 4.3 Application.cpp**

```cpp
#include "framework/Application.h"
#include "framework/Wait.h"
#include "framework/UIIds.h"
#include <string>
namespace uitest {
struct EnumCtx { DWORD pid; HWND found; };
BOOL CALLBACK AppProcess::findWindowProc(HWND hwnd, LPARAM lp) {
    EnumCtx* ctx = reinterpret_cast<EnumCtx*>(lp);
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid) return TRUE;
    if (!::IsWindowVisible(hwnd)) return TRUE;
    wchar_t title[256] = {};
    ::GetWindowTextW(hwnd, title, 256);
    if (std::wstring(title) == ids::kMainWindowTitle) { ctx->found = hwnd; return FALSE; }
    return TRUE;
}
bool AppProcess::refreshMainWindow() {
    EnumCtx ctx{ pid_, nullptr };
    ::EnumWindows(findWindowProc, reinterpret_cast<LPARAM>(&ctx));
    hwnd_ = ctx.found;
    return hwnd_ != nullptr;
}
bool AppProcess::start(const std::wstring& exePath, const std::wstring& cfgPath, int timeoutMs) {
    std::wstring cmd = L"\"" + exePath + L"\" -c \"" + cfgPath + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(exePath.c_str(), buf.data(), nullptr, nullptr, FALSE,
                          0, nullptr, nullptr, &si, &pi))
        return false;
    ::CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    pid_ = pi.dwProcessId;
    ::WaitForInputIdle(pi.hProcess, static_cast<DWORD>(timeoutMs));   // best effort
    return waitFor(timeoutMs, 200, [this] { return refreshMainWindow(); });
}
void AppProcess::stop(int gracefulMs) {
    if (!process_) { pid_ = 0; hwnd_ = nullptr; return; }
    if (hwnd_) ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    if (!waitFor(gracefulMs, 100, [this] { return ::WaitForSingleObject(process_, 0) == WAIT_OBJECT_0; })) {
        ::TerminateProcess(process_, static_cast<UINT>(-1));
        ::WaitForSingleObject(process_, 3000);
    }
    ::CloseHandle(process_);
    process_ = nullptr; pid_ = 0; hwnd_ = nullptr;
}
bool AppProcess::running() const {
    return process_ && ::WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
}
AppProcess::~AppProcess() { stop(2000); }
}
```

- [x] **Step 4.4 UIIds.h 初版**（Task 6 后允许修订）

```cpp
#pragma once
namespace uitest::ids {
inline constexpr wchar_t kMainWindowTitle[] = L"validproxy - Proxy Manager";
inline constexpr wchar_t kBtnLogClear[]     = L"清空日志窗口";
inline constexpr wchar_t kBtnLogStats[]     = L"日志统计";
inline constexpr wchar_t kBtnLogOpen[]      = L"打开日志";
// Filled from Task 6 discovery dump (ClassName of the editable inside wxSearchCtrl):
inline constexpr wchar_t kSearchBoxClass[]  = L"EDIT";   // provisional; verify via dump
}
```

- [x] **Step 4.5 Fixtures.h**

```cpp
#pragma once
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include "framework/Application.h"
#include "framework/UIElement.h"
#include "framework/Paths.h"
#include "framework/UIIds.h"

class AppFixture {
public:
    AppFixture() {
        INFO("start: " << uitest::paths::appExeW().c_str());
        REQUIRE(uitest::Uia::instance().init());
        REQUIRE(std::filesystem::exists(uitest::paths::sandboxCfg()));  // run scripts\prepare-ui-sandbox.bat
        REQUIRE(app_.start(uitest::paths::appExeW(), uitest::paths::sandboxCfgW()));
    }
    ~AppFixture() { app_.stop(); }

    uitest::AppProcess& app() { return app_; }
    uitest::UiElement mainWindow(int timeoutMs = 10000) {
        return uitest::UiElement::findBy(
            uitest::UiElement(uitest::Uia::instance().desktop()),
            { UIA_NamePropertyId, uitest::ids::kMainWindowTitle },
            app_.pid(), timeoutMs);
    }
private:
    uitest::AppProcess app_;
};
```

- [x] **Step 4.6 Commit** `feat(tests): app process lifecycle + element locator registry`

### Task 5: Screenshot + Diagnostics + 失败监听器

**Files:** `Screenshot.h/.cpp`、`Diagnostics.h/.cpp`、`ArtifactsListener.h`

- [x] **Step 5.1 Screenshot**

```cpp
// Screenshot.h
#pragma once
#include <string>
namespace uitest {
// Captures the whole virtual screen to PNG. Returns false on failure.
bool captureScreenToPng(const std::wstring& path);
}
```
```cpp
// Screenshot.cpp
#include "framework/Screenshot.h"
#include <windows.h>
#include <gdiplus.h>
namespace uitest {
bool captureScreenToPng(const std::wstring& path) {
    Gdiplus::GdiplusStartupInput si;
    ULONG_PTR token = 0;
    if (::GdiplusStartup(&token, &si, nullptr) != Gdiplus::Ok) return false;
    struct Guard { ULONG_PTR t; ~Guard() { ::GdiplusShutdown(t); } } guard{ token };

    const int x = ::GetSystemMetrics(SM_XVIRTUALSCREEN), y = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int w = ::GetSystemMetrics(SM_CXVIRTUALSCREEN), h = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HDC screen = ::GetDC(nullptr);
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = ::SelectObject(mem, bmp);
    ::BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY);
    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    ::ReleaseDC(nullptr, screen);

    Gdiplus::Bitmap image(bmp, nullptr);
    CLSID pngCls;
    bool ok = ::CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &pngCls) == S_OK
              && image.Save(path.c_str(), &pngCls, nullptr) == Gdiplus::Ok;
    ::DeleteObject(bmp);
    return ok;
}
}
```

- [x] **Step 5.2 Diagnostics**

```cpp
// Diagnostics.h
#pragma once
#include <string>
namespace uitest::diag {
void logUtf8(const std::string& msg);                       // append test-results/logs/ui-test.log
void onTestFailed(const std::string& testName);             // screenshot + tree dump + log
}
```
```cpp
// Diagnostics.cpp
#include "framework/Diagnostics.h"
#include "framework/Paths.h"
#include "framework/Screenshot.h"
#include "framework/UIAutomation.h"
#include "framework/UIIds.h"
#include "framework/UIElement.h"
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
namespace uitest::diag {
static std::wstring nowStamp() {
    auto t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    wchar_t buf[32];
    wcsftime(buf, 32, L"%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}
void logUtf8(const std::string& msg) {
    namespace fs = std::filesystem;
    fs::create_directories(paths::resultsRoot() / L"logs");
    std::ofstream f(paths::resultsRoot() / L"logs" / L"ui-test.log", std::ios::app);
    f << "[" << std::string(nowStamp().begin(), nowStamp().end()) << "] " << msg << "\n";
}
void onTestFailed(const std::string& testName) {
    namespace fs = std::filesystem;
    fs::create_directories(paths::resultsRoot() / L"screenshots");
    std::wstring safe;
    for (char ch : testName) safe += (ch == ' ' ? L'_' : static_cast<wchar_t>(ch));
    const std::wstring shot = paths::resultsRoot() / L"screenshots" / (safe + L"_failed.png");
    const bool shotOk = captureScreenToPng(shot);
    // Tree dump of any window titled like our app (best-effort, pid unknown here).
    std::wstring tree;
    UiElement desktop(Uia::instance().desktop());
    UiElement win = UiElement::findBy(desktop, { UIA_NamePropertyId, ids::kMainWindowTitle }, 0, 1500);
    if (win.valid()) tree = win.dumpTree(12);
    std::ofstream tf(paths::resultsRoot() / L"logs" / (safe + "-tree.txt"),
                     std::ios::binary);
    if (!tree.empty()) {
        std::string utf8;
        int need = ::WideCharToMultiByte(CP_UTF8, 0, tree.c_str(), -1, nullptr, 0, nullptr, nullptr);
        utf8.resize(static_cast<size_t>(need));
        ::WideCharToMultiByte(CP_UTF8, 0, tree.c_str(), -1, utf8.data(), need, nullptr, nullptr);
        tf << utf8;
    }
    logUtf8("FAILED test=" + testName +
            " screenshot=" + (shotOk ? "saved" : "FAILED") +
            " tree_dump=" + (tree.empty() ? "window-not-found" : "saved"));
}
}
```

- [x] **Step 5.3 ArtifactsListener.h**

```cpp
#pragma once
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include "framework/Diagnostics.h"

class ArtifactsListener : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;
    void testCaseEnded(const Catch::TestCaseStats& stats) override {
        if (stats.totals.assertions.failed == 0) return;
        uitest::diag::onTestFailed(stats.testInfo->name);
    }
};
CATCH_REGISTER_LISTENER(ArtifactsListener)
```
（该头仅被 TestMain.cpp include 一次。）

- [x] **Step 5.4 Commit** `feat(tests): failure artifacts (PNG screenshot + UIA tree dump + log)`

### Task 6: UIA 树发现（--dumptree）并固化 UIIds ⭐ 关键卡点

**Files:** `tests/ui/TestDiscovery.cpp`、修订 `UIIds.h`

- [x] **Step 6.1 写发现用例**

```cpp
// tests/ui/TestDiscovery.cpp
#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include "Fixtures.h"
#include "framework/UIIds.h"

// Hidden tag [.]: excluded from normal runs; run explicitly:
//   .\tests\UITests.exe "[dumptree]"
TEST_CASE("Dump application UIA tree", "[.][dumptree]") {
    AppFixture fx;
    uitest::UiElement win = fx.mainWindow();
    REQUIRE(win.valid());
    std::wcout << L"===== TREE pid=" << fx.app().pid() << L" =====\n"
               << win.dumpTree(14) << std::endl;
    // Also print toolbar-area descendants of interest for locator confirmation:
    uitest::UiElement clearBtn = uitest::UiElement::findBy(
        win, { UIA_NamePropertyId, uitest::ids::kBtnLogClear }, fx.app().pid(), 5000);
    INFO("clear button found=" << clearBtn.valid());
    CHECK(clearBtn.valid());   // informational; refine ids if this fails
}
```

- [x] **Step 6.2 准备沙箱并执行发现**

```powershell
# 先建最小沙箱（正式脚本在 Task 9；此处手工一次性准备）
New-Item -ItemType Directory -Force test\ui-sandbox | Out-Null
Copy-Item test\guiNDB_empty.db test\ui-sandbox\ui-test.db -Force
# 以 bin\test_config.json 为底稿生成 test\ui-sandbox\ui-test-config.json，
# 必须满足：
#   database.path = "<仓库绝对路径>/test/ui-sandbox/ui-test.db"
#   network_monitor.enabled = false
#   subscription.check_auto_update_interval = false
#   dedup.enabled = false ; notification.enabled = false ; log.file_level = WARN
#   xray.executable = "E:/v2rayN-windows-64/bin/xray/xray.exe"（必须存在，防校验弹窗）
.\tests\UITests.exe "[dumptree]"
```
Expected: stdout 输出完整 UIA 树。**记录**：①主窗口 Name 精确值；②搜索框可编辑子元素的 ClassName/AutomationId；③三个 LogPanel 按钮 Name 是否精确等于常量；④工具栏按钮是否以 Name=中文标签暴露（否则记入 UIIds 修订策略：改用 ClassName+序号）。

- [x] **Step 6.3 按实测修订 UIIds.h**（值来自上一步输出；若某常量与实测不符，同步修正引用它的测试文件）。
- [x] **Step 6.4 Commit** `test(ui): discovery pass - pin stable UI identifiers`

### Task 7: TestMainWindow — 第一个真实 UI 测试

**Files:** `tests/ui/TestMainWindow.cpp`

- [x] **Step 7.1 编写**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "Fixtures.h"

TEST_CASE("Application starts and shows main window", "[mainwindow]") {
    AppFixture fx;
    auto win = fx.mainWindow();
    REQUIRE(win.valid());                                        // window appeared <= 10s
    REQUIRE(win.processId() == fx.app().pid());                  // ours, not another instance
    REQUIRE(win.isEnabled());
    REQUIRE_FALSE(win.isOffscreen());
    REQUIRE(fx.app().running());
}
```

- [x] **Step 7.2 运行**

```powershell
.\scripts\prepare-ui-sandbox.bat   # 若 Task 9 未完成则沿用 Task 6 手工沙箱
.\tests\UITests.exe "[mainwindow]"
```
Expected: `All tests passed (1 assertion in 1 test case)`；`test-results/results.xml` 生成。
- [x] **Step 7.3 故意破坏性演练（验证失败链路，随后还原）**：临时把 `kMainWindowTitle` 改成错误字符串 → 重跑 → Expected FAIL 且生成 `test-results/screenshots/Application_starts_and_shows_main_window_failed.png` 与 `-tree.txt` → 还原常量 → 重跑 PASS。
- [x] **Step 7.4 Commit** `test(ui): main window smoke test with pid-scoped lookup`

### Task 8: TestSearch + TestClear

**Files:** `tests/ui/TestSearch.cpp`、`tests/ui/TestClear.cpp`

- [x] **Step 8.1 TestSearch.cpp**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "Fixtures.h"
#include "framework/UIIds.h"

TEST_CASE("Search box accepts and echoes text", "[search]") {
    AppFixture fx;
    auto win = fx.mainWindow();
    REQUIRE(win.valid());

    auto box = uitest::UiElement::findBy(
        win, { UIA_ClassNamePropertyId, uitest::ids::kSearchBoxClass },
        fx.app().pid(), 5000);
    REQUIRE(box.valid());                        // locator pinned in Task 6
    REQUIRE(box.setText(L"HK"));
    REQUIRE(box.getText() == L"HK");

    // Filtering must not crash or hang the app.
    ::Sleep(500);
    REQUIRE(fx.app().running());
}
```
（若 Task 6 发现搜索框 ClassName≠EDIT，改用实测值或 AutomationId 定位——以 dump 输出为准。）

- [x] **Step 8.2 TestClear.cpp**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "Fixtures.h"
#include "framework/UIIds.h"

TEST_CASE("Log panel clear button invokes", "[clear]") {
    AppFixture fx;
    auto win = fx.mainWindow();
    REQUIRE(win.valid());

    auto btn = uitest::UiElement::findBy(
        win, { UIA_NamePropertyId, uitest::ids::kBtnLogClear },
        fx.app().pid(), 5000);
    REQUIRE(btn.valid());
    REQUIRE(btn.isEnabled());
    REQUIRE(btn.click());                        // InvokePattern on real Win32 BUTTON
    ::Sleep(300);
    REQUIRE(fx.app().running());                 // click handled, app alive
}
```

- [x] **Step 8.3 分别运行** `.\tests\UITests.exe "[search]"` / `"​[clear]"` → Expected: 各自 All tests passed。
- [x] **Step 8.4 Commit** `test(ui): search box round-trip + log-clear invoke tests`

### Task 9: 沙箱脚本 + 三件套 bat

**Files:** `scripts/prepare-ui-sandbox.bat`、`scripts/build.bat`、`scripts/test-ui.bat`、`scripts/build-and-test.bat`、`.gitignore`

- [x] **Step 9.1 prepare-ui-sandbox.bat**

```bat
@echo off
setlocal
set "ROOT=%~dp0.."
set "SBX=%ROOT%\test\ui-sandbox"
if not exist "%SBX%" mkdir "%SBX%"

copy /y "%ROOT%\test\guiNDB_empty.db" "%SBX%\ui-test.db" >nul || (echo [FAIL] copy db & exit /b 1)

> "%SBX%\ui-test-config.json" (
echo {
echo   "database": { "path": "%ROOT:\=/%/test/ui-sandbox/ui-test.db" },
echo   "xray": { "executable": "E:/v2rayN-windows-64/bin/xray/xray.exe", "workers": 1, "start_port": 20801, "api_port": 20901 },
echo   "test": { "url": "https://www.google.com/generate_204", "timeout_ms": 5000 },
echo   "log": { "enabled": false, "console_level": "WARN", "file_level": "WARN" },
echo   "subscription": { "priority_mode": "proxy_first", "check_auto_update_interval": false },
echo   "dedup": { "enabled": false, "dedup_after_update": false, "blacklist_threshold": 5 },
echo   "sync": { "source_db": "", "target_db": "" },
echo   "notification": { "enabled": false, "on_update": true, "on_test": true },
echo   "network_monitor": { "enabled": false, "check_urls": [], "check_interval_ms": 60000, "check_timeout_ms": 5000,
echo     "probe_on_disconnect": { "max_probes": 3 } }
echo }
)
if not exist "E:\v2rayN-windows-64\bin\xray\xray.exe" (echo [FAIL] xray path invalid - popup guard D8 & exit /b 1)
if not exist "%ROOT%\bin\validproxy.exe" (echo [FAIL] bin\validproxy.exe missing - build first & exit /b 1)
echo [PASS] sandbox ready: %SBX%
exit /b 0
```
注：`sync.source_db/target_db` 置空仅为避免相对路径歧义，GUI 启动不消费它们；若 ConfigValidator 对空值告警但不阻断则保留，若阻断则填绝对路径指向沙箱 db。

- [x] **Step 9.2 build.bat / test-ui.bat**

```bat
@echo off
rem scripts\build.bat — configure + build app and UITests
setlocal
cd /d "%~dp0.."
cmake --preset default || exit /b 1
cmake --build build --parallel 8 --target validproxy validproxy-cli UITests || exit /b 1
echo [PASS] build
exit /b 0
```
```bat
@echo off
rem scripts\test-ui.bat — run registered UI_* CTest entries
setlocal
cd /d "%~dp0.."
call scripts\prepare-ui-sandbox.bat || exit /b 1
ctest --test-dir build --output-on-failure -R "^UI_"
exit /b %ERRORLEVEL%
```

- [x] **Step 9.3 build-and-test.bat（Spec §十四 9 步全流程）**

```bat
@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0.."
echo ========================================
echo  wxWidgets UI Test
echo ========================================

echo [1/5] Checking toolchain...
where gcc >nul 2>nul || (echo [FAIL] MinGW-w64 gcc not in PATH & exit /b 1)
where cmake >nul 2>nul || (echo [FAIL] cmake not in PATH & exit /b 1)
if not exist "%VCPKG_ROOT%\vcpkg.exe" if not exist "D:\vcpkg\vcpkg.exe" (echo [FAIL] vcpkg not found & exit /b 1)
if not defined VCPKG_ROOT set "VCPKG_ROOT=D:\vcpkg"
echo [PASS] toolchain

echo [2/5] Installing dependencies...
if not exist "%VCPKG_ROOT%\installed\x64-mingw-static\share\catch2" (
  "%VCPKG_ROOT%\vcpkg.exe" install catch2:x64-mingw-static || exit /b 1
)
echo [PASS] Catch2 3.x

echo [3/5] Building...
call scripts\build.bat || exit /b 1
echo [PASS] Application + UITests

echo [4/5] Running UI Tests...
call scripts\prepare-ui-sandbox.bat || exit /b 1
ctest --test-dir build --output-on-failure -R "^UI_"
set "RC=%ERRORLEVEL%"

echo [5/5] Result summary...
if "%RC%"=="0" (
  echo ========================================
  echo  ALL TESTS PASSED
  echo ========================================
) else (
  echo ========================================
  echo  UI TEST FAILED ^(exit %RC%^)
  echo  Screenshots: test-results\screenshots\
  echo  Logs:        test-results\logs\
  echo ========================================
)
exit /b %RC%
```

- [x] **Step 9.4 .gitignore 追加** `test-results/` 与 `test/ui-sandbox/`。
- [x] **Step 9.5 Commit** `ci(scripts): one-command build-and-test pipeline with sandbox reset`

### Task 10: 全量实跑（Spec Phase 9-11）

- [x] **Step 10.1** `scripts\build-and-test.bat` → Expected: `100% tests passed`（UI_MAINWINDOW/UI_SEARCH/UI_CLEAR 3 项）+ 既有 32 项 GTest 不受影响。
- [x] **Step 10.2** 失败则进入 §四 协议循环（≤5 次）：每次迭代记录分类 A/B/C/D、修改点、复跑结果于会话笔记。
- [x] **Step 10.3** 回归确认：`ctest --test-dir build --output-on-failure`（全量）→ 除已知环境抖动项（NetworkMonitorTest 离线场景）外全绿。

### Task 11: 最终交付报告 + 文档登记（Spec Phase 12 / §二十二）

- [x] **Step 11.1** 输出 `docs/reports/2026-08-XX-Report-UITestFramework-v1.0.md`，含 12 项交付清单（新增/修改文件、依赖、CMake/Catch2/CTest 配置、框架说明、用例列表、命令、最终结果、失败遗留）。
- [x] **Step 11.2** 更新 `docs/INDEX.md` §8.2 登记本计划与报告路径；`docs/plans/project-plans-tracker.md` 近期文档引用表追加两行（plan + report），状态 draft → completed。
- [x] **Step 11.3** 本文档 status 改 `completed`，附最终 ctest 摘录证据。

---

## 七、风险与缓解

| 风险 | 概率 | 缓解 |
|------|------|------|
| w64devkit 缺 `UIAutomation.h`/gdiplus | 低 | Task 0 前置硬门禁；缺失则升级方案评审（stb_image_write + 手写 COM 声明） |
| `IID_IUIAutomationInvokePattern` 链接未定义 | 低 | 已预挂 `-luuid`；仍报错则在 cpp 内 `DEFINE_GUID` 局部定义（UIAutomationClient.h 附 uuid 属性可 `__uuidof` 替代） |
| GUI 启动弹窗（DB 打不开/xray 校验）阻塞 | 中 | D7/D8 沙箱双保险；AppFixture 10s 超时后截图+树 dump 直接暴露弹窗元素 |
| 工具栏按钮 Name 非"更新/测试…"（wxToolBar 标签实现差异） | 中 | Task 6 发现步骤兜底：改 ClassName+序号或 tooltip 文本；不影响一期三用例（仅用到 LogPanel 按钮+搜索框） |
| 用户同时开着生产 GUI 干扰 | 低 | PID 过滤已隔离；bat 启动前 `tasklist | findstr validproxy.exe` 提示（不强制杀） |
| Debug 构建启动慢致 60s 超时 | 低 | CTest TIMEOUT=120；guiNDB_empty.db 仅 711 行 |

## 八、验收标准（映射 Spec §二十/§二十二）

1. `scripts\build-and-test.bat` 一键通过，输出含 `[1/5]…[5/5]` 与 `ALL TESTS PASSED`
2. `ctest --test-dir build -R ^UI_` 显示 UI_MAINWINDOW / UI_SEARCH / UI_CLEAR，`100% tests passed`
3. 人为注入失败时可获得：`test-results/screenshots/*_failed.png`、`logs/*-tree.txt`、`logs/ui-test.log`、`results.xml`
4. `src/` 业务代码 diff 为零
5. 既有 GTest 32 项不受影响
