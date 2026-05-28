# 更新订阅不可再入与取消功能设计

- **日期:** 2026-05-28
- **作者:** Kilo (AI 辅助)
- **状态:** 已批准

## 1. 问题描述

当前问题：
1. **不可再入不完整** — AppController 的单 `isRunning_` 标志阻止了并发操作，但 SubitemUpdaterV2 内部没有任何取消机制
2. **取消按钮只对测试有效** — 现有的 `ID_TOOL_CANCEL_TEST` 按钮仅设置 `cancelRequested_`，但 SubitemUpdaterV2 从不检查该标志
3. **异常安全缺失** — 多数 `doXxx` 方法没有 RAII 范围守卫，异常会导致 `isRunning_` 永久置 true
4. **按钮语义不匹配** — 更新进行时，取消按钮无反应，用户体验差

## 2. 设计目标

- 更新订阅期间不可再入（阻止新的更新或测试启动）
- 提供「停止更新」功能，复用现有取消按钮
- 安全的协作式取消（非线程终止）
- 异常安全：RAII 守卫保护 `isRunning_` 标志
- 按钮文字动态切换，反映当前可取消的操作

## 3. 架构改动

### 3.1 SubitemUpdaterV2 — 添加取消支持

**文件:** `include/SubitemUpdaterV2.h` / `src/SubitemUpdaterV2.cpp`

新增成员：
```cpp
std::atomic<bool>* externalCancel_{nullptr};
```

新增方法：
```cpp
bool isCancelled() const;
```

构造函数扩展：
```cpp
SubitemUpdaterV2(sqlite3* db, const std::string& xrayPath,
    const config::AppConfig& config, XrayManager* xrayMgr,
    ProxyFinder* proxyFinder,
    std::atomic<bool>* externalCancel = nullptr);  // 新增
```

#### 取消检查点

| 检查点位置 | 文件/方法 | 取消行为 |
|-----------|----------|---------|
| `run()` — 每个订阅循环开始前 | SubitemUpdaterV2.cpp | 跳出循环，跳过剩余订阅 |
| `runSingle()` — 各阶段前 | SubitemUpdaterV2.cpp | 提前返回、跳过后续阶段 |
| `fetchUrl()` — CURL 调用前后 | SubitemUpdaterV2.cpp | 跳过 fetch，标记为已取消 |
| `fetchUrlViaProxy()` — CURL 调用前后 | SubitemUpdaterV2.cpp | 跳过 fetch，标记为已取消 |
| `updateProfileItems()` — DB 事务前 | SubitemUpdaterV2.cpp | 跳过 DB 更新 |
| `getProxyPorts()` — Xray 启动前 | SubitemUpdaterV2.cpp | 跳过代理隧道建立 |

### 3.2 AppController — 连接取消 + RAII 守卫

**文件:** `src/ui/AppController.h` / `src/ui/AppController.cpp`

#### 改动清单

| 改动项 | 说明 |
|--------|------|
| 传递 `&cancelRequested_` 给 SubitemUpdaterV2 | 在 `doUpdateSubscription` 和 `doUpdateAllSubscriptions` 中传入 |
| 为所有 `doXxx` 方法添加 ResetGuard | `doUpdateSubscription`, `doUpdateAllSubscriptions`, `doTestSubscription`, `doTestSingleProxy`, `doTestAllProxies`, `doSyncDatabases` |
| 重命名 `cancelTest()` → `cancelOperation()` | 更通用的语义名称 |

#### ResetGuard 实现

已在 `AppController.h` 中定义，用于所有 `doXxx` 方法：
```cpp
struct ResetGuard {
    std::atomic<bool>& flag;
    ~ResetGuard() { flag = false; }
};
```

### 3.3 MainFrame — 动态取消按钮

**文件:** `src/ui/MainFrame.h` / `src/ui/MainFrame.cpp`

#### 改动清单

| 改动项 | 说明 |
|--------|------|
| 按钮 ID 重命名 | `ID_TOOL_CANCEL_TEST` → `ID_TOOL_CANCEL` |
| 更新启动时 | 设置按钮文字为 `停止更新`，启用按钮 |
| 测试启动时 | 设置按钮文字为 `取消测试`，启用按钮 |
| 操作完成时 | 禁用按钮，重置文字 |
| 点击处理 | 调用 `controller_->cancelOperation()` |

#### 按钮状态矩阵

| 系统状态 | 按钮启用 | 按钮文字 |
|---------|---------|---------|
| 空闲 | 禁用 | `✕ 取消` |
| 测试运行中 | 启用 | `✕ 取消测试` |
| 更新运行中 | 启用 | `✕ 停止更新` |
| 查找运行中 | 启用 | `✕ 取消查找` |
| 同步运行中 | 启用 | `✕ 取消同步` |
| 操作完成/已取消 | 禁用 | `✕ 取消` |

## 4. 数据流

### 更新启动 → 取消流程

```
用户点击"更新全部"
  → MainFrame::onMenuUpdateAll()
    → 设置按钮文字为"停止更新"，启用按钮
    → controller_->updateAllSubscriptionsAsync(this)
      → ResetGuard guard(isRunning_)
      → workerThread_ = thread(&AppController::doUpdateAllSubscriptions, ...)
        → SubitemUpdaterV2 updater(db_, ..., &cancelRequested_)
        → updater.run()
          → for each subscription:
            → if (isCancelled()) break;       ← 取消检查点
            → fetchUrl(...)
            → if (isCancelled()) break;       ← 取消检查点
            → parseSubscription(...)
            → if (isCancelled()) break;       ← 取消检查点
            → updateProfileItems(...)
            → if (isCancelled()) break;       ← 取消检查点
          → wxQueueEvent(StatusUpdateEvent)
      → isRunning_ 自动重置 (ResetGuard)

用户点击"停止更新"
  → MainFrame::onToolCancel()
    → 设置按钮文字为"正在取消…"，禁用按钮
    → controller_->cancelOperation()
      → cancelRequested_ = true
    → SubitemUpdaterV2::isCancelled() 返回 true
    → 当前检查点中断操作
    → 线程退出 → ResetGuard → isRunning_ = false
    → StatusUpdateEvent → 禁用按钮，重置文字
```

## 5. 边界情况

| 情况 | 行为 |
|------|------|
| 取消时正在 DB 事务中 | 不等中断事务，提交/回滚完成后在下一检查点停止 |
| 取消时正在 curl 下载 | 不等中断下载，下载完成后在下一检查点停止 |
| 取消后立即开始新操作 | `workerThread_.join()` 等待旧线程退出 → ResetGuard 已重置 isRunning_ → 新操作可开始 |
| 连续快速点击取消 | 幂等：`cancelRequested_ = true` 多次设置无副作用 |
| 异常抛出 | ResetGuard 确保 `isRunning_` 在任何退出路径都被重置 |
| 应用关闭时正在更新 | 析构函数设置 `cancelRequested_ = true` + `join()` 等待退出 |

## 6. 未涵盖的范围

- 不添加进度事件（StatusUpdateEvent 已完成通知已足够）
- 不回滚已完成的更新（取消时已完成的内容保留）
- 不修改 CLI 行为（CLI 操作不在 UI 上下文中，无取消按钮）
