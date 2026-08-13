# Bugfix: 重启实例继续服务后续队列项 v1.0

- **日期**：2026-08-10
- **模块**：`src/ProxyBatchTester.cpp`（worker 循环生命周期管理）
- **关联问题**：Xray 实例崩溃后被 `evaluateInstanceHealth` 成功重启，但对应 worker 立即退出，重启实例不再参与当前批次剩余代理的测试
- **关联文档**：
  - `docs/bugfix/2026-08-07-Xray-instances-are-not-automatically-restarted.md`（健康评估 + 自动重启机制的引入）
  - `docs/bugfix/2026-08-10-Bugfix-XrayInstance-StderrCapture-v1.0.md`（进程死亡观测设施）
  - `docs/bugfix/2026-08-10-Bugfix-SplitHTTP-nil-request-Panic-v1.0.md`（同批次注入链路防御）

---

## 1. 背景与动机

**事实链（现状行为）**：

1. `ProxyBatchTester::workerThreadFunc` 为每个 worker 创建本地 `xray::XrayApi`，并通过 `setConnectFailureHook` 注册连接失败钩子：当 `grpcConnect` **连续 2 次失败**时，钩子执行——
   - `instanceDead.store(true)`；
   - `relaunchedOk.store(xrayManager_->evaluateInstanceHealth(apiPort))`：后者会 stop + 释放端口 + erase，再用原配置（同 socksPort → 同 config 文件）重新 start + pollApiPortReady，成功后把新实例 push_back 回 `instances_`（端口不变）。
2. 主循环顶（`while (true)`）的退出条件之一是 `if (instanceDead.load()) break;`。
3. `instanceDead` 一旦置位**从不复位**。因此：
   - 在途代理仅能通过「在途重试块」（L249-262，`!addSuccess && instanceDead && relaunchedOk` 时 removeOutboundDirect + addOutboundDirect 再试一次）获得一次挽救机会；
   - 之后 worker 回到循环顶 → `instanceDead` 为 true → **break 退出**；
   - 重启成功的新实例虽然留在 `XrayManager::instances_` 池中且端口不变，但本 worker 不再向它注入后续代理，只由其他存活 worker 分担剩余队列项。

**缺陷**：实例重启成功（`relaunchedOk == true`，API 端口已就绪、可正常服务）时，worker 却主动放弃该实例，导致**整批测试的有效并发 worker 数永久减一**，重启资源被浪费；在 worker 数本来就少（如 2-3 个）或多次崩溃场景下，剩余队列项的处理吞吐显著下降。

**预期行为（本修复目标）**：实例重启成功后，worker 应**继续其主循环**，用重启后的实例（同 `apiPort`）继续处理后续队列项；仅当重启失败（`relaunchedOk == false`）时才退出。

## 2. 变更范围

| 文件 | 变更 |
| :--- | :--- |
| `src/ProxyBatchTester.cpp` | `workerThreadFunc` 主循环顶：`instanceDead && relaunchedOk` 时复位 `instanceDead` 并继续循环（WARN 日志）；同步更新循环顶生命周期注释 |
| `docs/bugfix/2026-08-10-Bugfix-RelaunchedInstanceContinuesQueue-v1.0.md` | 本文档 |
| `docs/INDEX.md` | Bug 修复记录表追加行 38；§0 快速总览计数 35 → 36 |
| `docs/context.md` | §三 本会话引用表顶部插入本条目 |
| **不修改** | `XrayManager::evaluateInstanceHealth`（重启机制本身已正确）；`XrayApi` 注入链路（splithttp 校验等）；「在途重试块」L249-262（继续保留当前代理的一次挽救重试）；L202 注入重试循环条件（`!instanceDead` 语义不变）；部署的 Xray 二进制 |

## 3. 设计

**核心改动**：将循环顶的硬退出改为「重启成功则继续服务」的条件逻辑：

```
if (instanceDead.load() && relaunchedOk.load()) {
    WARN 日志（worker 号 + apiPort：实例已重启，继续服务剩余队列项）
    instanceDead.store(false);        // 复位，worker 继续主循环
} else if (instanceDead.load()) {
    break;                            // 重启失败（或未触发重启钩子）→ 退出
}
```

**正确性论证**：

1. **非陈旧性**：`relaunchedOk` 与 `instanceDead` 由同一个钩子调用写入（钩子在 worker 线程内同步执行，`std::memory_order_relaxed` 足够）。每次 `instanceDead` 置 true 时，`relaunchedOk` 必为同一次 `evaluateInstanceHealth` 的即时结果，不存在「旧 true 配新死亡」的窗口。
2. **与重启实例的兼容性**：worker 的本地 `xrayApi` 每次操作都通过 `grpcConnect` 新建 socket（连接地址 `127.0.0.1:<apiPort>` 不变），天然兼容同端口重启后的新实例；`removeOutboundDirect` / `addOutboundDirect` 无需感知实例对象变化。
3. **失败路径不变**：`relaunchedOk == false`（重启失败）时走原 `break` 退出，剩余队列项由其他存活 worker 承担（与现状一致）。
4. **反复崩溃自愈**：若重启后的实例再次死亡，钩子再次触发 → `instanceDead` 再次置位 + 新一轮 `evaluateInstanceHealth` → 在途重试块挽救一次当前代理 → 循环顶再次复位继续。实例每成功重启一次，worker 就继续服务一个队列子段，直至队列耗尽。
5. **无死循环风险**：每次循环迭代要么消费一个队列项（pop 后继续），要么因队列空 / 取消 / 重启失败而退出；复位不新增任何空转路径。
6. **日志可观测**：新增 WARN 日志与既有「instance relaunched (api=...) — retrying proxy ... once」日志互补，便于在运行日志中确认重启-继续-服务链路。

## 4. 验证方案

- 静态审查：确认循环顶逻辑覆盖「正常 / 重启成功 / 重启失败 / 取消 / 队列空」五类路径。
- 构建：`cmake --build build --parallel 8`（应 309/309 成功，0 error）。
- 全量测试：`ctest --test-dir build -V`（**必须带 `--test-dir build`**；根目录直接运行会报 `No tests were found!!!`），应 All tests passed。
- 运行验证（可选）：`.\build\validproxy-cli.exe` 对 `bin/test_config.json` 指向的测试库执行批量测试，观察日志中出现「instance relaunched (api=...) — continuing to serve remaining queue items」且该 worker 继续处理后续代理（不再出现 worker 提前退出的现象）。

## 5. 预期收益与后续

- **收益**：实例重启后立即恢复服务能力，整批测试的有效并发数不再因单次崩溃永久减一；崩溃频繁场景下的吞吐退化显著缓解；日志可完整追溯「崩溃 → 重启 → 继续服务」链路。
- **后续**：若观察发现同一实例反复崩溃（连续多次重启），可考虑为 worker 增加单实例重启次数上限（如 3 次）后降级为退出，避免对不稳定节点的无效重试；该策略变更需另行文档化。
