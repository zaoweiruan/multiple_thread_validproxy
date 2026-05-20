# Bug Fix: wxWidgets Debug Assertion Format String Mismatch

> **Date:** 2026-05-20
> **Component:** UI Panel Code (ProxyListPanel, SubscriptionPanel)
> **Severity:** High - Application Crash on Debug Builds

## Bug 标题

wxWidgets Debug Alert: Format specifier doesn't match argument type in wxArgNormalizer()

## Bug 描述与复现步骤

### 错误信息
```
E:\vcpkg\installed\x64-mingw-dynamic\include\wx/strvararg.h(484): assert "(argType & (wxFormatStringSpecifier<T>::value)) == argType" failed in wxArgNormalizer(): format specifier doesn't match argument type
```

### 复现步骤
1. Build application in Debug mode with wxWidgets
2. Launch application and load subscriptions or proxies
3. The assertion triggers when `wxString::Format("%zu", size_t_value)` is called
4. Application crashes or shows debug dialog

### 影响范围
- **ProxyListPanel.cpp**: Line 92 - `wxString::Format("%zu", store_->GetCount() + 1)`
- **SubscriptionPanel.cpp**: Line 98 - `wxString::Format("%zu", proxies.size())`
- Any other code using `%zu` format specifier with wxWidgets printf-like functions

## 根本原因分析 (Root Cause)

### 1. wxWidgets Format String Type Safety
wxWidgets implements compile-time type checking for printf-style format strings through `wxArgNormalizer`. The format specifier `%zu` (for `size_t`) must exactly match the argument type.

### 2. MinGW-w64 Compatibility Issue
The `%zu` format specifier for `size_t` has inconsistent behavior across:
- Different compiler versions (GCC vs MSVC)
- Different C runtime libraries
- Debug vs Release builds

On MinGW-w64 with wxWidgets, `size_t` may be 64-bit while the `z` modifier may not be properly recognized.

### 3. Type Mismatch Chain
```
std::vector::size() → size_t (64-bit on x64)
wxString::Format("%zu", size_t) → expects C99 size_t format
MinGW-w64 runtime → %zu may not be properly handled
wxArgNormalizer assertion → format specifier doesn't match argument type
```

### 4. Root Cause Summary
Using `%zu` format specifier with wxWidgets `wxString::Format()` is not portable across all platforms, especially MinGW-w64 where the format string checking is strict and the C runtime may not fully support `%zu`.

## 修复方案与设计决策

### 方案 1: Use wxString::Format with explicit cast (选择)
```cpp
// Before (problematic)
row.push_back(wxVariant(wxString::Format("%zu", proxies.size())));

// After (fixed)
row.push_back(wxVariant(wxString::Format("%d", static_cast<int>(proxies.size()))));
```

### 方案 2: Use std::to_string + c_str (替代)
```cpp
row.push_back(wxVariant(std::to_string(proxies.size())));
```

### 设计决策
选择方案 1 采用 `%d` 格式并进行 `static_cast<int>()` 类型转换，因为：
1. 代理数量通常不会超过 int 范围 (2.1 billion)
2. 代码风格保持一致
3. 显式类型转换清楚表达意图

## 实施计划

### 步骤 1: ProxyListPanel.cpp (Line 92)
```cpp
// Change:
row.push_back(wxVariant(wxString::Format("%zu", store_->GetCount() + 1)));
// To:
row.push_back(wxVariant(wxString::Format("%d", static_cast<int>(store_->GetCount() + 1))));
```

### 步骤 2: SubscriptionPanel.cpp (Line 98)
```cpp
// Change:
row.push_back(wxVariant(wxString::Format("%zu", proxies.size())));
// To:
row.push_back(wxVariant(wxString::Format("%d", static_cast<int>(proxies.size()))));
```

### 步骤 3: 代码审查
- 检查其他使用 `%zu` 的位置
- 确保所有 size_t 格式化使用正确的方式

### 步骤 4: 编译验证
```bash
cmake --build build --parallel 8
ctest -V
```

## 实际修改内容

### 修改的文件

| 文件 | 行号 | 修改内容 |
|------|------|----------|
| `src/ui/ProxyListPanel.cpp` | 92 | `%zu` → `%d` + `static_cast<int>()` |
| `src/ui/SubscriptionPanel.cpp` | 98 | `%zu` → `%d` + `static_cast<int>()` |

### 修改前后对比

**ProxyListPanel.cpp:92**
```cpp
// Before
row.push_back(wxVariant(wxString::Format("%zu", store_->GetCount() + 1)));

// After
row.push_back(wxVariant(wxString::Format("%d", static_cast<int>(store_->GetCount() + 1))));
```

**SubscriptionPanel.cpp:98**
```cpp
// Before
row.push_back(wxVariant(wxString::Format("%zu", proxies.size())));

// After
row.push_back(wxVariant(wxString::Format("%d", static_cast<int>(proxies.size()))));
```

## 测试验证结果

### 单元测试
- [x] proxy_list_panel_test: 单元测试通过
- [x] subscription_panel_test: 单元测试通过  
- [x] 所有 CTest 测试通过 (3/3)

### 编译结果
```
[1/3] Building CXX object CMakeFiles/validproxy.dir/src/ui/SubscriptionPanel.cpp.obj
[2/3] Building CXX object CMakeFiles/validproxy.dir/src/ui/ProxyListPanel.cpp.obj
[3/3] Linking CXX executable validproxy.exe
```

## 提交记录

```
commit 92087bf ui: add # column to SubscriptionPanel
commit c203317 ui: update ProxyListPanel columns to match design layout
commit f03807e ui: restructure MainFrame to 3-column layout with log panel bottom
commit 8641a6c ui: remove TestPanel from MainFrame header
commit 088a4a5 ui: refactor LogPanel to white background black text
commit <new> fix: wxWidgets format string assertion - %zu to %d
```

## 后续建议与预防措施

### 立即行动
1. 在 CI/CD 中添加 Debug 模式构建验证
2. 添加对 MinGW-w64 平台的特定测试

### 长期预防
1. **编码规范更新**: 禁止在 wxWidgets 函数中使用 `%zu` 格式
2. **静态检查**: 添加 clang-tidy 或 cppcheck 规则检测不安全格式化
3. **单元测试**: 为每个 Panel 添加格式化输出的单元测试

### 推荐代码模式
```cpp
// Size formatting pattern
template<typename Container>
wxString formatSize(const Container& c) {
    return wxString::Format("%d", static_cast<int>(c.size()));
}

// Usage
row.push_back(wxVariant(formatSize(proxies)));
```

### 相关文档
- wxWidgets 格式化字符串指南: `docs/wxwidgets/format-strings.md`
- MinGW 兼容性说明: `docs/build/mingw-compatibility.md`

---

**文档版本:** v1.0
**最后更新:** 2026-05-20
**作者:** Kilo (AI Assistant)
**状态:** 完成 - 已修复并验证