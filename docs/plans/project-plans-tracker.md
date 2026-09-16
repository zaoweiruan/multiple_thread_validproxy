---
title: "docs: Project Plans Tracker — global plan index and progress tracker"
type: docs
status: maintained
date: 2026-05-11
updated: 2026-08-24
---

# Project Plans Tracker

> **只记录文档引用，不记录修改、修复、操作等具体操作条目。**
> 替代旧有逐条状态更新写法；只保留文档路径和摘要说明。

---

## 近期文档引用

| 日期 | 类型 | 文档路径 | 说明 |
|------|------|----------|------|
| 2026-09-15 | bugfix | docs/bugfix/2026-09-15-Bugfix-StandaloneProbe-OperationBusy-v1.0.md | 独立代理周期 silent 探活被 AsyncOperationGuard 拒绝时弹出 "Operation Busy" 对话框 — 修复：silent 探活传入 nullptr handler 静默跳过；手动测试优先于 silent（cancel+join 后启动手动）；commit 5ce71e4 | ✅ completed |
| 2026-09-15 | spec+plan | docs/specs/2026-09-15-Spec-StandalonePeriodicProbe-v1.0.md + docs/plans/2026-09-15-Plan-StandalonePeriodicProbe-v1.0.md | 独立代理周期自动探活 — 复用池评估机制语义（ProxyTester 本地端口端到端 + updateTestResult + 阈值 WARN），随 proxy_process_monitor timer 周期静默触发，失败更新数据不关闭进程；失败达 pruneFailStreak（默认 3）才 WARN，ONLINE_PROBE_DONE 触发列表刷新；零新增配置；Task 1-3 全部完成（silent 参数 + 阈值 WARN + MainFrame timer + UI 刷新，commit 9f883ec/6801ead） | ✅ completed |
| 2026-09-15 | spec+plan | docs/specs/2026-09-15-Spec-PoolInjectDuplicateGuard-v1.0.md + docs/plans/2026-09-15-Plan-PoolInjectDuplicateGuard-v1.0.md | 代理池重复注入防护 — injectMember 显式检查 members_，重复 indexId 返回 false + WARN 日志（"already in pool"），不调用 addOutboundDirect；Task 1-3 全部完成（重复检查 + Live 测试 DuplicateInjectRejected + 非 UI ctest 40/40，commit 0b1d0a2） | ✅ completed |
| 2026-09-15 | spec+plan | docs/specs/2026-09-15-Spec-PoolDeadMemberWriteback-v1.0.md + docs/plans/2026-09-15-Plan-PoolDeadMemberWriteback-v1.0.md | 代理池死亡成员剔除后测试数据写回 — 死亡成员（failStreak 达阈值或 lastAlive=false）剔除时经 onMemberRemoved 回调写回 ProfileExItem（delay=-1 + 历史重置健康度归 0 + consecutive_failures+1，与独立代理测试失败一致）；Task 1-5 全部完成（isDeadMember 纯函数 + 两处剔除触发 + AppController 写回 + GTest 11 通过 3 live 跳过 + 非 UI ctest 40/40，commit b321c2e） | ✅ completed |
| 2026-09-15 | spec+plan | docs/specs/2026-09-15-Spec-AddPoolMemberDialog-Enhance-v1.0.md + docs/plans/2026-09-15-Plan-AddPoolMemberDialog-Enhance-v1.0.md | 代理池添加代理弹窗增强 — 有效过滤（delay>0）+ 8 列（时延/Region/健康度/Message/协议名）+ 单击列头排序 + Shift+单击多选；Task 1-6 全部完成（PoolCandidate 模块 + getPoolCandidateProfiles join SQL + AddPoolMemberDialog 改造 + GTest 11/11 + 非 UI ctest 40/40，commit ce1a3c4） | ✅ completed |
| 2026-09-14 | plan | `docs/plans/2026-09-14-Plan-PoolMemberHealthProbeWorkers-v1.0.md` | 代理池成员健康探针 worker 实施计划 — Task 1-4 已实施（ConfigReader `evaluate.probeWorkers` 字段 + ProxyProbePool 常驻探针池 + ProxyHealthEvaluator 接线 + StandaloneProxyPool 生命周期接线，commit 4ea257a/f4c8e84/e622739/1eec320）；Task 5 文档修订完成（Note 标题吞行修复/行号→functionName/P2 收窄注入路径/P3-P4 补清单 + Spec §6 探针池风险 + tracker/INDEX 登记，commit 3c98506） | ✅ completed |
| 2026-09-14 | bugfix | `docs/bugfix/2026-09-14-Bugfix-FloatingWidget-OrbHitZone-PoolStatusText-v1.0.md` | 悬浮球 Orb 命中区被 poolStatusText_ 截获（悬停/单击仅下半球有效）— 用户报告"右下二分之一位置弹窗有效，其它位置失效"；OS 级 7×7 网格 WindowFromPoint 探针实证失效带在球上部，未命中点归属 poolStatusText_（wxStaticText）；根因=统一池监控变更新增 8 个池控件中 poolStatusText_ 唯一遗漏 Hide/Show——Orb 模式其 HWND 横亘窗口上部截获全部鼠标 hit-test，与整体尺寸计算无关；修复=applyShape Orb 分支 `if (poolStatusText_) poolStatusText_->Hide();` + Panel 分支 `if (poolStatusText_) poolStatusText_->Show();`；TDD 新 UI 用例「Orb hit-test is not intercepted by child controls」（5×5 网格 WindowFromPoint 断言全部命中球 hwnd）；验证=构建 0 error + 新用例 GREEN + 非 UI 38/38 + UI 7/7（UI_FLOATINGWIDGET 13.30s 6 用例） | ✅ completed |
| 2026-09-14 | bugfix | `docs/bugfix/2026-09-14-Bugfix-FloatingWidget-EdgeExpandReposition-v1.0.md` | 屏幕边缘悬浮球展开后收回位置漂至屏幕中部 — 边缘球单击展开 Panel 600×480：applyShape 尾部 clampToScreen 将 rx 钳制到 margin=8 → 面板中心漂至屏中；收回 keepCenter 继承被钳制后面板中心 → 球落屏中而非原边缘；修复=setMode 保存展开前球中心快照（savedOrbCenter_ std::optional<wxPoint>，Orb→Panel 存、Panel→Orb 用后 reset）；TDD 新 UI 用例「Edge orb returns to original position after expand-and-collapse」（拖左缘→展开→钳制证明 panelCx-orbCx>100→收回→断言球心每轴差≤10px，RED 复现漂移→GREEN）；验证=构建 0 error + 非 UI 38/38 + UI 7/7 全绿 | ✅ completed |
| 2026-09-14 | bugfix | `docs/bugfix/2026-09-14-Bugfix-FloatingWidget-HideReposition-v1.0.md` | 悬浮球展开后收回位置回归屏幕右侧中部 — onTimer hideTimer 到期分支与 onActivate 失活分支两处 setMode(Mode::Orb); positionForCurrentEdge() 强制按 dockEdge_ 全屏居中停靠，丢弃自由拖放落点；修复=两处收回路径删除 positionForCurrentEdge()（setMode→applyShape 内 keepCenter 已保位）；TDD UI 用例 Click-expand then auto-hide keeps free-drop position（RED 275/215 偏移→GREEN 4 用例 34 断言）；顺带移除 3 处 ClickDiag 临时诊断日志；验证=构建 [7/7] + 非 UI 38/38 + UI 7/7（29.90s） | ✅ completed |
| 2026-09-11 | bugfix | `docs/bugfix/2026-09-11-Bugfix-PoolMemberId-DisplayAndMenuUnify-v1.0.md` | 池成员 indexId int64 截断与右键菜单/点选行为统一 — PoolMember/PoolMemberView.indexId 为 int 且 injectMember 用 atoi 解析真实 19 位 int64 级 indexId → 截断溢出 + members_ map<int> 键碰撞风险；悬浮窗池行标识列显示内部 px-tag 而非真实 ID，点选/「定位到代理列表」取 px-tag 致 LocateProxyEvent 定位失败；修复=indexId 全面 long long 化（injectMember/mergeHealth 两处 atoi→atoll、members_ map<long long>、removeMember/removePoolMember 签名）+ 池行统一显示真实完整 indexId + 右键菜单统一「关闭代理」（池行=优雅移除成员，独立行=原有停进程）+「定位到代理列表」，删 ID_MENU_REMOVE_POOL_MEMBER/onMenuRemovePoolMember；TDD 新用例 Int64IndexIdRenderedFully；验证=构建 0 error + 新用例 GREEN + 非 UI 38/38 + UI 7/7（29.33s） | ✅ completed |
| 2026-09-11 | bugfix | `docs/bugfix/2026-09-11-Bugfix-PoolEvaluator-RecursiveLock-Hang-v1.0.md` | 代理池 evaluatorLoop 递归锁 AppHang（第四层缺陷，统一池监控变更引入，生产独有路径）— 锁内新增 notifyChanged() → snapshotMembers 二次 lock membersMutex_ 同线程递归加锁，MinGW std::mutex=SRWLOCK 非递归=确定性死锁（evaluator 冻结永久持锁 → UI 5s timer 等锁 → AppHangB1，WER 同签名 a889/318e）；修复=删锁内调用（锁外已有等价通知，零功能损失）+ TDD EvaluatorNoRecursiveLockOnNotify（RED 60s 挂死→GREEN 2.17s，全套 10/10）；全量=构建 0 error + 非 UI 38/38 + UI 7/7（UI_POOL 53.80s→10.09s=stop 真完成不再被强杀掩盖）+ 0 新 dump/AppHang | ✅ completed |
| 2026-09-11 | bugfix | `docs/bugfix/2026-09-11-Bugfix-TrayIcon-HelperWindow-DoubleDelete-v1.0.md` | 托盘图标辅助窗口 m_win 同族双删除崩溃修复 + UI 测试 mainWindow() 枚举竞态 — WM_CLOSE 广播下 wxTaskBarIcon 私有懒创建辅助窗口（wxTaskBarIconWindow : wxFrame）走默认 Destroy → wxPendingDelete，~wxTaskBarIcon raw delete m_win 二次删除（dump addr2line 精确至 TrayIcon.cpp:39 + ~MainFrame:598）；修复=TrayIcon ctor 以 wxTopLevelWindows 差分捕获 m_win 并 Bind(wxEVT_CLOSE_WINDOW) 无捕获 no-op 拦截；配套=测试框架 enumProc 排除无标题/悬浮窗标题、优先精确匹配 MainWindowName（UI_EXITASSERT 92.82s 失败→0.96s PASS）；验证=构建 0 error + 非 UI 38/38 + UI 7/7 全绿 + 0 新 dump | ✅ completed |
| 2026-09-11 | bugfix | `docs/bugfix/2026-09-11-Bugfix-FloatingWidget-CloseDoubleDelete-v1.0.md` | 悬浮窗 WM_CLOSE 广播双删除崩溃（统一池监控弹窗后启动崩溃真根因）— 顶层 wxFrame 走默认关闭 Destroy → wxPendingDelete，DeletePendingObjects 先删悬浮窗后 ~MainFrame:571 raw delete 二次删除 → execute AV @ 堆（uptime 5s）；修复=ctor Bind(wxEVT_CLOSE_WINDOW) 拦截仅 Show(false)，生命周期完全归属 MainFrame；验证=UI_CLEAR 稳定失败转 PASS + UI 7/7 + 0 新 dump | ✅ completed |
| 2026-09-10 | bugfix | `docs/bugfix/2026-09-10-Bugfix-AppShutdown-PoolWatcher-UAF-v1.0.md` | 应用关闭期代理池/退出监视线程 UAF — ~AppController 不先停池（evaluatorLoop 回调垂死 this）+ ProcessExitListener::shutdown() detach 后释放 Watcher（detached 线程调用已释放 std::function = execute AV，c0000005 param 8 精确吻合）；修复=~AppController 首句 stopProxyPool() + shutdown() move-out-then-join + Watcher wakeEvent 即时唤醒（纯 join 会阻塞整个 interval，新测试捕获）；TDD 新增 2 用例 22/22；验证=非 UI 38/38 + UI 7/7 | ✅ completed |
| 2026-09-08 | review | `docs/specs/2026-09-08-Review-TrayMinimizeBehavior-v1.0.md` | 需求评审：监控弹窗双击 / 最小化到托盘 / 托盘图标 — R1 悬浮窗双击 最大化⇄最小化（`toggleMainFrameMaximize` 已实现核心语义，补隐藏态 `Show+Raise` 唤回，与托盘双击语义对齐）；R2 主窗口最小化→托盘（`onIconize` 已满足，无需改）；R3 托盘图标改为主图标（`TrayIcon` ctor `wxART_INFORMATION` → 资源 `icon_ico`，失败回退）；验证=构建 0 error + ctest -R UI_ 7/7 全绿 | ✅ completed |
| 2026-09-07 | spec+plan | docs/specs/2026-09-07-Spec-ConfigDialog-PoolSync-v1.0.md + docs/plans/2026-09-07-Plan-ConfigDialog-PoolSync-v1.0.md | 配置编辑窗口与 config.json 同步 — 规格主文（方案甲 12 属性 + singbox_asset_dir + 沙盒键名修正）+ 附录A（移除 log.network_failures 改 DEBUG 级别控制）+ 实施计划 Task 0-5（已评审通过，实施中） | ⏳ in-progress |
| 2026-09-08 | bugfix | `docs/bugfix/2026-09-08-Bugfix-UITestHarness-Deadlock-Stall-v1.0.md` | UI 自动化套件死锁与 UIA 卡顿修复：killProcessTree 祖先链成环死循环（64 跳上限）、悬浮窗双击 wxMSW 最小化后 Maximize 空操作（先 Restore）、UIA desktop FindFirst stall（HWND 直挂 fromHwnd）、EXITASSERT 慢启动 30s 上限过紧（改 90s）；验证=ctest -R UI_ 7/7 全绿 ×2 | ✅ completed |
| 2026-09-01 | bugfix | `docs/bugfix/2026-09-01-Bugfix-StatusBarNetworkTimer-v1.0.md` | 网络断连时状态栏始终显示"Network OK" — `NetworkMonitor` 正确检测断连但状态栏网络指示不更新；根因=wxEvtHandler::SearchDynamicEventTable 按反向注册序遍历、首个「匹配且未 Skip」即 return 终止；`onProxyMonTimer`（后注册）与 `onNetMonTimer` 均 `new wxTimer(this)`（逻辑 id=wxID_ANY）且 `Bind(wxEVT_TIMER,...,this)` 通配，onProxyMonTimer 优先吞掉每个 wxTimerEvent → onNetMonTimer 永不触发 → `netMonConnected_{true}` 恒真 → 恒绘 "Network OK"；修复=枚举加 `ID_NETMON_TIMER=wxID_HIGHEST+400`/`ID_PROXYMON_TIMER=wxID_HIGHEST+401` + `new wxTimer(this,ID_*)` + id 限定 Bind；验证=构建 0 error + 非网络单测 9 件全绿无回归 | ✅ completed |
| 2026-09-01 | spec | `docs/specs/2026-09-01-Spec-SubRefreshLoadAllProxy-v1.0.md` | 订阅面板「刷新」时加载全部代理（loadallproxy）— `onRefreshSubscription` 现仅 `loadSubscriptions()` 不触达代理列表；新增 `SubscriptionRefreshEvent`（Events.h/cpp 声明+定义）贯通「SubscriptionPanel → MainFrame Bind」事件解耦链路，`onRefreshSubscription` 补重入检查 + `loadSubscriptions()` 后经 `wxGetTopLevelParent` 投递事件，MainFrame `Bind(wxEVT_SUBSCRIPTION_REFRESH,...)` 调 `controller_->loadProxiesAsync("", this)`（空 subId=全表 getAll，复用 MainFrame.cpp:1188 现成模式）加载全部代理；AppController/ProxyListPanel 无需改动；验证=构建 0 error + ctest 全绿 + 手动 GUI 右键刷新→订阅源+代理列表同步刷新 | ⏳ in-progress |
| 2026-08-31 | spec | `docs/specs/2026-08-31-Spec-StandaloneProxyPreStartCleanup-v1.0.md` | 独立代理启动前置清理（standalone proxy pre-start cleanup）— 恢复 PR #3649905 前「关闭旧进程后重启」体验，但清理范围**限定当前 indexId 的独立代理配置进程**；`startStandaloneProxy` R1 段由软拒绝改为按命令行定位（`enumerateByName` + `extractConfigFileName` 匹配 `standalone_<indexId>-*.json`）→ 发现后**解锁 + wxMessageBox 征询同意**（死锁防护 lock/unlock）→ NO/取消保持旧软拒绝；YES 则 `OpenProcess(PROCESS_TERMINATE)+TerminateProcess+WaitForSingleObject(5000)` 定向终止匹配 PID，复检 `isProcessRunningWithConfig` 兜底无残留才继续；**不做 Job Object**（用户裁定）；scope=仅当前 indexId config 进程。验证=构建 0 error（38/38）+ ProcessInspectorTest 9/9 + StandaloneProxyPoolTest 8 PASS 1 SKIP；全量 ctest 39/42（3 项失败均环境性：XrayManagerStartupTest.RealXrayStartsBounded 依赖外部真实 xray.exe 启动失败 "not all dependencies are resolved"、UI_CLEAR/UI_FLOATINGWIDGET 的 `win.valid()`=false 无窗口会话，均与本次改动路径不相交） | ✅ completed |
| 2026-08-31 | bugfix | `docs/bugfix/2026-08-31-Bugfix-IsPortAvailable-Wildcard-v1.0.md` | isPortAvailable 对已占用端口误判可用（与 xray 持有 0.0.0.0:10808 撞端口不起效）— 实机证伪 bind() 方案（重要教训：Windows 通配符地址 bind 可与既有监听跨进程共存，bind 0.0.0.0:10808 即返回可用而 xray PID 13920 持有；仅同进程 bind+listen 冲突，致早期单测假绿）；终版修复 `src/Utils.cpp` 改用 GetExtendedTcpTable 枚举系统 TCP 表（isPortOccupiedInTable：AF_INET MIB_TCPTABLE_OWNER_PID + AF_INET6 MIB_TCP6TABLE_OWNER_PID，LISTEN\|TIME_WAIT 即占用，isPortAvailable=!isPortOccupiedInTable）+ CMake 补 link_libraries(iphlpapi)；TDD 新增 3 项 PortCheck 用例（通配符/IPv6 占用检测 + findAvailablePort 跳过）；验证=PortCheckTest 6/6 + 实机跨进程探针 `port 10808 occupied=1 (available=0)` 正确（旧 bind 返回 available=1）+ PortManagerTest 6/6 + test_standalone_proxy_pool 8 PASS 1 SKIP(live 项) + test_subscription_parser 9/9；部署=bin/ 与 bin/worker/ validproxy.exe 均已更新至 11:48:34 修复版（手动复制，遵守 AGENTS §4.1 禁止构建期自动拷贝 → bin/worker 为运行目录） | ✅ completed |
| 2026-08-26 | bugfix | `docs/bugfix/2026-08-26-Bugfix-StandaloneProxy-RestoreConsoleWindow-v1.0.md` | 独立代理恢复独立控制台窗口启动 + 启动失败日志显示返回值 — 回退 #68 的 `CREATE_NO_WINDOW`+管道+转发线程，`startStandaloneProxy` 改 `CREATE_NEW_CONSOLE`（子进程自带控制台窗口直接输出）；CreateProcess 失败日志补「返回值=0, GetLastError」，早期退出保留 `logCrashReason` 取 exitCode；3 处文案指向独立控制台窗口；验证=构建 0 error + ctest 39/40（XrayManagerStartupTest 并行偶发，单独复跑 PASS，与本次无关） | ✅ completed |
| 2026-08-24 | spec | `docs/DEV-PROCESS.md` | 开发流程规范 v1.0→**v1.1**：新增「UI 自动化测试规范」章节 — 适用范围（UI 元素/窗口/事件链路变更强制随附用例；纯算法/DAO/网络层由 GTest 覆盖）；框架组成（tests/ui/framework + Test*.cpp Catch2 3.x + `UIIds.h` 唯一定位源禁硬编码选择器 + scripts 四件套 + test/ui-sandbox 沙箱自动重建）；TDD 定位先行（新控件先 --dumptree 实测再固化 UIIds.h，禁猜选择器；RED→GREEN；交付门=build-and-test.bat ALL TESTS PASSED exit 0；失败 A/B/C/D 四分类、B 类≥3 次升级评审、严禁削断言）；数据隔离红线（仅触碰 test/ui-sandbox，严禁读写 bin/worker/guindb.db 与 test/guindb.db；GUI 以真实 exe `-c` 沙箱启动，测试进程不链 wxWidgets；失败产物至 test-results/ui-artifacts）；已知限制 D5（DataView 自绘单元格断言）/D8（xray.executable 未显式写沙箱 config，存在性守卫兜底）列二期；顺带修正 §流程 step7 过期引用 todo-tracker→project-plans-tracker.md；同步更新 AGENTS.md §4.1 新增命令 #6 + INDEX.md §3 描述行 | ✅ completed |
| 2026-08-24 | plan | `docs/plans/2026-08-24-Plan-UITestFramework-v1.0.md` | wxWidgets UI 自动化测试体系落地（承接 Spec-install-test-Framework）— 零业务代码改动，新增 UITests target（vcpkg classic 装 catch2:x64-mingw-static，manifest 偏差 D1 记录于计划）；沙箱 test/ui-sandbox 隔离生产库（exe 原地 `-c` 启动、DB 复制自 guiNDB_empty.db、关网络监控/自动订阅更新、xray 路径存在性防校验弹窗 D8）；UIA 定位 Name(窗口文本)+ClassName 兜底+PID 过滤防多实例；Task0-11：环境硬门禁→骨架→COM RAII→元素封装→进程生命周期(WM_CLOSE→Terminate)→失败截图+树dump+JUnit→--dumptree 发现固化 UIIds→三用例(mainwindow/search/clear)→bat 三件套→实跑修复循环≤5(A业务/B框架/C用例/D环境分类)→交付报告；范围裁剪=DataView 自绘内容断言列二期(D5)；验收=build-and-test.bat 一键 ALL TESTS PASSED 且 GTest 32 项不受影响 | ✅ completed |
| 2026-08-24 | report | `docs/reports/2026-08-24-Report-UITestFramework-v1.0.md` | **UI 自动化测试体系落地交付报告** — 一键 build-and-test.bat ALL TESTS PASSED（UI 三用例 100% 3/3，ctest 1.74/1.85/1.25s）；GTest 32 项不受影响（NetworkMonitorTest 全量负载偶发失败属已知环境抖动白名单，单独复跑 7.88s 通过）；破坏性演练实证失败链路（改坏 MainWindowName → exit=42 + failure_*.png 68KB/*.txt 50KB UTF-8 树自动生成 → 还原全绿 16 assertions）；12 项交付清单、A/B/C/D 分类修复（Uia init 缺失/COM 析构顺序/PID 过滤/wofstream 中文 badbit/GUI 等号参数不解析/searchCtrl 容器定位）、二期遗留（DataView 单元格断言 D5、scripts 入库 git add -f）；偏差=bat 四件套实际平铺 `scripts/` 根而非计划的 `scripts/ui_tests/`（架构师裁决接受：随既有平铺惯例、%~dp0.. 互调无需改、gitignore 行为不变）；二期加固=沙箱 config 未显式写 xray.executable（D8 守卫经存在性检查兜底，实测两轮全绿无弹窗，建议补写显式路径）；报告修订 **v1.1**=补录架构师独立验收（ctest -N 注册 35 项含 UI 三用例 / 三用例独立复跑 3/3 / 一键端到端实跑 ALL TESTS PASSED exit 0）、bat 平铺 scripts\ 根偏差裁决（接受：随既有平铺惯例）、规范集成记录（DEV-PROCESS v1.1 + AGENTS.md §4.1 命令#6 + INDEX 同步） | ✅ completed（v1.1） |
| 2026-08-24 | bugfix | `docs/bugfix/2026-08-24-Bugfix-InstantSubSwitch-CacheSubset-v1.0.md` | 即时订阅切换后其它订阅显示为空（缓存误存过滤子集）— 即时切换实现（同日 Spec #64）误假设"ProxyListLoadedEvent 携带全表"，实际 loadProxiesAsync 后台线程 dao.getAll() 全表经 copy_if 过滤后事件只携子集且全表即弃 → 面板 allProxies_ 仅含首个订阅行，切换其它订阅内存过滤 0 命中显示空列表；修复=AppController.cpp 删除 copy_if 分支让事件直接携带 getAll() 全表（零额外 IO/内存，全表本就读完即弃）+ ProxyListPanel.cpp 4 参 loadProxies 缓存存全表后尾部视图逻辑委托 applySubscriptionFilter 复用（净减重复代码）；验证=构建 0 error + ctest 32/32 | ✅ completed |
| 2026-08-24 | spec | `docs/specs/2026-08-24-Spec-ProxyListPanel-InstantSubSwitch-v1.0.md` | 订阅切换零 DB 内存过滤（ProxyListPanel 白屏阻塞修复）— 根因=每次切换 MainFrame:282 无条件启动 loadProxiesAsync 后台线程全表读取（profiles 5.3 万行 + 全量 ProfileExItem + buildProxyListMaps），延迟 O(全库) 与所选订阅记录数无关，0 记录订阅同样白屏等待，快速连续切换堆叠 detached 线程争抢 IO 加重阻塞；面板已缓存全集（allProxies_/exItems_/预构建 maps）且同文件 filterBySearch 已验证纯内存过滤即时可用；修复=新增 `ProxyListPanel::applySubscriptionFilter(subId)`（cacheReady_ 未就绪回退 reloadFromDatabase()；就绪则内存过滤 + setDataWithoutRebuild 免 O(N) rebuildMaps + 双 Reset + detectIdOffset 镜像 updateProxyList 视图语义），MainFrame SubscriptionSelectedEvent 改调该方法；其余显式刷新入口保留异步全量刷新维持缓存新鲜；留档=stale-generation 防护范围外、filterBySearch rebuildMaps 开销另评；**回归修正见 Bugfix-InstantSubSwitch-CacheSubset-v1.0**；验证=构建 0 error + ctest 32/32 | ✅ completed |
| 2026-08-24 | spec | `docs/specs/2026-08-24-Spec-StandaloneFloatingWidget-v1.1.md` | **可配置悬浮窗监控独立代理进程（StandaloneFloatingWidget）v1.1 统一接管版 — 方案评审稿** — 悬浮窗**替代 StandaloneMonitorDialog**并成为「监控代理进程」唯一 UI；配置**零新增键**（复用 `proxy_process_monitor.enabled`+`checkIntervalMs`），**废弃 `targetProcessNames`**（状态栏 `proxyMonPanel_` 计数改由 `getRunningStandaloneCount()` 同源驱动）；折叠条常驻显示总数 + **鼠标悬停展开**详情表（IndexId/Host/时长/端口/PID），双击行经 LocateProxyEvent 定位；Ctrl+M/工具栏/菜单重定向 `toggleActive()`；透明度/置顶/悬停硬编码默认；含实施前 `targetProcessNames` 全仓清理核查清单；**状态=✅ completed，已实施（v1.0 已废弃）** | ✅ completed |
| 2026-08-21 | spec | `docs/specs/2026-08-21-Spec-ProxyListPanel-ColumnReorder-v1.0.md` | ProxyListPanel 列显示顺序调整（用户指定：Region / Latency ↕ / Health ↕ / Type / Host ↕ / Port / Message ↕ / Starts ↕ / Runtime ↕ / # / IndexId / Failures ↕ / Remarks）— 仅重排 `onColumnsInit` 的 AppendTextColumn 调用顺序，ProxyListModel 未动（COL_*/GetValueByRow/Compare/SetValueByRow 均基于模型列索引）；框架 Resort 时 Compare 收到模型列索引（m_sortOrder.GetColumn()=GetModelColumn()）与视觉位置解耦，排序不受影响；验证=构建 0 error + ctest 32/32 | ✅ completed |
| 2026-08-21 | spec | `docs/specs/2026-08-21-Spec-Toolbar-MonitorProxyButton-v1.0.md` | 工具栏新增「监控代理」按钮（打开独立代理监控对话框）— 新增 ID `ID_TOOL_STANDALONE_MON` 复用 onMenuStandaloneMonitor，插入配置按钮左侧；图标 tool_monitoring_proxy_process.png 经 icons.rc 嵌入 + bin/icons 回退；验证=构建 0 error + ctest 32/32 | ✅ completed |
| 2026-08-21 | bugfix | `docs/bugfix/2026-08-21-Bugfix-StandaloneProxy-PortExternalConfigDir-v1.0.md` | 独立代理由其它程序启动且配置文件在其它目录时无法解析监听端口 — 纳管仅双目录探测；外部程序把 standalone config 放在任意目录时文件找不到→端口恒 0；修复=新增 `ProcessInspector::extractConfigFullPath`（命令行提取含目录完整路径）+ 纳管端口解析第三步回退直读端口；权威无歧义（规避 OS 套接字查询多 inbound 端口错配，尤其 sing-box 多个 DNS inbound）；TDD 新增 `test_process_inspector` 9 用例 + CMake 注册；验证=构建 0 error + ctest 32/32 | ✅ completed |
| 2026-08-21 | bugfix | `docs/bugfix/2026-08-21-Bugfix-ProfileExItem-StartupTimeFallback-v1.0.md` | 监控代理启动时 message 测试时间为空的补位策略 — `formatStartupMessage`（src/ProfileExItemDAO.cpp:100）测试侧空/无效时由 `+<启动时间>` 改为 `<启动时间>+<启动时间>`（用启动时间补位测试侧），监控启动后 message 恒为双合法时间戳；兼容性=`messageActiveTime()` 对 `<T>+<T>` 返回 max=T、存量 `+<T>` 旧数据向后兼容；`formatTestMessage` 对称函数不动（启动侧空=从未启动过的真实语义）；TDD RED→GREEN：更新 LegacyReplaced/InvalidTestSideDropped 断言 + 新增 EmptyTestSideFilledWithStartupTime + 落库断言同步（20→39 字符双侧相等）；验证=test_profile_ex_item_dao 26/26 + ctest 31/31 | ✅ completed |
| 2026-08-21 | spec | `docs/specs/2026-08-21-Spec-StandaloneMonitor-Dialog-v1.1.md` | StandaloneMonitorDialog 监控列扩展 v1.1 — IndexId 后新增 Host 列显示 ProfileItem.Address：StandaloneMonitorRow 加 `host` 字段；`getWatchedStandaloneMonitors()` Pass2 装配循环经 `ProfileitemDAO::getByIndexId()`（参数化 bind_text，src/ProfileitemDAO.cpp:77）逐行填充——watched 条目 ≤个位数，避免 IN 列表 SQL 拼接转义风险且零新 DAO 代码；对话框 COL_HOST=1 插入 + InsertColumn(140px) + 渲染空值"-"（与 socksPort/pid 缺失语义一致）+ 窗口 720→860；设计决策=host 不入 proxy_runtime_history（运行时状态与生命周期审计分离，Address 权威源在 ProfileItem，副本会漂移）；边界=IndexId 已删除/地址为空显示"-"；验证=构建 0 error + ctest 回归通过（NetworkMonitorTest 环境抖动复跑通过） | ✅ completed |
| 2026-08-21 | bugfix | `docs/bugfix/2026-08-21-Bugfix-StandaloneMonitor-DataColumns-v1.0.md` | StandaloneMonitorDialog 数据列异常（运行时长恒 0 / 监控端口 "-"）— Bug B：`utils::getCurrentTimestamp()` 返回纪元秒字符串却被当 `"yyyy-MM-dd HH:mm:ss"` 喂给 `durationMsBetween`（sscanf 失败→0），AppController 共 7 处误用（对话框 now 基准 L1175、纳管 procStartedAt 回退、接管 newStartedAt 回退、正常启动 safeStartedAt 回退、正常启动/纳管/接管三处 baselineElapsedMs——后三者致心跳基线恒 0）；Bug A：悬垂进程由 worker 实例启动时配置在 `<exeDir>\worker\config\standalone_*.json`，纳管仅探测 `<exeDir>\config\` → WARN + 端口 0；修复=新增 `utils::getCurrentTimestampFormatted()`（localtime_s+strftime `%Y-%m-%d %H:%M:%S`）替换全部误用点（getCurrentTimestamp 纪元秒语义被既有排序键依赖，不做全局改动）+ 纳管配置路径双候选探测；TDD：test_utils 新增 `GetCurrentTimestampFormattedTest` 2 用例 RED→GREEN；验证=构建 100/100 + ctest 30/31（NetworkMonitorTest 环境抖动复跑通过，2026-08-18 已知偶发同款）+ GUI 回归待用户确认 | ✅ completed |
| 2026-08-21 | bugfix | `docs/bugfix/2026-08-21-Bugfix-StandaloneMonitorDialog-v1.0.md` | StandaloneMonitorDialog 构造期 wxASSERT 崩溃 — `buttonSizer->Add(wxButton, 0, wxALIGN_RIGHT)` 在 wxHORIZONTAL sizer 中使用非法水平对齐标志，触发 wxWidgets 3.3 `wxBoxSizer::DoInsert` 的 `wxFAIL_MSG`（水平 sizer 仅允许垂直对齐标志）；定位手段=objdump 全量反汇编 + 基址无关差分匹配解码断言堆栈（R=0x7ff69aea0000，ret=0x1400d2bd2 → 构造函数 0x1400d2bcd 调用点，id=0x1389/flags=0x200）；修复=移除冗余内层标志（整行右对齐由外层 topSizer->Add(buttonSizer,0,wxALIGN_RIGHT\|...) 承担，该处为垂直 sizer 合法）；验证=Debug 重链接通过（含终止占用进程 PID 16364），GUI 回归已确认（Ctrl+M 正常打开） | ✅ completed |
| 2026-08-20 | spec | `docs/specs/2026-08-20-Spec-RuntimeHistory-PidMatching-v1.0.md` | RuntimeHistory 会话匹配引入 PID 因子 + 系统进程启动时间 — ①`findInProgressHistory` 判断因子升级为 `indexId + pid + started_at` 三因子（杜绝崩溃重启/外部新启进程误复用旧 in-progress 会话：新 pid → 不命中 → insertStart starts+1；同 pid+同 started_at → 复用 starts 不 +1）；②`proxy_runtime_history` 新增 `pid` 列（幂等 ALTER，旧行 pid=NULL 不再被复用，审计保留）；③`started_at` 改为从系统获取进程启动时间（`ProcessInspector::processCreationTime`：GetProcessTimes creation time → FILETIME→`yyyy-MM-dd HH:mm:ss`，新增 `ProcessInfo.creationTime` 字段 + `enumerateByName` 填充）；④`insertStart` 签名增加 `int64_t pid`（3 调用点：正常启动 pi.dwProcessId / R6 takeover procs[i].pid / 悬垂纳管 procs[i].pid 均可获取）；⑤配套修正心跳基准：`ProcessExitListener::watch` 增加 `baselineElapsedMs`（= 纳管时刻−进程启动时间），duration_ms 含纳管前已运行时间，finalizeStop 聚合更真实；TDD 已实施（DAO 三因子 6 用例 + ProcessInspector 时间工具 5 用例 + baseline 心跳 1 用例）；验证=构建 0 error + ctest 30/30 | ✅ completed |
| 2026-08-20 | report | `docs/reports/2026-08-20-Report-ProxyListModel-StatsColumns-v1.0.md` | ProxyListPanel Starts/Runtime/Health 三列计算逻辑提取（**v1.1 修订：纳入 PID 匹配 + baseline 会话语义**）— Starts=`startCountMap_[id]=ex.start_count`（PID 三因子后崩溃重启新实例 +1 / 同实例纳管复用不 +1）；Runtime=历史 runtimeMap_（>0 才覆盖保留非零）+ runningDurations_（整体替换幂等，duration_ms 含纳管前 baseline），显示 M:SS；Health=贝叶斯平滑 (stable+1)/(start_count+2)（冷启动 0.0）+ 运行加成 min(running/30min,1)*0.3 封顶 1.0；排序不一致点：Runtime 排序仅比较 runtimeMap_ 历史部分不含运行中时长（候选改进，v1.1 确认仍存在）；含数据流图与关联历史修复（含 PID 匹配 spec） | ✅ completed |
| 2026-08-20 | bugfix | `docs/bugfix/2026-08-20-Bugfix-ProxyListModel-HealthColdStartZero-v1.0.md` | ProxyListPanel Health 列冷启动初始值 0.5 → 0.0 — 未测试代理（`start_count==0`）经贝叶斯平滑 `(stable+1)/(start_count+2)` 得出 0.5 "假健康"；修复=`ProxyListModel::rebuildMaps()` 对 `start_count==0` 强制 `healthMap_[id]=0.0`，`setRunningDurations()` 的 base 与运行 bonus 均纳入 `start_count>0` 条件（冷启动代理运行中也不授予 +0.3 加成）；有历史代理的贝叶斯平滑与 30 分钟运行加成保留；与 `ProxyScorer::compute` 冷启动 `history_score=0.0` 语义对齐；TDD 新增 `ColdStartProxyHasZeroHealth`（含运行中不授予 bonus 断言）；验证=构建 0 error + ctest 30/30 | ✅ completed |
| 2026-08-19 | spec | `docs/design/2026-08-19-Spec-ProxyProcessMonitor-v1.0.md` | 代理进程监控模块 — AppConfig 新增 `proxy_process_monitor` 配置段（enabled/checkIntervalMs/targetProcessNames）+ ProxyProcessMonitorConfigParser 解析器 + ConfigJsonSerializer 序列化 + ConfigDialog UI 类别「监控代理进程」+ AppController::getRunningStandaloneCount() + MainFrame 状态栏5字段（field3=proxyMonitorPanel 绿点+存活数）+ wxTimer 定时刷新 + 配置变更动态启停；Round-trip/Load 43 tests pass | ✅ completed |
| 2026-08-18 | bugfix | `docs/bugfix/2026-08-18-Bugfix-ProxyListRefresh-IdleRedraw-v1.0.md` | ProxyListPanel 未启动代理评价空转刷新 + Runtime 累加膨胀（双 bug 同源）— ①Bug A：`onRunningDurationsLoaded` 收到空 map 仍无条件 `notifyHistoryChanged()` + `listCtrl_->Refresh()` → 无 running 会话时 UI 每 3 秒强制重绘；②Bug B：`getRunningDurations()` 返回当前总时长绝对值（heartbeat 累计），但 `setRunningDurations()` 用 `+=` 累加 → Runtime 列每 3 秒翻倍膨胀（5s→15s→30s）；修复=`ProxyListModel` 新增 `runningDurations_` 成员（与 runtimeMap_ total 分离），`setRunningDurations` 改整体替换语义并返回 bool changed（空/相同快照返回 false），`getRuntime()`/`COL_TOTAL_RUNTIME_MS` = `runtimeMap_[id] + runningDurations_[id]`（幂等），`clear()` 同步清空；`onRunningDurationsLoaded` 仅 changed==true 才 notify + Refresh；TDD 新增 2 回归测试（`RepeatedRefreshDoesNotAccumulateRuntime` / `EmptySnapshotReportsNoChange`）；验证=test_proxy_list_model 6/6 + ctest 29/30（NetworkMonitorTest.LoggingOnConnectionLost 环境依赖失败：example.com 当前不可达，与本次无关） | ✅ completed |
| 2026-08-18 | spec | `docs/specs/2026-08-18-Spec-ProxyListRefresh-v2.0.md` | ProxyListPanel 定时刷新异步化（评价列 UI 卡死修复）— 根因：`refreshHistoryPeriodic()` 每 2 秒 UI 线程全表查询 5.3 万行 + computeBatch + 全量 rebuildMaps + 二次 DB 查询；修复=后台线程独立 sqlite3 连接（WAL/busy_timeout/cache_size）查 running 时长 → `wxQueueEvent` 投递 `RunningDurationsLoadedEvent` → `setRunningDurations` 增量更新 + `notifyHistoryChanged`（仅 start_count>0 行）+ Refresh；防抖 `refreshInFlight_`（std::atomic<bool>）；定时器 2000→3000ms；`AppController::getRunningDurationsAsync` 实现；网络监控 onNetMonTimer 非根因保持 2s；refreshResults() 全量路径保留（测试完成事件低频触发）；验证=构建通过 + ctest 30/30 | ✅ completed |
| 2026-08-18 | bugfix | `docs/bugfix/2026-08-18-Bugfix-TestExe-Comctl32-Manifest-v1.0.md` | test_proxy_list_model.exe 启动 0xC0000139 — wxcore 3.3.3 导入 comctl32 v6-only 导出（GetWindowSubclass/SetWindowSubclass/DefSubclassProc/RemoveWindowSubclass），test exe 无 manifest → 加载 system32 comctl32 5.82（无此 4 符号）→ STATUS_ENTRYPOINT_NOT_FOUND；validproxy 通过 icons.rc 内嵌 app.manifest 正常；修复=`tests/test_proxy_list_model.rc`（`1 24 "src/ui/app.manifest"`）追加目标源列表；排除假说（wxbase 670 符号全存在 / 系统 DLL 差集为 forwarded 误报 / minwxtest 仅 base+core 复现证明与业务无关）；验证=ProxyListModelTest 4/4 + ctest 30/30 全绿 | ✅ completed |
| 2026-08-18 | bugfix | `docs/bugfix/2026-08-18-Bugfix-Build-Deps-VcpkgMigration-v1.0.md` | 构建依赖迁移 E:/vcpkg → D:/vcpkg + Boost 改用 vcpkg 管理 + wxWidgets 3.3.3 适配 — ①删除 BOOST_ROOT（D:/boost_1_88_0），9 处 boost_json 库路径改 `${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a`（vcpkg boost-json 独立端口需代理安装）；②`copy_wx_dlls()` 更新 3.3.3 DLL 命名（wxbase333u_gcc_x64_*、libzd.dll、libtiffd-6.dll、libwebp*.dll），test_log_statistics_event 追加复制；③ConfigDialog.cpp `wxPG_FILE_DIALOG_TITLE`→`wxPG_DIALOG_TITLE`（3.3.3 兼容）；④zlib 包缺陷显式 `-DZLIB_LIBRARY=libzs.a`；⑤配置须显式 gcc 编译器 + 覆盖 CMAKE_CXX_FLAGS_DEBUG + `-O coff`；验证=构建 464 目标 ✅、ctest 29/29 ✅、CLI -h ✅ | ✅ completed |
| 2026-08-17 | bugfix | `docs/bugfix/2026-08-17-Bugfix-StandaloneProxy-WatchLifecycle-v1.0.md` | standalone 代理 watch 生命周期完善 — ①查重前置（UI 层 `isStandaloneProxyRunning` 端口检查前拦截 + startStandaloneProxy 内部查重提前至 profile 后）②watch 心跳（ProcessExitListener `HeartbeatFn` 30s 循环等待 + `touchHeartbeat` 定期刷新进行中会话 duration_ms，异常中断/外部停止仍留评价数据）③悬垂进程纳管（ProcessInspector `extractConfigFileName` + DAO `findInProgressHistory` 复用会话 + `adoptDanglingStandaloneProxies` MainFrame 构造后自动纳管 xray/sing-box 残留进程）；验证=98/98 构建 + 实机 PID 4084/5544 纳管 + taskkill 后 finalizeStop 自动收尾（ended_at/exit_code/duration_ms≈31s） | ✅ completed |
| 2026-08-17 | bugfix | `docs/bugfix/2026-08-17-Bugfix-ProxyConfigParser-WrongTypeWarning-v1.0.md` | 合法 proxy 配置误报 "has wrong type" 伪警告 — `ProxyConfigParser.h:95` 警告误放在 if 分支内部（合法 proxy 对象也触发）；修复=警告移至 `else if (obj.contains("proxy"))` 分支并补全 `(expected object), using default` 后缀；TDD 新增 2 回归测试（`ProxySectionValidObject_NoWrongTypeWarning` / `ProxySectionWrongType_UsesDefaultAndWarns`），RED→GREEN；test_config_reader 36/36、ctest ConfigReaderTest 100% passed | ✅ completed |
| 2026-08-14 | plan | `docs/plans/2026-08-14-Plan-ProxyScoringHistory-Implementation-v1.0.md` | 代理评分引入历史服务记录实施计划（承接 Spec v1.3，评审通过）— 开发环境 test/guiNDB.db（已备份 .bak）+ bin/config.test.json（已重写补 proxy/network_monitor/auto_task 段）；P1 数据层 ✅（proxy_runtime_history 明细表 + ProfileExItem 3 聚合列 + ProxyRuntimeHistoryDAO，嵌套事务 sqlite3_get_autocommit 模式；RuntimeHistoryDAOTest 19/19 通过）/ P2 采集层 ✅（ProcessInspector PEB+R1 查重+R2 连通验证 + ProcessExitListener R6 自动接管+事件投递；ProcessExitListenerTest 14/14, ProcessInspectorTest 14/14 通过）/ P3 评分引擎 ✅（ProxyScorer 20/30/50 三因子评分 + cold-start normalization；ProxyScorerTest 10/10 通过）/ P4 集成展示 ✅（AppController 启动流程接入 ProcessExitListener + ProcessInspector R1 查重 + finalize/takeover + score 写回 ProfileExItem；UI 列已接入；scoring.weights 读 config 完整集成）；DoD：ctest 新增≥15 用例 ✅（44 用例，28 通过）+ 端到端验证 | ✅ completed |
| 2026-08-12 | bugfix | `docs/bugfix/2026-08-07-Bugfix-GUI-AboutBuildTime-LogLevelSync-v1.0.md` | About 窗口编译时间修复（三次修复）— 源码树 stale `include/version.h` 被 `include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)` 优先检出，遮蔽 `build/include/version.h`；修复=删除源码树残留生成文件（`.gitignore` 已覆盖）；验证=exe 内嵌唯一时间戳 `2026-08-12 17:39:19`、旧时间戳消失、ctest 24/24、构建 347/347 | ✅ completed |
| 2026-08-12 | bugfix | `docs/bugfix/2026-08-12-Bugfix-LogPanel-Statistics-ReasonAggregation-v1.0.md` | 日志统计原因归一化聚合（去具体 host） — 统计弹窗 WARN/ERROR 原因 521 类全因按含 host 消息原文聚合拆类；根因=`parseLogFile` 按原文聚合；修复=src/LogStatistics.cpp 新增 `extractReason` 归一化聚合键（`SKIP: <host>[:port] - <reason>` → `<reason>`，无 ` - ` 分隔符/非 SKIP 保留原文）+ WARN/ERROR 分支聚合键改 `warnAgg[extractReason(message)]`/`errorAgg[extractReason(message)]`；标题行格式已具备；LogStatisticsTest 10 用例、构建 347/347、ctest 24/24 | ✅ completed |
| 2026-08-12 | bugfix | `docs/bugfix/2026-08-12-Bugfix-Ss2022-XrayApi-Protobuf-v1.0.md` | shadowsocks-2022 出站 gRPC 注入编码修复 — legacy `xray.proxy.shadowsocks.ClientConfig`/`Account` cipher 枚举无 2022-blake3 值 → Xray 拒 unsupported cipher、SS2022 批量测试全失败；修复=编码层 Method A：`isShadowsocks2022` 标志（parseOutboundJson 按 `method.rfind("2022-",0)==0` 选型）+ 新分支按 `xray.proxy.shadowsocks_2022.ClientConfig` 扁平编码（field1 address / 2 port / 3 method / 4 key=password 原样，无 Account 嵌套，空 method/key 回退子进程）+ supportedTypeUrls[] 注册 + 空配置回退检查；legacy 路径不变；7 新单测、构建 347/347、ctest 24/24 | ✅ completed |
| 2026-08-12 | bugfix | `docs/bugfix/2026-08-12-Bugfix-LogPanel-Statistics-Reopen-v1.0.md` | LogPanel 日志统计按钮关闭弹窗后无法再次打开 — `std::thread::joinable()` 在线程执行完毕后仍返回 true 直至显式 join/detach；`onLogStatistics` 防叠加守卫首次点击后 statsThread_ 恒 joinable、`onLogStatisticsResult` 从不回收线程 → 后续点击被守卫拦截；修复=结果回调开头 `if (statsThread_.joinable()) { statsThread_.join(); }` 回收已结束线程（赋值/回收均在 UI 线程无竞争，解析中重复点击仍防叠加，析构 join 兜底不变）；构建 347/347、ctest 24/24 | ✅ completed |
| 2026-08-12 | spec | `docs/specs/2026-08-12-Spec-LogPanel-Statistics-v1.0.md` | LogPanel 清空按钮文案调整 + 异步日志统计弹窗 — Clear 文本改「清空日志窗口」；新增「日志统计」按钮（ID_LOG_STATISTICS）异步解析当前日志文件（`parseLogFile` 纯 std 解析器，`LogStatistics.h`/`LogStatistics.cpp` + std::thread + wxQueueEvent 投递 `LogStatisticsEvent`），弹窗输出 TRACE/DEBUG/INFO/REPORT/WARN/ERROR 六级别计数，WARN/ERROR 额外输出原因（消息原文）及次数（降序）；析构 join 防悬垂；LogStatisticsTest 8 用例；构建 348/348、ctest 24/24 | ✅ completed |
| 2026-08-12 | spec | `docs/specs/2026-08-12-Spec-LogPanel-OpenLogButton-v1.0.md` | LogPanel 工具栏新增「打开日志」按钮 — Clear 后新增按钮一键打开当前日志文件（`Logger::getFilePath()` + ShellExecuteA/wxLaunchDefaultApplication）；事件表由 wxID_ANY 改为具体 ID（消除新增按钮后 Clear 被重复触发陷阱）；移除状态栏整条 `wxEVT_LEFT_DCLICK` 双击绑定（消除 field2/3 误触发），`onStatusBarDClick`/`logFilePath_` 删除；Field1 日志文件名显示保留；构建 341/341 | ✅ completed |
| 2026-08-12 | spec | `docs/specs/2026-08-12-Spec-ProfileExMessage-Sort-v1.0.md` | ProfileExItem message 字段排序（最近活跃时间）— Message 列排序键 = 测试时间与启动时间中较新者；`ProfileExItemDAO::messageActiveTime`/`compareMessage` 静态方法（严格 19 字符校验，空/遗留值排最后）；`ProxyListModel::Compare` COL_MESSAGE 接入；ProfileExItemDAOTest 排序用例 | ✅ completed |
| 2026-08-11 | spec | `docs/specs/2026-08-11-Spec-ProfileExMessage-v1.0.md` | ProfileExItem message 双时间戳 — message 仅写入测试成功时间 + 代理启动时间（恒含 '+'，测试时间恒在前）；`formatTestMessage`/`formatStartupMessage`（仅替换自身一侧、保留另一侧，严格 19 字符时间戳校验，旧值整体替换）/`updateStartupTime`；AppController::startStandaloneProxy 启动成功后写启动时间；测试失败不再覆盖 message；导入初始 message "NOT_TESTED"→空；ProfileExItemDAOTest 17 用例 | ✅ completed |
| 2026-08-10 | plan | `docs/plans/2026-08-10-Plan-ImportProxyValidation-v1.0.md` | 订阅导入数据污染治理计划（依据 `docs/bugfix/2026-08-10-Analysis-InjectionError-ConfigRootCause-v1.0.md` §4.7/§6 P1）— 私网地址 42 条 0.014%（上游聚合源污染，indexid 5323085616270219902 上游逐字段命中）；三层拦截：U1 utils 纯函数（isPublicAddress 数值判定 RFC1918 172.16-31 / isValidUuid 8-4-4-4-12 / isSupportedSsCipher AEAD+2022-blake3 白名单）→ U2 SubitemUpdaterV2::isValidProxy 导入闸门 → U3 Deduplicator::deduplicateConfigErrorPhase 存量清洗 → U4 ProxyBatchTester::preGenerateConfigs 显式预过滤（pregenFailedFlags_ skip）；ShareLink 仅导出排除；范围不含注入链路/ConfigGenerator | ✅ completed |
| 2026-08-07 | bugfix | `docs/bugfix/2026-08-07-Bugfix-MainFrame-StatusBar-Field0-v1.0.md` | 状态栏 Field0 被菜单帮助文本机制清空且点击关闭不恢复 — DoGiveHelp 菜单打开时空 help 清空 Field0（framecmn L577 m_statusBarPane<0 官方开关）；ESC 关闭可恢复、点击关闭不恢复；修复=initStatusBar 后 SetStatusBarPane(-1) + doTestAllProxies/doTestSingleProxy 启动状态文本 + 移除 dumpStatusBarGeometry；菜单全链路 SB_GETTEXTLENGTH lens=[5,22,0,61] 恒定 | ✅ completed |
| 2026-08-07 | report | `docs/reports/2026-08-07-Report-GitMasterSkill-Install-v1.0.md` | git-master 技能安装记录 — AGENTS.md §6.2 路由引用的 skill(name="git-master") 环境缺失；GitHub 直连不通致 npx skills add 失败（各镜像 403/404），配置 git 全局代理 socks5://127.0.0.1:10808 后手动 clone josiahsiegel/claude-plugin-marketplace 并复制 skills\git-master\ 至全局技能目录（10 文件）；重启 Kilo 后生效 | ✅ completed |
| 2026-08-07 | bugfix | `docs/bugfix/2026-08-07-Bugfix-GUI-AboutBuildTime-LogLevelSync-v1.0.md` | About 窗口编译时间不准确 — version.h 生成依赖 build.ninja 致仅 configure 时刷新，改 add_custom_target(update_version_h ALL) 每次构建重生成 APP_BUILD_TIME；二次修复：Ninja _unscanned 规则无 depfile 致未改源码 TU 保留旧时间戳，OBJECT_DEPENDS 钉死 version.h 到 main_gui/main_cli/MainFrame 3 个 TU；配置窗口保存 console 日志级别未同步 LogPanel 界面过滤，保存回调补 setInitialLogLevel(cfg.log_console_level) | ✅ completed |
| 2026-08-07 | plan | `docs/plans/2026-08-03-Plan-CodeAudit-Optimization-Implementation-v1.0.md` | 状态变更：draft → ❌ terminated（2026-08-07 终止）— Phase 1-4 全部落地（A1-A6/B1-B14/C1-C9/D1-D8/E1-E14，21/21 ctest）；Phase 5 尾项未完成：T5.1 全仓 auto 残留 15 处（触碰文件 XrayApi.cpp 10 处 + CurlEasyHandle.h L149 已清）、T5.2 无记录，按 §2 边界顺延单独立项 | ❌ terminated |
| 2026-08-07 | plan | `docs/plans/feature-status.md` §7 | 计划文档落地核验（源码 grep 证据）— Sing-box 全链路（Spec 2026-07-02）✅ / GitTag 版本头（Spec 2026-07-15）✅ / Region 列迁移（Spec 2026-07-07 Phase 2 之迁移部分）✅ / 批量 INSERT Phase B（Spec 2026-07-24）❌ / 去重阶段分区富化 deduplicateRegionPhase（Spec 2026-07-07 Phase 2）❌ | 📝 核验记录 |
| 2026-08-07 | bugfix | `docs/bugfix/2026-08-07-Bugfix-SubitemUpdater-SkipLogLevel-v1.0.md` | 订阅更新跳过提示误报 ERROR — updateAll() 全部订阅因更新间隔被跳过时（非错误，随后 return true）以 ERR 输出 "All subscriptions skipped by update interval - nothing to update"；修复=ERR→REPORT；真正失败的 "failed to update" 保持 ERR | ✅ completed |
| 2026-08-07 | bugfix | `docs/bugfix/2026-08-07-Bugfix-NetworkMonitor-ProbeLogLevel-Warn-v1.0.md` | 网络监控探测错误详情日志级别过低 — CheckURLWithDnsFlag 内 probe failed(DNS 错误 TRACE) 与 http_code 详情从 DEBUG/TRACE 提升为 WARN，使故障信号在生产 console_level=WARN 下可见；ThreadLoop 状态机日志（LOST/RESTORED）保持 ERR | ✅ completed |
| 2026-08-07 | bugfix | `docs/bugfix/2026-08-07-Bugfix-GuiLogFileLevel-v1.0.md` | GUI 启动日志未按 config.json file_level 过滤写入文件 — main_gui.cpp Logger::init 前预解析 log 段，用真实 file_level/console_level 初始化（INFO gui entry 与 ConfigReader::load 内部 DEBUG SQL 不再写入 file_level=ERROR 的日志文件） | ✅ completed |
| 2026-08-06 | bugfix | `docs/bugfix/2026-08-06-Bugfix-Logger-GUI-ConsoleLevel-v1.0.md` | GUI 入口 Logger 启动级别补全 console_level 配置 + LogPanel 筛选器同步 config.json log_console_level |
| 2026-06-12 | bugfix | `docs/bugfix/2026-06-12-Bugfix-ProxyBatchTester-Worker0-JoinTimeout-v1.0.md` | Worker join 超时修复 v3 — 动态 join timeout（v1 7s 不足，v2 ping 轮询过重导致启动延迟） |
| 2026-06-11 | bugfix | `docs/bugfix/2026-06-11-Bugfix-ProxyBatchTester-ZeroProxyEarlyReturn-v1.0.md` | 批量测试零代理时 printSummary/stopAll 缺失修复 |
| 2026-06-11 | bugfix | `docs/bugfix/2026-06-11-Bugfix-ProxyListPanel-RefreshFreeze-v1.0.md` | 大代理集测试后 UI 冻结修复 |
| 2026-06-11 | bugfix | `docs/plans/2026-06-11-Spec-ProxyFinder-findWorkingProxy-bug.md` | ProxyFinder::findWorkingProxy 返回端口时未重新注入代理 |
| 2026-06-11 | plan | `docs/plans/2026-06-11-Plan-Stability-Hardening-v1.0.md` | 稳定性加固计划（ASAN/UBSan/MiniDump/静态分析/覆盖率） |
| 2026-06-11 | bugfix | `docs/bugfix/2026-06-11-Bugfix-ConfigGenerator-NetworkFallback-v1.0.md` | 无效网络兜底与 splithttp→xhttp 回退 |
| 2026-06-09 | plan | `docs/plans/2026-06-09-config-validation-improvements-plan.md` | Config validation improvements |
| 2026-06-09 | bugfix | `docs/bugfix/2026-06-09-configreader-load-error-popup-fix.md` | ConfigReader load double-popup fix |
| 2026-06-09 | plan | `docs/plans/2026-06-09-feat-ui-resizable-splitter-v1.0.md` | Resizable splitter |
| 2026-06-04 | bugfix | `docs/bugfix/2026-06-04-subitemupdater-hardcoded-binconfig-path.md` | SubitemUpdaterV2 hardcoded path fix |
| 2026-06-12 | spec | `docs/plans/2026-06-11-Spec-Refactoring-Phase1-v1.0.md` | Phase 1 重构方案状态更新 — auto(17/17)✅ / SQL注入(3处)✅ / Logger(实例化+委托)✅ / 全部 92 tests pass |
| 2026-08-03 | spec | `docs/plans/2026-08-03-Plan-CodeAudit-Optimization-Implementation-v1.0.md` | 代码审计与优化实施计划（Phase 1-5，T1.1-T5.2，含 ASAN/性能/稳定性） | ❌ terminated |
| 2026-06-16 | bugfix | `docs/bugfix/2026-06-16-Bugfix-Review-AutoTask-CLI-StateFile-v1.0.md` | Code Review 修复: #2 状态文件路径集中 + ConfigDialog 移除, #4 未使用图标删除 |
| 2026-06-18 | spec | `docs/specs/2026-06-18-Spec-Responsibility-Decomposition-v1.0.md` | 过度职责分解方案 — ProxyBatchTester/AppController/ShareLink/ConfigGenerator/ConfigReader 五阶段拆分 |
| 2026-06-18 | spec | `docs/specs/2026-06-18-Spec-SubitemUpdaterV2-Decomposition-v1.0.md` | SubitemUpdaterV2 分解已完成 — 提取 SubscriptionParser/Deduplicator/Importer/SubscriptionUpdater 四个类 |
| 2026-06-18 | plan | `docs/plans/2026-06-18-Plan-Decomposition-Five-Phase-Implementation-v1.0.md` | 五阶段实施计划 — 逐任务拆解 ConfigReader → ShareLink → ConfigGenerator → ProxyBatchTester → AppController |
| 2026-05-19 | — | 批量状态更新 | P4→❌ cancelled; 003->completed; DLL fix->completed |
| 2026-07-09 | spec | `docs/specs/2026-07-09-Spec-DnsCache-v1.0.md` | DNS 缓存解析模块 — DnsCache 类 getaddrinfo + 惰性 WSAStartup + 互斥锁缓存 | ✅ completed |
| 2026-07-09 | bugfix | `docs/bugfix/2026-07-09-Bugfix-RegionResolver-Hang-v1.0.md` | RegionResolver 假死修复 — DNS 超时(std::async+10s) + 缓冲互斥锁解耦 + Worker 退出日志 | ✅ completed |
| 2026-07-24 | spec | `docs/specs/2026-07-24-Spec-SubscriptionWritePerformance-v1.0.md` | 订阅写入性能优化 — PrAGMA 调优(2-5x) + 批量INSERT + 内存哈希去重(10-50x) + 复合索引(50-100x)，4阶段实施方案 | 📝 draft |
| 2026-07-24 | plan | `docs/superpowers/plans/2026-07-24-SubscriptionWritePerformance-Optimization-v1.0.md` | 订阅写入性能优化实施计划——Phase A Pragma调优 / Phase B 批量INSERT+哈希去重 / Phase C 复合索引+Dedupulator优化，含完整代码变更步骤和验收标准 | 📝 draft |
| 2026-07-29 | bugfix | `docs/bugfix/2026-07-29-Bugfix-XrayApi-gRPC-AddOutboundDirect-Fields-v1.0.md` | addOutboundDirect TypedMessage field 2→3 (proxy_settings) + 两处 gRPC 路径 CommandService→HandlerService 修复，4 新增单元测试，23/23 tests pass | ✅ completed |
| 2026-07-29 | bugfix | `docs/bugfix/2026-07-29-Bugfix-XrayApi-gRPC-AddOutboundDirect-Fields-v1.0.md` | addOutboundDirect JSON parse 回退修复 — read `outbounds[]` 数组而非 `outbound` 单对象，parseOutboundJson 静态提取，10 协议映射，33/33 tests pass | ✅ completed |
| 2026-08-03 | plan | `docs/plans/2026-08-03-Plan-CodeAudit-Optimization-Implementation-v1.0.md` | 代码审查优化实施计划 — 26 任务 5 阶段（崩溃/UB → 正确性 → 性能 → 低危收尾 → 规范清理），每任务含位置/变更/验收，A3 测试期望同步修正；Phase 1-4 全部落地：A1-A6/B1-B14/C1-C9/D1-D8/E1-E14（C2/C5/C8/C9/E3/E4/E8/E9b 验证为已实现），21/21 ctest 通过；Phase 5 尾项未完成（T5.1 全仓 auto 残留 15 处 / T5.2 无记录），2026-08-07 终止 | ❌ terminated |
| 2026-08-04 | bugfix | 本会话 `docs/context.md` 2026-08-04 条目 | 批量测试网络监控误报断网 — waitForNetworkRecovery 无超时 CV 死等重写为 250ms 轮询 + 30s 上限；清理 fc15eec 调试残留（2×listOutboundsDirect/代理 + outbound_json TRACE）；21/21 通过 | ✅ completed |
| 2026-08-04 | bugfix | `docs/bugfix/2026-08-04-Bugfix-SubscriptionParser-GarbageSS-v1.0.md` | 订阅解析乱码 Shadowsocks 节点 — Argh94 订阅 malformed ss:// 链接（VLESS 参数伪装成 ss）+ decodeBase64 非法字符解码为索引 0 → 全库 29811 个代理 Security/Id 二进制垃圾致批量测试全失败；修复：decodeBase64 跳过非法字符 + ss:// 分支 isPrintableAscii 校验丢弃 + Deduplicator 去重阶段删除非法配置代理 + 日志降噪（XRAY_ERROR 不再打印完整 outbound JSON 消除密码泄露）；21/21 通过 | ✅ completed |
| 2026-08-04 | bugfix | `docs/bugfix/2026-08-04-Bugfix-XrayApi-XHTTP-SplitHTTP-Mapping-v1.0.md` | gRPC xhttp 传输编码与 Xray v26.2.4 兼容修复 — 新构建首次真正编码 xhttp（protocolName="xhttp" + `xray.transport.internet.xhttp.Config`）但 v26.2.4 已移除 xhttp 协议、该消息类型未注册 → AddOutbound `proto: not found`（旧构建是静默退化裸 TCP 的假成功，非代码回退）；修复 network=="xhttp" 统一按 splithttp 编码（protocolName="splithttp" + `xray.transport.internet.splithttp.Config`，与 v26.2.4 JSON 适配器 xhttp→splithttp 重映射一致）；单元测试 3/3 + 真实 v26.2.4 端到端 xhttp OK=true 验证 | ✅ completed |
| 2026-08-05 | spec | `docs/specs/2026-08-05-Spec-DnsCache-Permanent-v1.0.md` | DNS 解析结果永久缓存 — DnsShareCache 程序级共享（CURLSH + DNS_CACHE_TIMEOUT=-1）已实施 + NetworkMonitor dnsCache_ 整体删除（只写不读死代码）；NetworkMonitorTest 11 用例通过 | ✅ completed |
| 2026-08-05 | spec | `docs/specs/2026-08-05-Spec-ImportProxyValidation-v1.0.md` | 导入订阅稽核 — utils::isPrintableAscii 提取统一 + isValidProxy Security/Id 可打印 ASCII 校验（与去重阶段判定一致）；test_utils 单测 | ✅ completed |
| 2026-08-05 | bugfix | `docs/bugfix/2026-08-05-Bugfix-ProxyBatchTester-PreGenParseError-v1.0.md` | 批量测试 PreGen 失败节点触发 boost.json 解析错误刷屏与卡顿 — preGenerateConfigs 失败 push 空 outbound_json → worker 对空串 addOutboundDirect → parseOutboundJson 抛 `syntax error ... parse_string`（3 次重试×5s 超时放大）；E7 skip（pregenFailedFlags_ + worker 入口跳过）+ DIAG 防御输出坏 config 前 80 字节 hex；PreGenFailedSkipTest 7 用例；21/21 通过 | ✅ completed |

---

*项目全量文档索引见 `docs/INDEX.md`*

---
- [x] U1: `ProxyBatchTester.cpp` — 4× `LogLevel::WARN` added for boundary conditions
- [x] U1: `ProxyBatchTester.cpp` — 4× `LogLevel::INFO` replaced REPORT (xray lifecycle + network)
- [x] U2: `SubitemUpdaterV2.cpp` — sync failure summary log added (`LogLevel::ERR`)
- [x] U3: `XrayInstance.cpp` — 4× `LogLevel::ERR` added, 3× REPORT→INFO
- [x] U4: `XrayManager.cpp` — `LogLevel::ERR` (port fail) + `LogLevel::WARN` (instance fail)
- [x] U5: `ConfigGenerator.cpp` — 4× missing `LogLevel` params added (2×INFO, 2×WARN)
- [x] U6: Residual `std::cerr`/`std::cout` removed from `ProxyBatchTester.cpp`

---
- [x] `bin/config.json` notification block has `enabled`/`on_update`/`on_test`
- [x] `bin/worker/config_test.json` notification block complete
- [x] `bin/test_config.json` notification block added
- [x] `bin/test_config_full.json` notification block added

### Production Sync Fix
- [x] `bin/worker/config.json` `sync.target_db` corrected
- [x] `bin/worker/config.json` `database.path` corrected to absolute path
- [x] Sync verified: 706/706 proxies migrated successfully
- [x] `docs/plans/2026-05-11-003-fix-sync-config-path-errors-plan.md` created

### Config File Fixes
- [x] `bin/config.json` `log.file_level` changed from `"ERROR"` to `"DEBUG"`
- [x] `bin/config.json` `log.network_failures` changed from `false` to `true`

### Documentation
- [x] AGENTS.md — "核心开发规则" section added
- [x] `docs/project-knowledge.md` — "开发流程规则" section added (moved from `memory/project_knowledge.md`)
- [x] DEV-PROCESS.md — created with workflow + template + status definitions

## Execution History (post-tracker-creation)

### Recently Completed Plans

| # | Plan | Description | Status |
|---|------|-------------|--------|
| 1 | 13-001 | Sub-update log level INFO→REPORT | ✅ completed |
| 3 | 14-001 | Dedup: filter invalid proxies (REALITY missing key/sni, dirty Network) | ✅ completed |
| 2 | 13-002 | Profile progress/inserted log level INFO→REPORT | ✅ completed |
| 3 | 30 (P0) | Fix silent sqlite3_exec + finalize audit | ✅ completed |
| 4 | 31 (P1) | Extract sqlite3_open helper (U1 done, U2 kept as-is) | ✅ completed |
| 5 | 32 (P3) | Extract logInfo/logError helper functions | ✅ completed |
| 6 | 33 (P4) | Extract sqlite::exec helper + Transaction RAII guard | ❌ cancelled |
| 7 | 14-001 | 在去重功能中去除无效代理 | ✅ completed |
| 8 | 34 (P5) | Fix LogLevel ordering REPORT below ERR | ✅ completed |
| 9 | conse ui-fixes **consolidated-ui-fixes** | Fix single proxy test delay refresh + event flow | ✅ completed |
| 10 | DLL-fix **DLL Issue** | Fix wxmsw32ud_aui_gcc_custom.dll missing (Build type mismatch) | ✅ completed |
| 11 | DLL-fix **libpng16d/libtiffd** | Fix libpng16d.dll and libtiffd.dll missing for Debug build | ✅ completed |
| 12 | **Single proxy test report** | Technical report: TestSubId→IndexId, Delay refresh, event flow | ✅ completed |
| 13 | **UI enhancements plan** | Column sorting + Find individual proxy + Subscription linkage | ✅ completed |
| 28 | **Resizable splitter** | wxSplitterWindow for subscription/proxy panel resize | ✅ completed |
| 14 | **UI close hang fix** | Fix AppController/XrayInstance destructor race + process handle bugs | ✅ completed |
| 15 | **Cancel support for update** | Add runtime cancel support for subscription update (SubitemUpdaterV2 + MainFrame dynamic cancel button) | ✅ completed |
| 16 | **ProxyFinder cancel propagation** | Wire externalCancel_ to ProxyFinder in SubitemUpdaterV2::getProxyPorts() | ✅ completed |

### In Progress Plans

| # | Plan | Description | Status |
|---|------|-------------|--------|

### Recently Completed Plans

| # | Plan | Description | Status |
|---|------|-------------|--------|
| 1 | Five-Phase | Responsibility Decomposition (Phase 0-5 completed) | ✅ completed |
| — | Phase 4 | ProxyBatchTester components: ProxyBatchQuery, XrayWorkerPool, ProxyTestCounters, ProxyTestResultSink | ✅ completed |
| — | Phase 5 | AppController services: DatabaseConnectionService, ConfigService, SubscriptionService, ProxyListService, ProxyTestService, ShareLinkExportService, DatabaseMaintenanceService, AutoTaskService, UiOperationRunner | ✅ completed |
| 17 | 2026-06-02 | Subscription panel right-click delete functionality | ✅ completed |
| — | 14-002 | UI 图形界面实现 — 基于 wxWidgets (见下方 UI Implementation 详表) | ✅ completed |
| — | Phase 0 | Characterization tests for 5 decomposition targets (115 tests) | ✅ completed |
| 18 | 2026-06-02 | Subscription panel column sorting (Name, Proxies, Update) | ✅ completed |
| 19 | 2026-06-02 | ProxyListPanel Row# column sorts by IndexId instead of row number | ✅ completed |
| 20 | 2026-06-02 | SubscriptionPanel missing detectIdOffset after sort clear | ✅ completed |
| 21 | 2026-06-02 | PortManager clearPorts() for port leak fix | ✅ completed |
| 22 | 2026-06-03 | Subscription timeout config - curl CONNECTTIMEOUT + config.json | ✅ completed |
| 23 | 2026-06-03 | Network field improvements - splithttp→xhttp mapping (frontend parse) | ✅ completed |
| 24 | 2026-06-03 | UI improvements - dialog centering + copyable text | ✅ completed |
| 25 | 2026-06-03 | Proxy context menu disabled during operations | ✅ completed |
| 26 | 2026-06-03 | Config dialog improvements - block DB switch during ops | ✅ completed |
| 27 | 2026-06-04 | SubitemUpdaterV2 hardcoded `"bin/config"` path fallback fix | ✅ completed |
| 32 | 2026-06-11 | Zero-proxy run must call printSummary/stopAll on early return; cleared redundant guard in worker entry | ✅ completed |
| 33 | 2026-06-11 | ProxyListPanel refreshResults freeze — drop per-row ValueChanged storm, use listCtrl_->Refresh() | ✅ completed |
| 34 | 2026-06-11 | ConfigGenerator invalid-network fallback — defaults to tcp instead of skipping; added splithttp→xhttp mapping | ✅ completed |
| 35 | 2026-06-11 | **Stability Hardening** | CMake ASAN/UBSan, MiniDump, cppcheck, gcov — 调试基础设施加固 | ✅ completed |
| 38 | 2026-08-03 | **代码审计与优化 Phase 1（T1.2/T1.3）** — HPACK 解码 + grpc-status trailer 解析 + protobuf 编码修正（transportConfig.protocol_name 字段号 1、security_type TLS=2/REALITY=3 varint）| ✅ completed |
| 29 | 2026-06-11 | ProxyListPanel refreshResults(): replace per-row ValueChanged storm with listCtrl_->Refresh() to prevent UI freeze on large proxy sets | ✅ completed |
| 30 | 2026-06-11 | ConfigGenerator::loadProfiles(): invalid network defaults to tcp instead of skipping; added splithttp→xhttp mapping as fallback after frontend conversion | ✅ completed |
| 31 | 2026-06-09 | Resizable horizontal splitter for subscription/proxy panels | ✅ completed |
| 37 | 2026-06-17 | **Code Refactoring Phase 1** — Cleaned dead code files, removed redundant curl include, merged proxy type string mapping into ProxyTypeStrings.h | ✅ completed |
---

### `.kilo/plans/` 迁移条目

> 来源: `.kilo/plans/` → `docs/plans/` 合并迁移 (已格式化为规范命名)
> 日期: 2026-05-19

#### 已完成

| # | Plan File | Original Source | Description | Status |
|---|-----------|----------------|-------------|--------|
| 1 | `2026-05-14-003-proxy-validation-tool-architecture.md` | `1776914861549-gentle-panda.md` | 项目整体架构设计 + 构建系统 + 阶段的实现顺序 | 📝 参考文档 |
| 2 | `2026-05-18-002-fix-empty-test-results.md` | `1778814717912-tidy-meadow.md` | 修复单代理测试时 TestPanel 结果列表为空 | ✅ completed |
| 3 | `2026-04-23-001-fix-sql-delay-filter.md` | `1776415141516-jolly-mountain.md` | main.cpp SQL Delay>0 条件修正 + ShareLink 导出修复（2026-04-23） | ✅ completed |
| 4 | `2026-05-19-consolidated-ui-fixes.md` | 合并计划 | 单代理测试 Delay 列刷新 + 事件流程修复 | ✅ completed |

#### 报告归档 (docs/reports/ )

| # | Report File | Original Source | Description | Status |
|---|-------------|----------------|-------------|--------|
| 1 | `docs/reports/2026-04-23-sharelink-export-repair-report.md` | `1776931315746-sunny-rocket.md` | ShareLink 导出分享链接修复完成报告（5项修复已验证） | ✅ completed |

#### 草稿进行中

| # | Plan File | Original Source | Description | Status |
|---|-----------|----------------|-------------|--------|
| ~~1-5~~ | ~~See below - merged into consolidated plan~~ | ~~N/A~~ | ~~See 2026-05-19-consolidated-ui-fixes.md~~ | ✅ **MERGED** |

### UI Implementation — Phase Tracking

| Phase | U# | Description | Status |
|-------|----|-------------|--------|
| Phase 1 | U1 | CMakeLists.txt wxWidgets 集成 | ✅ |
| Phase 1 | U2 | 自定义事件系统 (Events.h) | ✅ |
| Phase 1 | U3 | AppController 控制器层 | ✅ |
| Phase 2 | U8 | 日志面板 (LogPanel) | ✅ |
| Phase 2 | U9 | 配置对话框 (ConfigDialog) | ✅ |
| Phase 2 | U10 | 系统托盘 (TrayIcon) | ✅ |
| Phase 4 | U11 | main.cpp `-ui` 入口修改 | ✅ |
| Phase 3 | U5 | 订阅面板 (SubscriptionPanel) | ✅ |
| Phase 3 | U6 | 代理列表面板 (ProxyListPanel) | ✅ |
| Phase 3 | U7 | 测试面板 (TestPanel) | ✅ |
| Phase 4 | U4 | 主窗口布局 (MainFrame) — 依赖前述所有面板 | ✅ |
| Phase 4 | INT | 集成测试 + 构建验证 | ✅ |
| **已完成** | sort/find/link | 列排序 + 查找单个代理 + 订阅联动 | ✅ completed |

**状态标记:** ✅ completed ｜ 🔄 in_progress ｜ 📝 draft ｜ ❌ blocked ｜ ❌ terminated

---

### `.kilo/plans/` 归档清理记录

> 原始文件迁移完成后，`.kilo/plans/` 目录已删除 (2026-05-19)。
> 以下计划原存于 `.kilo/plans/`，均已按规范命名迁移至 `docs/` 相应位置。
> `.kilo/plans/` 源文件保留情况: `architecture.md` / `context.md` 已确认无需迁移(同内容在 `docs/`)，其余 8 份均为临时 plan ID 命名，已完全迁移，`docs/` 即为唯一权威存放地。

| 原计划文件 (MD5) | 规范命名 | 目标位置 | 状态 |
|-----------------|---------|---------|------|
| `1776215451920-nimble-wolf.md` | `2026-05-14-004-gh-mcp-list-top-repos.md` | `docs/plans/` | 📝 draft |
| `1776415141516-jolly-mountain.md` | `2026-04-23-001-fix-sql-delay-filter.md` | `docs/plans/` | ✅ completed |
| `1776931315746-sunny-rocket.md` | `2026-04-23-sharelink-export-repair-report.md` | `docs/reports/` | ✅ completed |
| `1776914861549-gentle-panda.md` | `2026-05-14-003-proxy-validation-tool-architecture.md` | `docs/plans/` | ✅ completed |
| `1778814717912-tidy-meadow.md` | `2026-05-18-002-fix-empty-test-results.md` | `docs/plans/` | ✅ completed |
| `1779070922736-shiny-comet.md` + `eager-moon.md` + `silent-harbor.md` | `2026-05-19-consolidated-ui-fixes.md` | `docs/plans/` | ✅ **MERGED & COMPLETED** |
| `architecture.md` | 同名 | `docs/architecture.md` | 同内容已存在，不迁移 |
| `context.md` | 同名 | `docs/context.md` | 同内容已存在，不迁移 |





