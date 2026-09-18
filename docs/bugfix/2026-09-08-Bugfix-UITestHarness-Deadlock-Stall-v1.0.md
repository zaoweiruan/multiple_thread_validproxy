# 2026-09-08 Bugfix: UI 自动化套件死锁与 UIA 卡顿 — v1.0

## 问题现象

`ctest -R UI_` 七项 UI 自动化测试长期无法全绿：

- 早期：5/7 超时（120s ctest timeout），仅 PORTCLOSE / EXITASSERT 通过；子进程起动后 ~1s 收到 WM_CLOSE 干净退出；UITests.exe 主线程 CPU 自旋挂死。
- 中期（killProcessTree 修复后）：5/7 → 6/7，剩余为间歇性 flaky（同一条目不同轮次互斥地过/挂），失败断言集中在 UIA 元素解析。

## 根因分析（gdb attach 主线程栈定位）

### 1. `killProcessTree` 祖先链死循环（tests/ui/framework/Application.cpp）

挂死栈：`killProcessTree → std::unordered_map::find → _M_find_before_node`（纯 CPU 自旋）。

```cpp
while (cur != 0) {
    if (cur == rootPid) { isDesc = true; break; }
    auto it = parentOf.find(cur);
    if (it == parentOf.end()) break;
    cur = it->second;   // ← Windows 父进程链偶发成环：无环检测 → 死循环
}
```

进程表积累（历次被强杀测试残留孤儿 xray/validproxy）后出现父子环（服务宿主重启 / PID 复用），遍历永不终止 → fixture dtor 卡死 → UITests 永不退出 → ctest 超时。纯 Win32 的 PORTCLOSE 从不调用 killProcessTree，故一直通过。

**修复**：祖先遍历加 64 跳上限（真实链深 <10），环被界定后按"非后代"处理。

### 2. 悬浮窗双击"最小化→最大化"路径失效（src/ui/StandaloneFloatingWidget.cpp）

`toggleMainFrameMaximize`：最大化→`Iconize`；其余→`Maximize(true)`。wxMSW 下对**已最小化**窗口 `Maximize(true)` 是空操作（ShowWindow(SW_MAXIMIZE) 被忽略），原注释"会取消最小化并最大化"不成立 → 双击测试 `CHECK(afterSecond == wasMaxed)` 失败。

**修复**：`if (frame->IsIconized()) frame->Restore();` 后再 `Maximize(true)`。

### 3. Desktop/子树级 UIA `FindFirst` 间歇性 stall → 元素找不到

对话框（独立代理池）与按钮（清空日志窗口）解析走桌面/子树树遍历；任何窗口 WM_GETOBJECT 应答慢都会使单次 `FindFirst` 烧掉 UIA 内部超时（秒级），轮询窗口被吞 → `dialog.valid()/btn.valid()` false。

**修复**：改用"纯 Win32 定位 HWND + `UiElement::fromHwnd`（ElementFromHandle 直挂，无树遍历）"：

- `TestStandaloneProxyPool.cpp`：新增 `waitForPoolDialog`（EnumWindows 按 PID+标题 → fromHwnd），替换 3 处 `findNamed(desk, PoolDialogName, …)`。
- `TestClear.cpp`：新增 `findChildByText`（EnumChildWindows + GetWindowText）→ fromHwnd，替换子树 findBy。
- `TestSearch.cpp`：保留子树 findBy，超时 5000/3000 → 15000/8000（给重试更多机会）。

### 4. EXITASSERT 30s 上限对慢启动过紧（tests/ui/TestExitAssert.cpp)

失败轮子进程日志显示 WM_CLOSE 在消息队列等待 **30s 整**才被处理：启动期 DB 大表（53k 代理）面板异步加载在主线程执行，磁盘/杀软抖动时可阻塞消息泵 >30s。测试语义是"退出路径无断言"，与快慢无关。

**修复**：`waitFor(30000) → waitFor(90000)`（ctest 单测超时 120s 仍满足）。

## 变更文件

| 文件 | 变更 |
| --- | --- |
| `tests/ui/framework/Application.cpp` | killProcessTree 祖先遍历 64 跳上限 |
| `src/ui/StandaloneFloatingWidget.cpp` | 最小化时先 Restore 再 Maximize |
| `tests/ui/TestStandaloneProxyPool.cpp` | waitForPoolDialog（HWND 直挂）替换 desktop FindFirst |
| `tests/ui/TestClear.cpp` | findChildByText + fromHwnd 替换子树 findBy |
| `tests/ui/TestSearch.cpp` | findBy 超时 5000/3000 → 15000/8000 |

## 验证

- 构建：`cmake --build build --parallel 8` 0 error（UITests + validproxy）。
- 套件：`ctest --test-dir build -R "UI_"` 连续两轮 **7/7 全绿**（51.4s / 55.2s，rc=0）。
- 插桩清理：临时 stderr 里程碑（[TM]/[AL]）已移除；失败产物保存 listener 保留。

## 已知后续项

- `xrayCanStart()` 探针（E:\v2rayN-windows-64\bin\xray\xray.exe）偶发留下孤儿进程（TerminateProcess 后存活并重父化）；暂不影响套件全绿，后续可改为按进程树终止。