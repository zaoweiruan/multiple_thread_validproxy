# Spec: StandaloneMonitorDialog 监控列扩展 — Host 列 (v1.1)

| 项目 | 内容 |
| :--- | :--- |
| 日期 | 2026-08-21 |
| 模块 | `StandaloneMonitorDialog`、`AppController::getWatchedStandaloneMonitors` |
| 前置版本 | `docs/specs/2026-08-21-Spec-StandaloneMonitor-Dialog-v1.0.md` |
| 关联修复 | `docs/bugfix/2026-08-21-Bugfix-StandaloneMonitor-DataColumns-v1.0.md` |

## 1. 需求

监控对话框在 **IndexId 列之后**新增 **Host 列**，显示该代理条目的服务器地址
（`ProfileItem.Address`），便于用户直接辨识被监控进程对应哪台代理服务器，
无需复制 IndexId 到主列表反查。

## 2. 数据流设计

```text
ProfileItem.Address (权威源, DB)
        │  ProfileitemDAO::getByIndexId()   ← 参数化查询 (src/ProfileitemDAO.cpp:77)
        ▼
AppController::getWatchedStandaloneMonitors()
        │  Pass2 装配循环内逐行填充 row.host
        ▼
StandaloneMonitorRow.host  (AppController.h)
        │
        ▼
StandaloneMonitorDialog::refreshRows() → COL_HOST 列渲染 (空值显示 "-")
```

### 设计决策

| 决策点 | 选择 | 理由 |
| :--- | :--- | :--- |
| 查询方式 | 复用 `getByIndexId()` 逐行参数化查询 | watched 条目数极小（≤个位数）；避免 IN 列表 SQL 拼接转义风险；DAO 已有实现零新代码 |
| host 缺失语义 | DB 无此 IndexId / address 为空 → 显示 "-" | 与 socksPort/pid 列的缺失语义一致 |
| 不存历史表 | 端口/host 均不入 `proxy_runtime_history` | 运行时状态与生命周期审计分离；Address 权威源在 ProfileItem，副本会漂移 |

## 3. 变更清单

| 文件 | 变更 |
| :--- | :--- |
| `include/AppController.h` | `StandaloneMonitorRow` 新增 `std::string host;` |
| `src/ui/AppController.cpp` | `getWatchedStandaloneMonitors()` Pass2 装配时经 `ProfileitemDAO::getByIndexId()` 填充 host |
| `src/ui/StandaloneMonitorDialog.cpp` | `ColumnId` 插入 `COL_HOST=1`；`InsertColumn`；`refreshRows()` 渲染；窗口宽 720→800 |

## 4. 边界条件

- IndexId 在 ProfileItem 中已被删除（如订阅刷新后节点消失）→ host="-"，其余列正常
- address 含宽字符/UTF-8 → wxString 构造按 UTF-8 处理（项目既有惯例）
- 刷新周期 2s 内多次查询：本地 SQLite 参数化单行查询为微秒级，无性能影响

## 5. 验证

- [x] 构建：`cmake --build build --parallel 8` 0 error
- [x] 回归：ctest 通过（NetworkMonitorTest 环境抖动除外，已知偶发）
- [ ] GUI 回归：Ctrl+M 打开，Host 列位于索引ID右侧并显示代理地址
