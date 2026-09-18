# 手动集成验收清单 — 单进程代理池（StandaloneProxyPool）

- 关联：`docs/specs/2026-08-26-Spec-StandaloneProxyPool-v1.0.md` §11 Step 10
- 前置：具备显示器 + 网络的 Windows 机器；已构建 `validproxy`（GUI）。

## A. 配置就绪（已落盘，无需再改）

- `bin/config.json` 已含 `standalone_pool.enabled=true`（socks 10809 / api 10810）。
- `bin/test_config.json` 已含 `standalone_pool.enabled=true`（socks 10829 / api 10830）+ `proxy.xray_executable = E:/eclipse_workspace/Xray-core/xray.exe`，数据库指向 `test/guindb.db`。
- 如需用生产库跑，直接双击 `build/validproxy.exe`（读 `bin/config.json`）；如用精简库演示，命令行 `.\build\validproxy.exe -c bin/test_config.json`。

## B. 启动与空池（Phase 1）

1. 启动 GUI，菜单「代理」→「代理池…」打开 `StandalonePoolDialog`。
2. 观察日志：`initializeStandaloneProxies` 启动单 Xray 进程（config 来自 `ConfigGenerator::buildPoolConfig`，含 `balancer-out` + Observatory + `selector:["px-"]`）。
3. 面板初始为空（Phase 1 无预加载）。状态栏显示监控循环已启动（REPORT 级日志 `evaluate loop tick` / 注入相关 INFO）。

## C. 双击注入（核心路径）

4. 在 `ProxyListPanel` 双击一条代理行 → `onStartProxy` 进入 `injectProxyToPool` 分支。
5. 预期：
   - 面板新增一行 `px-<indexId>`，状态 `INJECTING` →（成功）`ALIVE(—ms)` 或 `DEAD`；日志 INFO `inject member px-<indexId> ok`。
   - `xray api ado` 实际写入 outbound（tag `px-<indexId>`）；Balancer 经 `selector=["px-"]` 立刻纳入。
6. 重复双击 2~3 条，确认多成员并存、标签不冲突。

## D. 监控删除 + 两阶段 DRAINING

7. 在池面板选中某成员 → 删除。
8. 预期：状态先 `REMOVING` → 日志 `removeOutbound px-<indexId>` → 面板行消失；Xray 侧该 tag 退出选择集合与状态查询（存量连接自然结束）。
9. 用 `xray api lo` 可确认该 tag 已不在 outbounds 列表。

## E. 崩溃重注入（自动恢复）

10. 注入成功后再删除对应代理的 Xray 进程手工 `taskkill /im xray.exe`（仅池用的那个）—— 注：当前池不自动重启 Xray（仅 `relaunchAndReinject` 在 XrayInstance 崩溃时触发，需确认 AppController 崩溃回调已接 `relaunchAndReinject`）。
11. 若 Xray 进程被 kill，预期：AppController 崩溃检测触发 `relaunchAndReinject`，现存 `px-*` 成员被重新 `addOutbound`；面板状态回到 `ALIVE/DEAD` 而非永久卡 `REMOVING`。

## F. 健康开关（a/b/c）

12. `standalone_pool.evaluate.reportHealth=true`：面板实时显示延迟/状态（已实现）。
13. `autoPruneDead=false`（Phase 1 默认）：死节点保留，不自动移除（符合设计）。
14. `autoOptimize=false`（Phase 1 默认）：仅记录，不调用 `OverrideBalancerTarget`。

## G. 通过判据

- B/C/D 全流程无崩溃、无 gRPC 报错（日志无 `proto: not found` / `not found` 类错误）。
- `px-` 标签经 Balancer 纳入（可用 `xray api bal` 查看 balancer 候选含 `px-*`）。
- 删除后 `xray api lo` 不再含该 tag。
- 验证文档：`docs/bugfix/2026-08-26-Verify-XraySelectorCache-Observatory-Discovery-v1.0.md`（源码层已确认 selector 缓存失效 / Observatory 动态发现 / Remove DRAINING 兼容）。

## 备注

- 无网络环境下 Observatory 探测 `https://www.google.com` 会判 dead，但 **addOutbound/selector/status 链路本身与网络无关**，仍可证明动态注入结构正确；真实可用性需联网环境复测。
- 若不想动生产库，`bin/test_config.json` 已就绪，直接 `.\build\validproxy.exe -c bin/test_config.json` 即可。
