# ProxyProcessMonitor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Add periodic monitoring of standalone proxy processes — config support, UI category, status bar indicator with green/red dot + alive count, and timer-driven adopt + count refresh.

**Architecture:** Follows existing `NetworkMonitor` pattern: a header-only config parser, wxPropertyGrid UI category, wxTimer-driven callbacks in MainFrame, and a custom-painted status bar panel for the live indicator.

**Tech Stack:** C++17, wxWidgets, Boost.JSON, Google Test, sqlite3

---

## File Structure

| File | Action | Responsibility |
|------|--------|---------------|
| `include/ConfigReader.h` | Modify | Add `proxy_process_monitor` struct to `AppConfig` |
| `include/config/sections/ProxyProcessMonitorConfigParser.h` | Create | Header-only parser for `proxy_process_monitor` JSON section |
| `src/ConfigReader.cpp` | Modify | Wire parser into `load()` |
| `src/config/ConfigJsonSerializer.cpp` | Modify | Serialize `proxy_process_monitor` section |
| `src/ui/ConfigDialog.cpp` | Modify | Add "监控代理进程" category + load/save/validate |
| `src/ui/AppController.h` | Modify | Declare `getRunningStandaloneCount()` |
| `src/ui/AppController.cpp` | Modify | Implement `getRunningStandaloneCount()` |
| `src/ui/MainFrame.h` | Modify | Add timer, panel, count members; new method declarations |
| `src/ui/MainFrame.cpp` | Modify | Status bar 5-field, panel paint, timer callback, config change handling |
| `tests/test_config_reader.cpp` | Modify | Add round-trip test for `proxy_process_monitor` fields |
| `tests/test_config_reader_load.cpp` | Modify | Add load test verifying defaults and custom values |

---

### Task 1: Add `proxy_process_monitor` struct to AppConfig

**Files:**
- Modify: `include/ConfigReader.h:68-84` (after `network_monitor` struct)

- [x] **Step 1: Add the struct definition**

In `include/ConfigReader.h`, insert the new struct inside `AppConfig`, after the `network_monitor` struct (after line 68) and before the `proxy` struct (line 70):

```cpp
    // ProxyProcessMonitor configuration
    struct {
        bool enabled{false};
        int checkIntervalMs{30000};
    } proxy_process_monitor;
```

- [x] **Step 2: Verify compilation**

```powershell
cmake --build build --parallel 8 2>&1 | Select-String -Pattern "error|proxy_process_monitor" | Select-Object -First 20
```

Expected: No errors related to this change (struct is added, nothing reads it yet).

- [x] **Step 3: Commit**

```bash
git add include/ConfigReader.h
git commit -m "feat(config): add proxy_process_monitor struct to AppConfig"
```

---

### Task 2: Create ProxyProcessMonitorConfigParser (header-only)

**Files:**
- Create: `include/config/sections/ProxyProcessMonitorConfigParser.h`

- [x] **Step 1: Create the parser header**

```cpp
#ifndef CONFIG_SECTIONS_PROXY_PROCESS_MONITOR_H
#define CONFIG_SECTIONS_PROXY_PROCESS_MONITOR_H

#include <boost/json.hpp>
#include "ConfigReader.h"
#include "Logger.h"

namespace config {

class ProxyProcessMonitorConfigParser {
public:
    void parse(const boost::json::value& root, AppConfig& config);
};

inline void ProxyProcessMonitorConfigParser::parse(const boost::json::value& root, AppConfig& config) {
    if (!root.is_object()) return;
    const boost::json::object& obj = root.as_object();

    if (obj.contains("proxy_process_monitor") && obj.at("proxy_process_monitor").is_object()) {
        const boost::json::object& pm = obj.at("proxy_process_monitor").as_object();

        if (pm.contains("enabled") && pm.at("enabled").is_bool()) {
            config.proxy_process_monitor.enabled = pm.at("enabled").as_bool();
        } else if (pm.contains("enabled")) {
            Logger::write("WARNING: config.proxy_process_monitor.enabled has wrong type (expected bool), using default", LogLevel::WARN);
        }

        if (pm.contains("check_interval_ms") && pm.at("check_interval_ms").is_int64()) {
            config.proxy_process_monitor.checkIntervalMs = static_cast<int>(pm.at("check_interval_ms").as_int64());
            if (config.proxy_process_monitor.checkIntervalMs < 5000) {
                config.proxy_process_monitor.checkIntervalMs = 5000;
            }
            if (config.proxy_process_monitor.checkIntervalMs > 300000) {
                config.proxy_process_monitor.checkIntervalMs = 300000;
            }
        } else if (pm.contains("check_interval_ms")) {
            Logger::write("WARNING: config.proxy_process_monitor.check_interval_ms has wrong type (expected int64), using default", LogLevel::WARN);
        }
    } else if (obj.contains("proxy_process_monitor")) {
        Logger::write("WARNING: config.proxy_process_monitor has wrong type (expected object), using default", LogLevel::WARN);
    }
}

} // namespace config
#endif
```

- [x] **Step 2: Verify compilation**

```powershell
cmake --build build --parallel 8 2>&1 | Select-String -Pattern "error|ProxyProcessMonitorConfigParser" | Select-Object -First 20
```

Expected: No errors (header-only, not yet included).

- [x] **Step 3: Commit**

```bash
git add include/config/sections/ProxyProcessMonitorConfigParser.h
git commit -m "feat(config): add ProxyProcessMonitorConfigParser header-only parser"
```

---

### Task 3: Wire parser into ConfigReader::load() + add serialization

**Files:**
- Modify: `src/ConfigReader.cpp:26-77` (add include + parser call)
- Modify: `src/config/ConfigJsonSerializer.cpp:142-144` (add serialization block)

- [x] **Step 1: Add include and parser call in ConfigReader.cpp**

At the top of `src/ConfigReader.cpp`, add the include after line 27 (`#include "config/sections/ProxyConfigParser.h"`):

```cpp
#include "config/sections/ProxyProcessMonitorConfigParser.h"
```

In the `load()` function, add the parser call after line 77 (`ProxyConfigParser().parse(jv, config, exeDir);`):

```cpp
    ProxyProcessMonitorConfigParser().parse(jv, config);
```

Note: `ProxyProcessMonitorConfigParser::parse` only takes 2 args (root, config) — no `exeDir` needed.

- [x] **Step 2: Add serialization in ConfigJsonSerializer.cpp**

At the end of `serialize()`, before `return root;` (after line 142, the `network_monitor` block), add:

```cpp
    // proxy_process_monitor
    boost::json::object pmObj;
    pmObj["enabled"] = config.proxy_process_monitor.enabled;
    pmObj["check_interval_ms"] = config.proxy_process_monitor.checkIntervalMs;
    root["proxy_process_monitor"] = pmObj;
```

- [x] **Step 3: Verify compilation**

```powershell
cmake --build build --parallel 8 2>&1 | Select-String -Pattern "error" | Select-Object -First 20
```

Expected: No errors.

- [x] **Step 4: Commit**

```bash
git add src/ConfigReader.cpp src/config/ConfigJsonSerializer.cpp
git commit -m "feat(config): wire ProxyProcessMonitorConfigParser + serialize proxy_process_monitor"
```

---

### Task 4: Add config round-trip tests

**Files:**
- Modify: `tests/test_config_reader.cpp:113-163` (add fields to `SaveRoundTrip_FieldCompleteness`)
- Modify: `tests/test_config_reader_load.cpp` (add new test for defaults + custom parse)

- [x] **Step 1: Add fields to round-trip test in test_config_reader.cpp**

In `SaveRoundTrip_FieldCompleteness`, after line 116 (`original.network_monitor.checkTimeoutMs = 8000;`), add:

```cpp
    original.proxy_process_monitor.enabled = true;
    original.proxy_process_monitor.checkIntervalMs = 15000;
```

In the EXPECT assertions block (after line 163), add:

```cpp
    EXPECT_EQ(loaded->proxy_process_monitor.enabled, original.proxy_process_monitor.enabled);
    EXPECT_EQ(loaded->proxy_process_monitor.checkIntervalMs, original.proxy_process_monitor.checkIntervalMs);
```

- [x] **Step 2: Add load test for defaults and custom values in test_config_reader_load.cpp**

Add at the end of the file:

```cpp
TEST_F(ConfigReaderLoadTest, ProxyProcessMonitorDefaults) {
    writeConfig("empty.json", "{}");
    std::optional<AppConfig> result = ConfigReader::load(configPath("empty.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->proxy_process_monitor.enabled);
    EXPECT_EQ(result->proxy_process_monitor.checkIntervalMs, 30000);
}

TEST_F(ConfigReaderLoadTest, ProxyProcessMonitorCustomValues) {
    writeConfig("ppm.json", R"({
        "proxy_process_monitor": {
            "enabled": true,
            "check_interval_ms": 15000
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("ppm.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->proxy_process_monitor.enabled);
    EXPECT_EQ(result->proxy_process_monitor.checkIntervalMs, 15000);
}

TEST_F(ConfigReaderLoadTest, ProxyProcessMonitorClampsLowInterval) {
    writeConfig("ppm_low.json", R"({
        "proxy_process_monitor": {
            "enabled": true,
            "check_interval_ms": 1000
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("ppm_low.json"));
    ASSERT_TRUE(result.has_value());
    // Parser clamps to minimum 5000
    EXPECT_EQ(result->proxy_process_monitor.checkIntervalMs, 5000);
}

TEST_F(ConfigReaderLoadTest, ProxyProcessMonitorClampsHighInterval) {
    writeConfig("ppm_high.json", R"({
        "proxy_process_monitor": {
            "enabled": true,
            "check_interval_ms": 500000
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("ppm_high.json"));
    ASSERT_TRUE(result.has_value());
    // Parser clamps to maximum 300000
    EXPECT_EQ(result->proxy_process_monitor.checkIntervalMs, 300000);
}
```

- [x] **Step 3: Run tests**

```powershell
cmake --build build --parallel 8 2>&1 | Select-String -Pattern "error" | Select-Object -First 10
```

```powershell
.\tests\test_config_reader.exe --gtest_filter="ConfigReaderTest.SaveRoundTrip_FieldCompleteness:ConfigReaderLoadTest.ProxyProcessMonitor*"
```

Expected: All tests pass (round-trip preserves fields, defaults correct, custom values parsed, clamping works).

- [x] **Step 4: Commit**

```bash
git add tests/test_config_reader.cpp tests/test_config_reader_load.cpp
git commit -m "test(config): add proxy_process_monitor round-trip and load tests"
```

---

### Task 5: Add config UI category ("监控代理进程")

**Files:**
- Modify: `src/ui/ConfigDialog.cpp:167-169` (add category + properties)
- Modify: `src/ui/ConfigDialog.cpp:240-252` (add loadConfig lines)
- Modify: `src/ui/ConfigDialog.cpp:348-357` (add saveConfig lines)
- Modify: `src/ui/ConfigDialog.cpp:490-494` (add validation)

- [x] **Step 1: Add UI properties in constructor**

In `ConfigDialog.cpp`, after line 167 (`propGrid_->Append(new wxBoolProperty(L"任务完成通知", "autotask_notify", cfg.auto_task.notify_on_complete));`), and before line 169 (`propGrid_->SetPropertyAttributeAll(...)`), add:

```cpp
    // --- 监控代理进程 配置 ---
    propGrid_->Append(new wxPropertyCategory(L"监控代理进程"));
    propGrid_->Append(new wxBoolProperty(L"启用", "proxy_process_monitor_enabled", cfg.proxy_process_monitor.enabled));
    propGrid_->Append(new wxIntProperty(L"检测间隔(毫秒)", "proxy_process_monitor_check_interval_ms", cfg.proxy_process_monitor.checkIntervalMs));
```

- [x] **Step 2: Add loadConfig lines**

In `ConfigDialog::loadConfig()`, after line 251 (`propGrid_->SetPropertyValue("proxy_singbox_template_config_path", ...)`), add:

```cpp
    // ProxyProcessMonitor fields
    propGrid_->SetPropertyValue("proxy_process_monitor_enabled", cfg.proxy_process_monitor.enabled);
    propGrid_->SetPropertyValue("proxy_process_monitor_check_interval_ms", cfg.proxy_process_monitor.checkIntervalMs);
```

- [x] **Step 3: Add saveConfig lines**

In `ConfigDialog::saveConfig()`, after line 356 (`editedConfig_.proxy.singbox_template_config_path = ...`), add:

```cpp
    // ProxyProcessMonitor fields
    editedConfig_.proxy_process_monitor.enabled = propGrid_->GetPropertyValueAsBool("proxy_process_monitor_enabled");
    editedConfig_.proxy_process_monitor.checkIntervalMs = propGrid_->GetPropertyValueAsInt("proxy_process_monitor_check_interval_ms");
    // Clamp to valid range
    if (editedConfig_.proxy_process_monitor.checkIntervalMs < 5000) editedConfig_.proxy_process_monitor.checkIntervalMs = 5000;
    if (editedConfig_.proxy_process_monitor.checkIntervalMs > 300000) editedConfig_.proxy_process_monitor.checkIntervalMs = 300000;
```

- [x] **Step 4: Add validation in validateConfig()**

In `ConfigDialog::validateConfig()`, before `return true;` (line 494), add:

```cpp
    // ProxyProcessMonitor validation
    if (editedConfig_.proxy_process_monitor.checkIntervalMs < 5000 ||
        editedConfig_.proxy_process_monitor.checkIntervalMs > 300000) {
        wxMessageBox("代理进程监控检测间隔必须在 5000 到 300000 毫秒之间", "验证错误", wxOK | wxICON_ERROR);
        return false;
    }
```

- [x] **Step 5: Verify compilation**

```powershell
cmake --build build --parallel 8 2>&1 | Select-String -Pattern "error" | Select-Object -First 10
```

Expected: No errors.

- [x] **Step 6: Commit**

```bash
git add src/ui/ConfigDialog.cpp
git commit -m "feat(ui): add 监控代理进程 config category in ConfigDialog"
```

---

### Task 6: Add `getRunningStandaloneCount()` to AppController

**Files:**
- Modify: `src/ui/AppController.h:133` (add declaration)
- Modify: `src/ui/AppController.cpp` (add implementation)

- [x] **Step 1: Add declaration in AppController.h**

In `src/ui/AppController.h`, after line 133 (`std::vector<std::string> getRunningStandaloneIds() const;`), add:

```cpp
    // Returns the count of currently running standalone proxy processes.
    int getRunningStandaloneCount() const;
```

- [x] **Step 2: Add implementation in AppController.cpp**

At the end of `src/ui/AppController.cpp` (before the closing `}` of the file or after the last method), add:

```cpp
int AppController::getRunningStandaloneCount() const {
    std::lock_guard<std::mutex> lock(standaloneMutex_);
    int count = 0;
    for (const auto& [id, info] : standaloneProxies_) {
        if (info.running) {
            ++count;
        }
    }
    return count;
}
```

- [x] **Step 3: Verify compilation**

```powershell
cmake --build build --parallel 8 2>&1 | Select-String -Pattern "error" | Select-Object -First 10
```

Expected: No errors.

- [x] **Step 4: Commit**

```bash
git add src/ui/AppController.h src/ui/AppController.cpp
git commit -m "feat(controller): add getRunningStandaloneCount() method"
```

---

### Task 7: MainFrame — status bar 5-field, proxyMon panel, timer

**Files:**
- Modify: `src/ui/MainFrame.h:125-128` (add member variables)
- Modify: `src/ui/MainFrame.h:100-103` (add method declarations)
- Modify: `src/ui/MainFrame.cpp:635-649` (expand status bar to 5 fields)
- Modify: `src/ui/MainFrame.cpp:348-406` (add proxyMon timer + panel in startMonitoring)
- Modify: `src/ui/MainFrame.cpp:742-764` (add repositionProxyMonPanel + onProxyMonTimer)
- Modify: `src/ui/MainFrame.cpp:888-983` (add config change detection in onMenuConfig)
- Modify: `src/ui/MainFrame.cpp:409-415` (cleanup in destructor)

- [x] **Step 1: Add member variables in MainFrame.h**

In `src/ui/MainFrame.h`, after line 127 (`bool netMonConnected_{true};`), add:

```cpp
    wxTimer* proxyMonTimer_{nullptr};
    wxPanel* proxyMonPanel_{nullptr};
    bool proxyMonEnabled_{false};
    int proxyAliveCount_{0};
```

- [x] **Step 2: Add method declarations in MainFrame.h**

In `src/ui/MainFrame.h`, after line 102 (`void repositionNetMonPanel();`), add:

```cpp
    void onProxyMonTimer(wxTimerEvent& event);
    void repositionProxyMonPanel();
    void updateProxyMonStatus(bool enabled, int aliveCount);
    void startProxyMonitor(int intervalMs);
    void stopProxyMonitor();
```

- [x] **Step 3: Expand status bar to 5 fields**

In `src/ui/MainFrame.cpp`, replace `initStatusBar()` (lines 635-650) with:

```cpp
void MainFrame::initStatusBar() {
    statusBar_ = CreateStatusBar(5);
    // field0=status msg, field1=log file, field2=network status,
    // field3=proxy monitor status, field4=database path.
    int widths[] = { 200, 200, 100, 110, -1 };
    statusBar_->SetStatusWidths(5, widths);
    statusBar_->SetStatusText("Ready", 0);
    statusBar_->SetStatusText("", 1);
    statusBar_->SetStatusText("", 2);
    statusBar_->SetStatusText("", 3);
    statusBar_->SetStatusText(wxString(getDbPath()), 4);
}
```

- [x] **Step 4: Add proxyMon timer + panel in startMonitoring()**

In `startMonitoring()`, after the existing `statusBar_->Bind(wxEVT_SIZE, ...)` block (line 403-406), add:

```cpp
    // 4) Proxy process monitor timer + panel (field 3)
    proxyMonPanel_ = new wxPanel(statusBar_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    proxyMonPanel_->SetBackgroundStyle(wxBG_STYLE_PAINT);
    proxyMonPanel_->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxPaintDC dc(proxyMonPanel_);
        wxSize sz = proxyMonPanel_->GetClientSize();
        if (sz.x < 4 || sz.y < 4) return;
        // Background
        wxColour face = wxSystemSettings::GetColour(wxSYS_COLOUR_MENUBAR);
        dc.SetBrush(wxBrush(face));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(0, 0, sz.x, sz.y);
        // Border
        wxColour shadow = wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW);
        wxColour highlight = wxSystemSettings::GetColour(wxSYS_COLOUR_3DHIGHLIGHT);
        dc.SetPen(wxPen(shadow));
        dc.DrawLine(0, 0, sz.x - 1, 0);
        dc.DrawLine(0, 0, 0, sz.y - 1);
        dc.SetPen(wxPen(highlight));
        dc.DrawLine(0, sz.y - 1, sz.x - 1, sz.y - 1);
        dc.DrawLine(sz.x - 1, 0, sz.x - 1, sz.y - 1);
        // Text: alive count
        wxString label = wxString::Format("%d", proxyAliveCount_);
        dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        wxSize textExt = dc.GetTextExtent(label);
        int textH = textExt.y;
        int r = (textH - 2) / 2;
        if (r < 2) r = 2;
        int cx = r + 3;
        int cy = sz.y / 2;
        // Dot color
        wxColour dotColor;
        if (!proxyMonEnabled_) {
            dotColor = wxColour(128, 128, 128);  // Grey: not enabled
        } else {
            dotColor = wxColour(0, 180, 0);      // Green: monitoring
        }
        dc.SetBrush(wxBrush(dotColor));
        dc.SetPen(wxPen(dotColor));
        dc.DrawCircle(cx, cy, r);
        dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT));
        dc.DrawText(label, cx + r + 4, cy - textH / 2);
    });

    // Start proxy monitor timer if enabled in config
    if (controller_ && controller_->getConfig().proxy_process_monitor.enabled) {
        startProxyMonitor(controller_->getConfig().proxy_process_monitor.checkIntervalMs);
    } else {
        updateProxyMonStatus(false, 0);
    }
    repositionProxyMonPanel();
```

Also update the `statusBar_->Bind(wxEVT_SIZE, ...)` block to also reposition the proxyMon panel. Replace the existing bind (lines 403-406) with:

```cpp
    statusBar_->Bind(wxEVT_SIZE, [this](wxSizeEvent& evt) {
        evt.Skip();
        repositionNetMonPanel();
        repositionProxyMonPanel();
    });
```

- [x] **Step 5: Add repositionProxyMonPanel and onProxyMonTimer methods**

In `src/ui/MainFrame.cpp`, after `repositionNetMonPanel()` (after line 750), add:

```cpp
void MainFrame::repositionProxyMonPanel() {
    if (!statusBar_ || !proxyMonPanel_) return;
    wxRect fieldRect;
    statusBar_->GetFieldRect(3, fieldRect);
    proxyMonPanel_->SetSize(fieldRect);
    proxyMonPanel_->Refresh();
}
```

After `onNetMonTimer()` (after line 764), add:

```cpp
void MainFrame::onProxyMonTimer(wxTimerEvent&) {
    if (!controller_) return;
    // Scan and adopt dangling standalone proxies in background
    std::thread([this]() {
        controller_->adoptDanglingStandaloneProxies();
    }).detach();
    // Update alive count in status bar
    int aliveCount = controller_->getRunningStandaloneCount();
    updateProxyMonStatus(true, aliveCount);
}
```

- [x] **Step 6: Add updateProxyMonStatus, startProxyMonitor, stopProxyMonitor methods**

In `src/ui/MainFrame.cpp`, after `repositionProxyMonPanel()`, add:

```cpp
void MainFrame::updateProxyMonStatus(bool enabled, int aliveCount) {
    if (!proxyMonPanel_) return;
    proxyMonEnabled_ = enabled;
    proxyAliveCount_ = aliveCount;
    proxyMonPanel_->Refresh();
}

void MainFrame::startProxyMonitor(int intervalMs) {
    if (proxyMonTimer_) {
        proxyMonTimer_->Stop();
        delete proxyMonTimer_;
    }
    proxyMonTimer_ = new wxTimer(this);
    Bind(wxEVT_TIMER, &MainFrame::onProxyMonTimer, this);
    proxyMonTimer_->Start(intervalMs);
    updateProxyMonStatus(true, 0);
}

void MainFrame::stopProxyMonitor() {
    if (proxyMonTimer_) {
        proxyMonTimer_->Stop();
        delete proxyMonTimer_;
        proxyMonTimer_ = nullptr;
    }
    updateProxyMonStatus(false, 0);
}
```

- [x] **Step 7: Add config change detection in onMenuConfig()**

In `onMenuConfig()`, after the existing `netMonSettingsChanged` block (after line 924, the closing `}` of `if (netMonSettingsChanged)`), add:

```cpp
        // Detect proxy process monitor changes
        bool oldProxyMonEnabled = config_.proxy_process_monitor.enabled;
        int oldProxyMonInterval = config_.proxy_process_monitor.checkIntervalMs;
        bool proxyMonEnabledChanged = (cfg.proxy_process_monitor.enabled != oldProxyMonEnabled);
        bool proxyMonIntervalChanged = (cfg.proxy_process_monitor.checkIntervalMs != oldProxyMonInterval);
        if (proxyMonEnabledChanged || proxyMonIntervalChanged) {
            if (cfg.proxy_process_monitor.enabled) {
                startProxyMonitor(cfg.proxy_process_monitor.checkIntervalMs);
            } else {
                stopProxyMonitor();
            }
        }
```

- [x] **Step 8: Cleanup in destructor**

In `~MainFrame()`, after the `netMonTimer_` cleanup block (after line 415), add:

```cpp
    if (proxyMonTimer_) {
        proxyMonTimer_->Stop();
        delete proxyMonTimer_;
        proxyMonTimer_ = nullptr;
    }
```

- [x] **Step 9: Update statusBar_->SetStatusText calls for field 3→4 shift**

Search all `SetStatusText(..., 3)` calls in MainFrame.cpp and update to field 4 (database path moved from field 3 to field 4):

```powershell
rg "SetStatusText.*,\s*3\)" src/ui/MainFrame.cpp
```

Update each occurrence (e.g., line 950: `statusBar_->SetStatusText(wxString(cfg.database_path), 3);` → field `4`).

- [x] **Step 10: Verify compilation**

```powershell
cmake --build build --parallel 8 2>&1 | Select-String -Pattern "error" | Select-Object -First 20
```

Expected: No errors.

- [x] **Step 11: Run full test suite**

```powershell
ctest -V 2>&1 | Select-String -Pattern "test|PASS|FAIL" | Select-Object -Last 20
```

Expected: All existing tests pass.

- [x] **Step 12: Commit**

```bash
git add src/ui/MainFrame.h src/ui/MainFrame.cpp
git commit -m "feat(ui): add proxy process monitor status bar panel + timer in MainFrame"
```

---

### Task 8: Final verification and project tracker update

- [x] **Step 1: Full build**

```powershell
cmake --build build --parallel 8 2>&1 | Select-String -Pattern "error" | Select-Object -First 20
```

Expected: Build succeeds with zero errors.

- [x] **Step 2: Full test suite**

```powershell
ctest -V 2>&1 | Select-String -Pattern "tests passed|FAIL|PASS" | Select-Object -Last 10
```

Expected: All tests pass (existing + new proxy_process_monitor tests).

- [x] **Step 3: Update project tracker**

In `docs/plans/project-plans-tracker.md`, add entry for ProxyProcessMonitor feature completion.

- [x] **Step 4: Commit**

```bash
git add docs/plans/project-plans-tracker.md
git commit -m "docs: update project tracker with ProxyProcessMonitor completion"
```

---

## Spec Coverage Check

| Spec Section | Task(s) |
|---|---|
| §2.1 AppConfig struct | Task 1 |
| §2.2 config.json format | Task 2 (parser), Task 3 (serializer) |
| §3 Status bar layout 5-field | Task 7 (initStatusBar) |
| §3.3 proxyMonPanel_ paint | Task 7 (startMonitoring paint lambda) |
| §4.1 Timer architecture | Task 7 (proxyMonTimer_) |
| §4.2 Timer callback | Task 7 (onProxyMonTimer) |
| §4.3 Status bar panel | Task 7 (proxyMonPanel_ create + paint) |
| §4.4 Reposition | Task 7 (repositionProxyMonPanel + EVT_SIZE) |
| §4.5 Start/stop control | Task 7 (startProxyMonitor/stopProxyMonitor) |
| §4.6 Config change handling | Task 7 (onMenuConfig detection) |
| §4.7 Startup initialization | Task 7 (startMonitoring conditional) |
| §5.1 Config UI category | Task 5 |
| §5.2 Property mapping | Task 5 (load/save) |
