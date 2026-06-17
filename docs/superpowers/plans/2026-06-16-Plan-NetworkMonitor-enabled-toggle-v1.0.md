# NetworkMonitor Enabled Toggle Implementation Plan

> **For agentic workers:** Use subagent-driven-development recommended.

**Goal:** When `network_monitor.enabled=false`, NetworkMonitor reports "connected" to skip all abort logic, and status bar hides the network indicator panel.

**Architecture:** Add `enabled_` flag to NetworkMonitor class. `IsConnected()` returns `true` when disabled. MainFrame checks `IsEnabled()` and hides panel. ConfigDialog binds checkbox to config value.

**Tech Stack:** C++17, wxWidgets 3.2, SQLite (no changes)

---

### Task 1: Add `enabled_` member to NetworkMonitor

**Files:**
- Create: `include/NetworkMonitor.h` (modify)
- Modify: `src/NetworkMonitor.cpp`
- Test: `tests/test_network_monitor.cpp`

- [ ] **Step 1: Add `enabled_` member and constructor parameter**

```cpp
// include/NetworkMonitor.h
private:
    bool enabled_{true};  // Track whether monitoring is active
```

```cpp
// include/NetworkMonitor.h
public:
    NetworkMonitor(bool enabled = true);  // Accept enabled flag
```

- [ ] **Step 2: Implement IsEnabled() method**

```cpp
// include/NetworkMonitor.h
bool IsEnabled() const { return enabled_; }  // Add method declaration
```

```cpp
// src/NetworkMonitor.cpp
bool NetworkMonitor::IsEnabled() const {
    return enabled_;
}
```

- [ ] **Step 3: Modify IsConnected() to check enabled**

```cpp
// src/NetworkMonitor.cpp
bool NetworkMonitor::IsConnected() const {
    if (!enabled_) return true;  // When disabled, always "connected"
    return connected_.load();
}
```

- [ ] **Step 4: Add test for enabled=false returns true**

```cpp
// tests/test_network_monitor.cpp
TEST(NetworkMonitorTest, DisabledReturnsTrue) {
    NetworkMonitor nm(false);
    EXPECT_TRUE(nm.IsConnected());
}
```

- [ ] **Step 5: Build and run tests**

```bash
cmake --build build --parallel 8 && .\tests\test_network_monitor.exe
```

Expected: All 8 tests pass (including new DisabledReturnsTrue)

- [ ] **Step 6: Commit**

```bash
git add include/NetworkMonitor.h src/NetworkMonitor.cpp tests/test_network_monitor.cpp
git commit -m "feat: add enabled flag to NetworkMonitor, return true when disabled"
```

---

### Task 2: Update AppController to pass enabled config

**Files:**
- Modify: `src/ui/AppController.cpp`
- Modify: `include/ConfigReader.h` (ensure default true)

- [ ] **Step 1: Update AppController constructor to pass config**

```cpp
// src/ui/AppController.cpp - constructor
AppController::AppController(const config::AppConfig& cfg) : config_(cfg) {
    // ... existing code ...
    netMon_ = new NetworkMonitor(config_.network_monitor.enabled);
}
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build build --parallel 8
```

- [ ] **Step 3: Commit**

```bash
git add src/ui/AppController.cpp
git commit -m "feat: pass network_monitor.enabled to NetworkMonitor constructor"
```

---

### Task 3: Hide status bar panel when disabled

**Files:**
- Modify: `src/ui/MainFrame.cpp`
- Modify: `include/MainFrame.h`

- [ ] **Step 1: Add netMonEnabled_ member to MainFrame**

```cpp
// include/MainFrame.h
private:
    bool netMonEnabled_{true};
```

- [ ] **Step 2: Store enabled state in constructor**

```cpp
// src/ui/MainFrame.cpp - constructor after controller_ created
if (controller_) {
    netMonEnabled_ = config_.network_monitor.enabled;
}
```

- [ ] **Step 3: Modify onNetMonTimer to hide panel when disabled**

```cpp
// src/ui/MainFrame.cpp:606
void MainFrame::onNetMonTimer(wxTimerEvent&) {
    if (!controller_ || !netMonPanel_) return;
    NetworkMonitor* netMon = controller_->getNetworkMonitor();
    if (!netMon) return;
    
    if (!netMon->IsEnabled()) {
        if (netMonPanel_->IsShown()) {
            netMonPanel_->Show(false);
        }
        return;
    }
    
    if (!netMonPanel_->IsShown()) {
        netMonPanel_->Show(true);
    }
    
    bool connected = netMon->IsConnected();
    if (connected != netMonConnected_) {
        netMonConnected_ = connected;
        netMonPanel_->Refresh();
    }
}
```

- [ ] **Step 4: Build and verify**

```bash
cmake --build build --parallel 8
```

- [ ] **Step 5: Commit**

```bash
git add include/MainFrame.h src/ui/MainFrame.cpp
git commit -m "feat: hide network status panel when network_monitor.enabled=false"
```

---

### Task 4: Add ConfigDialog checkbox for enabled

**Files:**
- Modify: `src/ui/ConfigDialog.cpp`
- Modify: `include/ui/ConfigDialog.h`

- [ ] **Step 1: Add checkbox in ConfigDialog**

```cpp
// include/ui/ConfigDialog.h
wxCheckBox* m_cbNetMonEnabled{nullptr};
```

- [ ] **Step 2: Create checkbox in constructor or createControls**

```cpp
// src/ui/ConfigDialog.cpp - in createControls or constructor
m_cbNetMonEnabled = new wxCheckBox(this, wxID_ANY, "Enable network connectivity monitoring");
m_cbNetMonEnabled->SetValue(config_.network_monitor.enabled);
// Add to appropriate sizer
```

- [ ] **Step 3: Update getConfig() to read checkbox**

```cpp
// src/ui/ConfigDialog.cpp - getConfig()
config.network_monitor.enabled = m_cbNetMonEnabled->GetValue();
```

- [ ] **Step 4: Build and verify**

```bash
cmake --build build --parallel 8
```

- [ ] **Step 5: Commit**

```bash
git add src/ui/ConfigDialog.cpp include/ui/ConfigDialog.h
git commit -m "feat: add network_monitor.enabled checkbox to ConfigDialog"
```

---

### Task 5: Full integration test

- [ ] **Step 1: Run all tests**

```bash
ctest -V --output-on-failure
```

- [ ] **Step 2: Manual smoke test**: Build release, test that checkbox works

- [ ] **Step 3: Commit if all pass**

```bash
git commit -m "test: verify NetworkMonitor enabled toggle integration"
```

---

**Plan complete. Options:**
1. **Subagent-Driven** - dispatch per task with review checkpoints
2. **Inline Execution** - execute all tasks in this session

Choose preferred approach.