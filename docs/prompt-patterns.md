---
title: "Prompt Patterns Guide"
type: guide
status: draft
updated: 2026-05-22
---

# Prompt Patterns Guide

## 目的

本文档定义与 `validproxy` C++ wxWidgets 项目协作时的最优提示模式，确保各工作类型的输入结构清晰、信息完备、执行高效。

**目标读者**: 开发者（人类或 AI agent）。

---

## 目录

- [1. 通用原则](#1-通用原则)
- [2. 功能开发](#2-功能开发)
- [3. Bug 修复](#3-bug-修复)
- [4. 调整/修改](#4-调整修改)
- [5. 代码审查](#5-代码审查)
- [6. 调研/探索](#6-调研探索)
- [7. 构建/测试](#7-构建测试)
- [8. 性能优化](#8-性能优化)
- [9. 文档撰写](#9-文档撰写)
- [10. 工作流速查表](#10-工作流速查表)

---

## 1. 通用原则

### 1.1 信息完备四要素

每条提示应尽可能包含：

| 要素 | 说明 | 举例 |
|------|------|------|
| **文件路径** | 涉及的文件（已知就给） | `src/ui/MainFrame.cpp` |
| **期望行为** | "做什么"而非"怎么做" | "搜索框最小宽度设为 120px" |
| **不做范围** | 明确不碰的模块 | "不要修改 ProxyListPanel" |
| **参考文档** | 已有计划/设计/报告 | `docs/plans/2026-05-19-xxx.md` |

### 1.2 任务量判断

| 范围 | 预期处理方式 |
|------|-------------|
| 1 个文件、1 处修改 | 直接执行 |
| 2+ 个文件 | Agent 自动创建 todo list，并行执行 |
| 涉及决策 | Agent 先确认再执行 |
| Bug 无复现步骤 | Agent 先问清楚再动手 |

### 1.3 项目具体约束

- **构建系统**: CMake + Ninja（Debug → `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug`）
- **GUI 框架**: wxWidgets 3.2+（wxMSW）
- **平台**: Windows（需关注 wxMSW 原生控件行为）
- **类型规范**: 禁止 `auto` 类型推导（按项目 convention）
- **文档先行**: 功能开发必须先有计划文档（按 `docs/plans/DEV-PROCESS.md`）

---

## 2. 功能开发

### 2.1 标准流程

```
brainstorm → plan（计划文档） → approve → implement → review → verify
```

### 2.2 有计划的执行

**模式 A — 引用已有计划文档：**

```
继续执行 docs/plans/2026-05-19-ui-enhancements-sort-find-link.md
从任务 "[列排序]" 开始
```

Agent 会读取计划文档，创建 todo list，按顺序执行。

**模式 B — 需要创建计划：**

```
/ce-plan "给 ProxyListPanel 添加列排序功能，支持点击表头切换升/降序"
```

或手动指定：

```
创建计划文档：功能 "列排序"
参考：docs/design/ui-design-plan.md（ProxyListPanel 部分）
约束：不引入新依赖，排序状态显示在表头
```

### 2.3 直接执行（简单功能）

```
功能: 在 LogPanel 添加 "清空日志" 按钮
文件: src/ui/LogPanel.cpp, src/ui/LogPanel.h
位置: 工具栏最后一个位置
行为: 点击后清空日志文本框内容
参考: MainFrame 中已有清空按钮的实现模式
不做: 不修改数据模型
```

---

## 3. Bug 修复

### 3.1 标准 Bug 报告格式

```
Bug: wxSearchCtrl 残留残影
复现步骤:
1. 启动应用（窗口默认 1200×800）
2. 水平缩窄窗口至 700px 以下
3. 观察搜索框区域
实际表现: 搜索框缩窄后左侧留黑色残影，dbPath 标签与搜索框重叠
预期表现: 搜索框干净缩小，dbPath 标签随动不重叠
相关文件: src/ui/MainFrame.cpp (onResize 方法)
```

### 3.2 Bug + 截图/日志

```
Bug: 批量测试时崩溃

复现:
1. 菜单 Tools → Batch Test
2. 选择 50 个代理
3. 点击 Start

日志尾 20 行:
[2026-05-22 10:00:00] [ERR] vector subscript out of range at ProxyBatchTester.cpp:142

相关文件: src/core/ProxyBatchTester.cpp
```

### 3.3 GUI 视觉问题

附带截图（base64）可大幅加速诊断：

```
Bug: Clear 按钮黑色背景

截图: [base64]

复现: 鼠标悬停 Clear 按钮时背景变黑方块
期望: 透明背景，只显示 X 图标
```

---

## 4. 调整/修改

适用于微调已有功能的行为或外观，通常涉及 1-2 个文件。

```
调整: MainFrame 搜索框最小宽度
目标文件: src/ui/MainFrame.cpp
位置: onResize() 中 searchWidth 的计算
当前值: 80
目标值: 120
不做: 不修改 dbPath 的逻辑
```

```
调整: ProxyListPanel 默认列宽
目标文件: src/ui/ProxyListPanel.cpp
位置: 构造函数中列宽设置
变更: 
  - 地址列 250 → 300
  - 延迟列 80 → 100
  - 协议列 120 → 100
```

---

## 5. 代码审查

### 5.1 审查已提交的变更

```
审查变更:
  - src/ui/MainFrame.cpp
  - src/ui/MainFrame.h
  - src/ui/Icons.h
关注点: 内存泄漏、wxMSW 兼容性、是否符合现有设计规范
跳过: 测试文件
```

### 5.2 审查特定 PR/branch

```
审查: 当前分支与 main 的差异
关注: 事件处理链是否正确、有无未处理的边缘情况
```

Agent 会自动调用 `ce-code-review` 技能执行 5 路并行审查（correctness、maintainability、security、testing、project-standards）。

---

## 6. 调研/探索

### 6.1 代码结构探索

```
调研: ProxyListPanel 的数据更新流程
关注:
  - 数据从哪些模块流入（XrayManager? SubscriptionPanel?）
  - 更新事件如何分发
  - 界面刷新的触发点
输出: 数据流图（文本描述）和关键文件路径
```

### 6.2 根因分析

```
分析: 批量测试 500+ 代理时 UI 卡顿
线索: CPU 使用率 100%，界面无响应约 30s
怀疑: 主线程被测试循环阻塞
期待: 确认阻塞点 + 建议修复方向
```

Agent 会启动 `explore` agent 并行搜索相关代码模式。

---

## 7. 构建/测试

### 7.1 完整构建

```
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
ctest -V
```

> 全写一行即可，Agent 会顺序执行。

### 7.2 仅构建 + LSP 检查

```
cmake --build build && lsp_diagnostics
```

### 7.3 构建错误处理

贴最后 15-20 行错误：

```
build 错误, 尾 15 行:
C:\\src\\main.cpp(42): error C2664: ...
```

不要贴完整 build log（>500 行）——Agent 只会看尾部。

---

## 8. 性能优化

```
优化: ProxyListPanel 加载 10000+ 条代理时的响应速度
当前数据: 加载耗时约 8s，界面冻结
目标: 加载耗时 < 1s，界面不冻结
已知瓶颈: 猜测是 wxVirtualListBox 的 OnGetItem 中数据库查询
提供: ProxyListPanel.cpp 和 ProxyDataAccessor.cpp
不做: 不改数据库 schema
```

---

## 9. 文档撰写

```
创建 Bug 修复文档
路径: docs/bugfix/2026-05-22-xxx.md
内容:
  - Bug 描述
  - 根因分析
  - 修复方案（列出修改的文件和关键行）
  - 验证结果
参考格式: docs/bugfix/2026-05-22-toolbar-resize-remnants-overlap-fix.md
```

---

## 10. 工作流速查表

| 你想做什么 | 一句话模板 |
|-----------|-----------|
| 执行已有计划 | `继续执行 docs/plans/xxx.md` |
| 创建计划 | `/ce-plan "功能描述"` |
| 改代码（知道文件） | `在 [路径] 把 [A] 改成 [B]` |
| 改代码（不知道文件） | `找到实现 [功能] 的代码，把 [行为] 改为 [期望]` |
| 报告 Bug | `Bug: [简述]\n复现: 1. 2. 3.\n实际: [现象]\n期望: [现象]` |
| 审查代码 | `审查变更: [文件列表]` |
| 调研代码 | `调研: [模块名] 的 [方面]` |
| 构建 | `cmake --build build` |
| 构建+测试 | `cmake --build build && ctest -V` |
| 创建文档 | `创建 [类别] 文档，路径: [路径]` |

### 10.1 反模式（不要这样做）

| 反模式 | 问题 |
|--------|------|
| "帮我看看这个"（无上下文） | Agent 需要先猜你在说什么 |
| "修复它"（无 Bug 描述） | 没有根因分析直接修 = 猜谜 |
| 贴 1000 行 build log | 只有最后 20 行有用 |
| "改一下这个功能"（无范围界定） | Agent 可能改了你不想改的地方 |
| 同时说 3 个无关任务 | 分开说效率更高（Agent 会并行执行） |
