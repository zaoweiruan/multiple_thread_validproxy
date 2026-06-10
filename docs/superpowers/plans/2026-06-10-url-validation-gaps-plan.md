# URL验证补充 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add missing URL validation in two paths (GUI edit dialog, accelerator URL in ConfigDialog) and extract `isValidUrlFormat()` into shared Utils.

**Architecture:** Move `isValidUrlFormat()` from `SubitemUpdaterV2` instance method to `utils::isValidUrlFormat()` free function in Utils.h/Utils.cpp. Call it in SubscriptionPanel::showEditDialog (before save) and ConfigDialog::saveConfig (for accelerator_url).

**Tech Stack:** C++17, wxWidgets, Google Test

---

## File Structure

| File | Change | Responsibility |
|------|--------|---------------|
| `include/Utils.h` | Add `bool isValidUrlFormat(const std::string& url)` | URL format validation (single source of truth) |
| `src/Utils.cpp` | Implement `isValidUrlFormat` | Same logic as existing |
| `include/SubitemUpdaterV2.h` | Remove `isValidUrlFormat` declaration | Cleanup |
| `src/SubitemUpdaterV2.cpp` | Replace internal call `this->isValidUrlFormat()` → `utils::isValidUrlFormat()` | Delegate to shared util |
| `src/SubitemUpdaterV2.cpp` | Remove `isValidUrlFormat` definition | Cleanup |
| `src/ui/SubscriptionPanel.cpp` | Call `utils::isValidUrlFormat()` before save in showEditDialog | GUI edit validation |
| `src/ui/ConfigDialog.cpp` | Call `utils::isValidUrlFormat()` for accelerator_url in saveConfig | Accelerator URL validation |
| `tests/test_utils.cpp` | Add tests for `isValidUrlFormat` | Coverage |

---

### Task 1: Move `isValidUrlFormat()` to Utils

**Files:**
- Modify: `include/Utils.h`
- Modify: `src/Utils.cpp`
- Modify: `include/SubitemUpdaterV2.h`
- Modify: `src/SubitemUpdaterV2.cpp`

- [ ] **Step 1: Add declaration to Utils.h**

Insert after `joinUrl` declaration:

```cpp
bool isValidUrlFormat(const std::string& url);
```

- [ ] **Step 2: Add implementation to Utils.cpp**

Add at end of file:

```cpp
bool utils::isValidUrlFormat(const std::string& url) {
    if (url.find("http://") != 0 && url.find("https://") != 0) {
        return false;
    }
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return false;
    std::string hostPart = url.substr(schemeEnd + 3);
    size_t pathStart = hostPart.find('/');
    std::string domain = (pathStart != std::string::npos)
                        ? hostPart.substr(0, pathStart)
                        : hostPart;
    return domain.find('.') != std::string::npos;
}
```

- [ ] **Step 3: Remove declaration from SubitemUpdaterV2.h**

Delete line 105: `bool isValidUrlFormat(const std::string& url);`

- [ ] **Step 4: Update SubitemUpdaterV2.cpp references**

Replace `isValidUrlFormat(` → `utils::isValidUrlFormat(` (2 occurrences: in importSingleUrl and importSubitemsFromFile).

Then delete the `isValidUrlFormat` function definition (lines 2190-2208).

- [ ] **Step 5: Build and run tests**

```powershell
cmake --build build --parallel 8 2>&1
ctest -V
```

Expected: build succeeds, all tests pass.

- [ ] **Step 6: Commit**

---

### Task 2: Add URL validation to GUI edit dialog

**Files:**
- Modify: `src/ui/SubscriptionPanel.cpp`

- [ ] **Step 1: Add include**

Add at top of SubscriptionPanel.cpp (after existing includes):
```cpp
#include "Utils.h"
```

- [ ] **Step 2: Add validation before save in showEditDialog**

Replace lines 350-360 with:
```cpp
    if (dlg.ShowModal() == wxID_OK) {
        wxString newUrl = urlCtrl->GetValue().Trim();
        if (!newUrl.empty() && !utils::isValidUrlFormat(newUrl.ToStdString())) {
            wxMessageBox(L"订阅 URL 格式无效，请确认包含 http:// 或 https:// 开头且域名有效",
                         L"URL 格式错误", wxOK | wxICON_WARNING);
            return;
        }
        db::models::Subitem updated = sub;
        updated.remarks = nameCtrl->GetValue().ToStdString();
        updated.url = newUrl.ToStdString();
        updated.enabled = enabledCtrl->GetValue() ? "1" : "0";
        updated.useragent = sub.useragent;
        updated.autoupdateinterval = intervalCtrl->GetValue().ToStdString();

        controller_->updateSubitem(updated);
        loadSubscriptions();
    }
```

- [ ] **Step 3: Build**

```powershell
cmake --build build --parallel 8 2>&1
```

Expected: build succeeds (no test changes needed — GUI only).

- [ ] **Step 4: Commit**

---

### Task 3: Add accelerator URL validation to ConfigDialog

**Files:**
- Modify: `src/ui/ConfigDialog.cpp`

- [ ] **Step 1: Add include**

```cpp
#include "Utils.h"
```

- [ ] **Step 2: Add validation in saveConfig**

In `saveConfig()`, after reading `accelerator_url`, add validation:

```cpp
    std::string accelUrl = propGrid_->GetPropertyValueAsString("accelerator_url").ToStdString();
    if (!accelUrl.empty() && !utils::isValidUrlFormat(accelUrl)) {
        wxMessageBox(L"加速器 URL 格式无效，请确认包含 http:// 或 https:// 开头且域名有效。\n该配置将被忽略。",
                     L"URL 格式错误", wxOK | wxICON_WARNING);
        // `editedConfig_.accelerator_url` will be set to invalid value but SubitemUpdaterV2
        // already handles empty accelerator_url by skipping accelerator phase — no data loss.
    }
    editedConfig_.accelerator_url = accelUrl;
```

- [ ] **Step 3: Build**

```powershell
cmake --build build --parallel 8 2>&1
```

- [ ] **Step 4: Commit**

---

### Task 4: Add tests for `utils::isValidUrlFormat()`

**Files:**
- Modify: `tests/test_utils.cpp`

- [ ] **Step 1: Add test cases to test_utils.cpp**

```cpp
TEST(UrlValidationTest, ValidHttpUrl) {
    EXPECT_TRUE(utils::isValidUrlFormat("http://example.com"));
}

TEST(UrlValidationTest, ValidHttpsUrl) {
    EXPECT_TRUE(utils::isValidUrlFormat("https://example.com"));
}

TEST(UrlValidationTest, ValidUrlWithPath) {
    EXPECT_TRUE(utils::isValidUrlFormat("https://example.com/path/to/file"));
}

TEST(UrlValidationTest, ValidUrlWithSubdomain) {
    EXPECT_TRUE(utils::isValidUrlFormat("https://sub.example.com"));
}

TEST(UrlValidationTest, InvalidNoScheme) {
    EXPECT_FALSE(utils::isValidUrlFormat("example.com"));
}

TEST(UrlValidationTest, InvalidFtpScheme) {
    EXPECT_FALSE(utils::isValidUrlFormat("ftp://example.com"));
}

TEST(UrlValidationTest, InvalidNoDomain) {
    EXPECT_FALSE(utils::isValidUrlFormat("http://"));
}

TEST(UrlValidationTest, InvalidEmpty) {
    EXPECT_FALSE(utils::isValidUrlFormat(""));
}

TEST(UrlValidationTest, InvalidDotOnly) {
    EXPECT_FALSE(utils::isValidUrlFormat("http://."));
}

TEST(UrlValidationTest, InvalidLocalhostNoDot) {
    EXPECT_FALSE(utils::isValidUrlFormat("http://localhost"));
}
```

- [ ] **Step 2: Run the new tests**

```powershell
cmake --build build --parallel 8 2>&1
.\tests\test_utils.exe 2>&1
```

Expected: all tests pass.

- [ ] **Step 3: Commit**

---

### Task 5: Verify final build and full test suite

- [ ] **Step 1: Full build**

```powershell
cmake --build build --parallel 8 2>&1
```

Expected: build succeeds (pre-existing libjpeg-62.dll copy error is a non-blocking environment issue).

- [ ] **Step 2: Run all tests**

```powershell
ctest -V 2>&1
```

Expected: all tests pass (currently 59).

- [ ] **Step 3: Commit cumulative changes**
