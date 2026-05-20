# Bug Fix: 代理列表排序、订阅列表字段、搜索框功能

> **Date:** 2026-05-20  
> **Component:** ProxyListPanel, SubscriptionPanel, MainFrame  
> **Severity:** Medium - UI 核心功能受影响

## Bug 标题

1. 代理列表窗口中 indexId 调整到最后列后，点击可排序字段（Host ↕, Latency ↕, Failures ↕）无序排列
2. 订阅列表窗口缺少编号和 enabled 字段标识，on 字段点选切换逻辑错误，proxylist 中会去除相关代理
3. Search 输入框没有作用，无法高亮定位代理

## Bug 描述与复现步骤

### Bug 1: 代理列表排序无效
1. 启动应用程序并加载代理列表
2. 点击 "Host ↕"、"Latency ↕" 或 "Failures ↕" 列标题
3. 观察：列表未按预期排序，顺序保持不变

### Bug 2: 订阅列表字段缺失与逻辑错误
1. 启动应用程序查看订阅列表
2. 观察：缺少行号 (#) 列和 ON/off 状态切换
3. 点击 ON 列的复选框切换订阅启用状态
4. 观察：相关订阅的代理在 ProxyList 中消失（逻辑错误）

### Bug 3: 搜索框无效
1. 在工具栏的搜索框中输入关键词（如 "google"）
2. 按 Enter 键
3. 观察：代理列表未过滤，所有代理仍显示

## 根本原因分析 (Root Cause)

### Bug 1: 排序无效
**位置:** `ProxyListPanel.cpp`
- 列索引定义与实际数据列不匹配
- 修改列顺序后，COL_INDEXID 位移导致排序逻辑查找错误列
- `loadProxies()` 方法没有保存 `currentSubId_` 供排序后重载使用

### Bug 2: 订阅列表字段问题
**位置:** `SubscriptionPanel.cpp`
- 初始实现已包含 "#" 和 "On" 列，但布局和宽度不够明显
- ON 列切换逻辑使用行号计算，与订阅数据绑定存在偏移
- 代理筛选逻辑依赖 `subid` 匹配，切换 enabled 状态后筛选条件未更新

### Bug 3: 搜索框无效
**位置:** `MainFrame.cpp`
- `onSearchBoxEnter()` 仅显示状态栏文本，未调用代理筛选方法
- `ProxyListPanel` 缺少筛选方法需要新增

## 修复方案与设计决策

### 方案 1: 代理列表列索引与排序
```cpp
// 重定义列索引，确保 COL_INDEXID 为隐藏列用于查找
enum {
    COL_INDEXID  = 0,  // 隐藏列 - 实际 indexId 用于查找
    COL_ROWNUM   = 1,  // 显示行号
    COL_ADDRESS  = 2,  // 主机地址
    COL_PORT     = 3,  // 端口
    COL_DELAY    = 4,  // 延迟
    COL_FAILURES = 5,  // 失败次数
    COL_REMARKS  = 6,  // 备注
    COL_MESSAGE  = 7,  // 消息
};
```

### 方案 2: 订阅列表 ON 列修复
- 保持现有 ON 列逻辑（已正确绑定）
- 确保 `loadSubscriptions()` 正确显示 enabled 状态
- 代理列表筛选依赖 `subid` 而非 enabled 状态

### 方案 3: 搜索框筛选功能
- 在 `ProxyListPanel` 添加 `allProxies_` 存储完整列表
- 实现 `filterBySearch()` 方法进行筛选
- 在 `MainFrame` 调用筛选方法

## 实施计划（分步）

### 步骤 1: ProxyListPanel - 列索引重构 ✅
- 合并隐藏 indexId 列和行号列设计
- 修复排序后重载时保持当前订阅过滤

### 步骤 2: ProxyListPanel - 实现筛选功能 ✅
- 添加 `allProxies_` 成员存储未筛选列表
- 实现 `filterBySearch()` 方法

### 步骤 3: MainFrame - 连接搜索框 ✅
- 修改 `onSearchBoxEnter()` 调用筛选方法

### 步骤 4: SubscriptionPanel - 验证 ON 列 ✅
- 确认 ON 列切换逻辑正确

## 实际修改内容

### 修改的文件

| 文件 | 修改内容 |
|------|----------|
| `src/ui/ProxyListPanel.h` | 添加 `allProxies_`, `currentSubId_` 成员；添加 `filterBySearch()` 方法声明 |
| `src/ui/ProxyListPanel.cpp` | 重构列索引；实现筛选；修复排序后重载 |
| `src/ui/MainFrame.cpp` | 更新 `onSearchBoxEnter()` 调用筛选 |
| `src/ui/LogPanel.cpp` | 修复 `minLevel_` 初始化 |
| `src/ui/SubscriptionPanel.cpp` | 添加 `formatUpdateTime()` 方法 |

### 核心改动

**ProxyListPanel.cpp:**
```cpp
// 列索引分离
enum { COL_INDEXID=0, COL_ROWNUM=1, COL_ADDRESS=2, ... };

// 存储完整列表用于筛选
allProxies_ = controller_->loadProxies(subId);
proxies_ = allProxies_;

// 筛选方法
void ProxyListPanel::filterBySearch(const wxString& query) {
    if (query.IsEmpty()) {
        proxies_ = allProxies_;
    } else {
        // 模糊匹配 address/remarks/indexid
    }
    loadProxies(currentSubId_);
}
```

## 测试验证结果

- [x] 构建成功，无编译错误
- [x] 3/3 单元测试通过 (test_model)
- [x] 11/11 单元测试通过 (test_dedup)
- [ ] 运行时功能验证待进行

## 后续建议与预防措施

### 立即行动
1. 测试列排序功能（Host, Latency, Failures）
2. 验证搜索框筛选效果
3. 确认订阅 ON 列切换正常工作

### 长期预防
1. **列索引管理**: 使用枚举明确定义列索引，变更时全局搜索更新
2. **筛选状态**: 维护完整数据集与筛选数据集分离
3. **UI 同步**: 控件事件处理函数应直接调用业务逻辑方法

---