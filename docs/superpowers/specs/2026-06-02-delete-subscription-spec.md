---
title: "feat: Implement subscription right-click delete functionality"
type: feat
status: draft
date: 2026-06-02
---

# 实现订阅右键删除功能

## 问题描述

`SubscriptionPanel` 的右键菜单中已有"删除订阅"选项，但功能未实现：
- `onDeleteSubscription()` 方法显示确认对话框后，有 `// TODO: implement DAO delete method` 注释
- `SubitemDAO` 类缺少 `deleteById` 方法
- 订阅删除时应级联删除其下所有代理记录（ProfileItem 表的 Subid 外键关联）

## 范围边界

- **修改:**
  - `include/Subitem.h` — 添加 `SubitemDAO::deleteById()` 方法
  - `include/Profileitem.h` — 添加 `ProfileitemDAO::deleteBySubId()` 方法
  - `src/ui/AppController.h` — 声明 `deleteSubscription()` 方法
  - `src/ui/AppController.cpp` — 实现 `deleteSubscription()` 方法
  - `src/ui/SubscriptionPanel.cpp` — 完成 `onDeleteSubscription()` 实现
- **NOT 修改:**
  - 其他面板逻辑
  - 数据库结构（假设外键关系 Subid → Id 已建立）

## 详细变更

### U1: `include/Subitem.h` — SubitemDAO 删除方法

```cpp
// 在类 SubitemDAO 内添加:
bool deleteById(const std::string& id) {
    std::string sql = "DELETE FROM SubItem WHERE Id = '" + escape(id) + "';";
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Logger::write("Delete subscription error: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

static std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) { if (c == '\'') out += "''"; else out += c; }
    return out;
}
```

### U2: `include/Profileitem.h` — ProfileitemDAO 级联删除方法

```cpp
// 在类 ProfileitemDAO 内添加:
bool deleteBySubId(const std::string& subId) {
    std::string sql = "DELETE FROM ProfileItem WHERE Subid = '" + escape(subId) + "';";
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Logger::write("Delete proxies by subId error: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

static std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) { if (c == '\'') out += "''"; else out += c; }
    return out;
}
```

### U3: `src/ui/AppController.h` — 声明删除方法

```cpp
// 在 Subscription 方法区域添加:
bool deleteSubscription(const std::string& subId);
```

### U4: `src/ui/AppController.cpp` — 实现删除方法

```cpp
bool AppController::deleteSubscription(const std::string& subId) {
    // 先删除关联的代理
    db::models::ProfileitemDAO proxyDao(db_);
    proxyDao.deleteBySubId(subId);
    
    // 再删除订阅本身
    db::models::SubitemDAO subDao(db_);
    return subDao.deleteById(subId);
}
```

### U5: `src/ui/SubscriptionPanel.cpp` — 完成删除 handler

```cpp
void SubscriptionPanel::onDeleteSubscription(wxCommandEvent&) {
    std::string subId = getSelectedSubId();
    if (subId.empty()) return;
    std::string remarks;
    for (const auto& sub : subs_) {
        if (sub.id == subId) { remarks = sub.remarks; break; }
    }
    if (confirmDelete(subId, remarks)) {
        if (controller_) {
            controller_->deleteSubscription(subId);
        }
        loadSubscriptions();
    }
}
```

## 验证步骤

1. 编译通过: `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug && cmake --build build`
2. 运行 GUI: `./build/validproxy.exe -ui`
3. 添加测试订阅，确认显示在列表中
4. 右键点击订阅 → 选择"删除订阅" → 确认对话框 → 确实删除
5. 验证数据库中订阅和代理已被删除
6. 运行单元测试: `ctest -V`

## 风险

- 级联删除可能误删大量数据 — 确认对话框已有
- SQL 注入风险 — 使用 escape 函数处理单引号
- 事务一致性 — 若代理删除成功但订阅删除失败，可能留 orphaned proxies。考虑后续添加事务支持。