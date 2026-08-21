# Bugfix: 监控代理启动时 message 测试时间为空的补位策略

| 项目 | 内容 |
| :--- | :--- |
| 日期 | 2026-08-21 |
| 模块 | `ProfileExItemDAO::formatStartupMessage`（src/ProfileExItemDAO.cpp:100） |
| 关联 | `docs/specs/2026-08-21-Spec-StandaloneMonitor-Dialog-v1.1.md`（监控列扩展） |

## 1. 需求

监控代理（standalone 启动/纳管）写入 `ProfileExItem.message` 时，格式为
`<测试时间>+<启动时间>`。当测试时间为空（从未测试过 / 旧数据非时间戳），
原实现留空测试侧（`+<启动时间>`），现要求**用启动时间替代**，即
`<启动时间>+<启动时间>`。

## 2. 原行为与新行为

| 输入 existingMessage | 原输出 | 新输出 |
| :--- | :--- | :--- |
| `"OK"`（无 +，非时间戳） | `+<NS>` | `<NS>+<NS>` |
| `""` | `+<NS>` | `<NS>+<NS>` |
| `"NOT_TESTED"` | `+<NS>` | `<NS>+<NS>` |
| `"garbage+" + S1`（测试侧无效） | `+<NS>` | `<NS>+<NS>` |
| `"+<S1>"`（存量空测试侧） | `+<NS>` | `<NS>+<NS>` |
| `"<T1>+<S1>"`（测试侧有效） | `<T1>+<NS>` | `<T1>+<NS>`（不变） |

## 3. 兼容性分析

- **消费方 `messageActiveTime()`**：对 `<T>+<T>` 返回 max(T,T)=T，语义正确
  （活跃时间=启动时间）；对存量旧数据 `+<T>` 仍向后兼容（plusPos=0 → 测试侧
  视为空 → 返回启动侧）。
- **对称函数 `formatTestMessage()` 不改动**：测试结果写入时启动侧为空保持
  `<NT>+`——"从未启动过"是真实语义，不应伪造。
- 文件头注释契约同步更新：'+' 两侧在启动写入后均保证为合法时间戳。

## 4. 变更清单

| 文件 | 变更 |
| :--- | :--- |
| `src/ProfileExItemDAO.cpp` | `formatStartupMessage` 空测试侧分支返回 `newStartupTime + "+" + newStartupTime` |
| `tests/test_profile_ex_item_dao.cpp` | 更新 5 处断言（LegacyReplaced×3、InvalidTestSideDropped×2）+ 新增 `FormatStartupMessage_EmptyTestSideFilledWithStartupTime` 用例 |

## 5. 验证

- [x] TDD RED→GREEN：新断言先行失败，实现一行修改后通过
- [x] test_profile_ex_item_dao 全量通过
- [x] ctest 回归通过（NetworkMonitorTest 环境抖动除外）
