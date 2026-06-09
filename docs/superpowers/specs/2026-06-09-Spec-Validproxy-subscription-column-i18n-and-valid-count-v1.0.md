---
title: "feat: 订阅列表面板 — 新增有效代理列 + 列名中文化"
type: feat
status: implemented
date: 2026-06-09
---

# 订阅列表面板 — 新增有效代理列 + 列名中文化

## 问题描述

### 1. 缺少有效代理计数

订阅列表中只显示每个订阅的总代理数（"Proxies" 列），但用户无法直观了解每个订阅中有多少代理是经过测试验证可用的（delay > 0）。这导致用户需要逐个测试后才能判断订阅质量。

### 2. 列名为英文

订阅列表的列名使用英文（"Name", "Proxies", "Update"），项目整体面向中文用户，列名应中文化以降低使用门槛。

## 范围边界

- **修改文件:**
  - `include/Profileitem.h` — DAO 新增 `countValidBySubId()` 查询方法
  - `src/ui/AppController.h` — 新增 `countValidProxiesBySubId()` 声明
  - `src/ui/AppController.cpp` — 新增 `countValidProxiesBySubId()` 实现
  - `src/ui/SubscriptionListModel.h` — 枚举调整、新增 valid 数据指针、setData 扩展
  - `src/ui/SubscriptionListModel.cpp` — GetValueByRow/Compare 新增列支持
  - `src/ui/SubscriptionPanel.h` — 新增 `validProxyCounts_` 成员
  - `src/ui/SubscriptionPanel.cpp` — 加载 valid 数据、添加列、列名中文化、排序支持
- **不修改:**
  - SubscriptionPanel 的右键菜单、编辑对话框等 UI 元素
  - Subitem / Profileitem 数据模型本身
  - 其他面板（ProxyListPanel / LogPanel 等）
  - 测试基础设施

## 设计决策

### 决策 1：有效代理判定条件

| 方案 | 优点 | 缺点 |
|------|------|------|
| **选中：`CAST(ProfileExItem.delay AS INTEGER) > 0`** | 与现有测试结果表一致；delay=-1 表示未通过 | 需 JOIN 两张表 |
| `delay > 0 AND delay IS NOT NULL` (纯字符串比较) | 语法简单 | SQLite 字符串比较可能产生误判（如 "99" > "0" 但 "9" < "10"） |
| 在应用层统计 | 可精细控制 | 需要加载所有 ProfileExItem，N+1 问题 |

**结论：** 使用 SQL JOIN + CAST 确保数值比较正确。延迟以字符串存储（"0"=未测试/失败, "-1"=失败, 正数=测试成功的延迟值/10）。

### 决策 2：新列位置

| 方案 | 优点 | 缺点 |
|------|------|------|
| **选中：Name 和 Proxies 之间（col 3）** | 有效代理作为代理质量的直观指标，紧邻代理数，用户可快速对比"有效 vs 总数" | 需调整后续列索引 |
| Proxies 之后（col 4） | 不改变现有索引顺序 | 有效代理与总代理数不连续，对比不便 |
| 替换 Proxies 列 | 节省空间 | 丢失总代理数信息 |

**结论：** 位于 Name 和 Proxies 之间，使用户能直观对比"有效代理数 / 总代理数"。

### 决策 3：列名中文化

| 方案 | 优点 |
|------|------|
| **选中：全部中文化**（名称 / 有效 / 代理数 / 更新） | 与项目中文用户定位一致 |
| 保留英文列名 | 无需改动 |

**结论：** 全部列名改为中文。"有效"列不带排序指示符（本身可排序，但列名短不占空间）。

## 详细变更

### C1: `include/Profileitem.h` — DAO 新增 countValidBySubId()

**位置：** `countBySubId()` 方法之后

```cpp
/// Count valid (delay > 0) proxies per subscription via JOIN with ProfileExItem.
std::unordered_map<std::string, int> countValidBySubId() {
    std::unordered_map<std::string, int> result;
    const char* sql = "SELECT p.SubId, COUNT(DISTINCT p.IndexId) "
                      "FROM ProfileItem p "
                      "INNER JOIN ProfileExItem e ON p.IndexId = e.IndexId "
                      "WHERE CAST(e.delay AS INTEGER) > 0 "
                      "GROUP BY p.SubId;";
    // ... standard sqlite3 prepare/step/finalize pattern
    return result;
}
```

- 使用 `COUNT(DISTINCT p.IndexId)` 避免同 IndexId 多行重复计数
- `CAST(e.delay AS INTEGER)` 确保数值比较正确
- 空结果（无有效代理）的 subId 不包含在返回 map 中，调用方默认为 0

### C2: `src/ui/AppController` — 桥接方法

**AppController.h:**
```cpp
std::unordered_map<std::string, int> countValidProxiesBySubId();
```

**AppController.cpp:**
```cpp
std::unordered_map<std::string, int> AppController::countValidProxiesBySubId() {
    db::models::ProfileitemDAO dao(db_);
    return dao.countValidBySubId();
}
```

### C3: `src/ui/SubscriptionListModel` — 枚举和数据层

**枚举调整：**
```cpp
// 修改前
enum { SUB_COL_ROWNUM=0, SUB_COL_ENABLED=1, SUB_COL_NAME=2,
       SUB_COL_PROXIES=3, SUB_COL_UPDATE=4 };
// 修改后
enum { SUB_COL_ROWNUM=0, SUB_COL_ENABLED=1, SUB_COL_NAME=2,
       SUB_COL_VALID=3, SUB_COL_PROXIES=4, SUB_COL_UPDATE=5 };
```

**setData 扩展：**
```cpp
void setData(std::vector<db::models::Subitem>* subs,
             std::unordered_map<std::string, int>* proxyCounts,
             std::unordered_map<std::string, int>* validProxyCounts = nullptr);
```

**GetValueByRow 新增 SUB_COL_VALID case：**
```cpp
case SUB_COL_VALID: {
    int count = 0;
    if (validProxyCounts_) {
        auto it = validProxyCounts_->find(sub.id);
        if (it != validProxyCounts_->end()) count = it->second;
    }
    variant = wxVariant(wxString::Format("%d", count));
    break;
}
```

**Compare 新增 SUB_COL_VALID case：** 与 SUB_COL_PROXIES 相同的整数比较逻辑。

### C4: `src/ui/SubscriptionPanel` — UI 层

**列定义（构造函数）：**
```cpp
// 修改前
listCtrl_->AppendTextColumn("Name ↕", 2, wxDATAVIEW_CELL_INERT, 200);
listCtrl_->AppendTextColumn("Proxies ↕", 3, wxDATAVIEW_CELL_INERT, 70, wxALIGN_RIGHT);
listCtrl_->AppendTextColumn("Update ↕", 4, wxDATAVIEW_CELL_INERT, 130);

// 修改后
listCtrl_->AppendTextColumn("名称 ↕", 2, wxDATAVIEW_CELL_INERT, 200);
listCtrl_->AppendTextColumn("有效 ↕", 3, wxDATAVIEW_CELL_INERT, 60, wxALIGN_RIGHT);
listCtrl_->AppendTextColumn("代理数 ↕", 4, wxDATAVIEW_CELL_INERT, 70, wxALIGN_RIGHT);
listCtrl_->AppendTextColumn("更新 ↕", 5, wxDATAVIEW_CELL_INERT, 130);
```

**同步加载路径（loadSubscriptions）：**
```cpp
validProxyCounts_ = controller_->countValidProxiesBySubId();
```

**异步加载路径（loadSubscriptions const ref 重载）：**
```cpp
validProxyCounts_ = controller_->countValidProxiesBySubId();
```

**updateSubscriptionList 传参至 model：**
```cpp
model_->setData(&subs_, &proxyCounts_, &validProxyCounts_);
```

**排序 handler 扩展：**
```cpp
// 修改前
if (col == SUB_COL_NAME || col == SUB_COL_PROXIES || col == SUB_COL_UPDATE)
// 修改后
if (col == SUB_COL_NAME || col == SUB_COL_VALID || col == SUB_COL_PROXIES || col == SUB_COL_UPDATE)
```

## 兼容性分析

| 场景 | 修改前行为 | 修改后行为 | 影响 |
|------|------------|------------|------|
| 订阅列表显示 | Name / Proxies / Update 三列 | 名称 / 有效 / 代理数 / 更新 四列 | 新增列，列名中文 |
| 排序 Name | col 2 排序 | col 2 排序 | 不变 |
| 排序 Proxies | col 3 排序 | col 4 排序（列索引变化） | 列索引 3→4，因枚举同步更新无影响 |
| 排序 Update | col 4 排序 | col 5 排序 | 列索引 4→5，同上 |
| 有效代理计数 | 无此数据 | 从 ProfileExItem JOIN 查询 | 新增数据 |
| 异步刷新订阅 | 不加载 valid counts | 在 loadSubscriptions 重载中同步加载 | 快速查询无性能问题 |
| 右键菜单 / 编辑对话框 | 中文 | 中文 | 不变 |

## 测试覆盖

| 测试 | 验证点 |
|------|--------|
| 构建验证（3/3 targets） | 所有文件编译通过，枚举引用一致 |
| 全量测试（7/7 suites） | 无回归，12+14+13+11+2+4+1 = 57 tests all PASSED |

- **构建：** `cmake --build build --parallel 8` — 3 个目标全部通过
- **测试：** 7 个测试套件全部通过（ConfigReaderTest 12/12, LoggerTest 14/14, DedupTest 13/13, ShareLinkTest 11/11, ConfigGeneratorTest 2/2+1 skipped, DeleteSubscriptionTest 4/4, CurlEasyHandleTest PASSED）
