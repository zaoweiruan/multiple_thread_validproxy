```text
Known limitation:
Xray instances are not automatically restarted after runtime crashes.
```

**Update (2026-08-07):** Fix 1 below has been **implemented**. `isRunning()` now queries actual process state via `GetExitCodeProcess`, and syncs `running_` to `false` when the process is detected as dead. Fixes 2 (watchdog) and 3 (Manager-level restart) remain deferred as future enhancements.

---

## 当前行为

可以把现有生命周期分成两个阶段：

### 1. 启动阶段

`XrayInstance::start()` 创建进程后，会在约 5 秒内检查进程是否仍然存活。如果 Xray 在这段时间内立即退出，启动流程会失败并清理资源。

同时，`pollApiPortReady()` 会等待 gRPC API 端口开放；这次修复让它遇到 `WSAECONNREFUSED` 时继续轮询，而不是第一次失败就退出。 [ppl-ai-file-upload.s3.amazonaws](https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/attachments/82110049/9348fd54-560e-4109-b36e-f02de401e28f/2026-08-07-Bug-XRAY_ERROR.md?AWSAccessKeyId=ASIA2F3EMEYEQHAWCOER&Signature=RO%2FRcV9%2BomtCDAFduUseFHmubs8%3D&x-amz-security-token=IQoJb3JpZ2luX2VjEIr%2F%2F%2F%2F%2F%2F%2F%2F%2F%2FwEaCXVzLWVhc3QtMSJIMEYCIQDpLC6I9RWdf81W%2BNCZlGZ26z74L%2B0nOYifSgq3KSPfNgIhAPxZIaeFonA3dqjgMLilotdv0mh09Wc2o6aJ%2BcIvcdq8KvMECFMQARoMNjk5NzUzMzA5NzA1IgxZpwcrA5YxlWd3Qwgq0ATgnZrDy1b19m39KRRF1L7s8G6K79fdE8lPEWtKyDKziNWVzYBA8S65oPtkyQBTErI73NdLL0p30zlmdSuxzuZS6uCZqgq1TFqswJG4OtmHXX1AIkmEK1TjULHmhbzfPe1G89vQpdDzzINDRjfiKj4BuIQXOOQOoL0i%2FJwXGxhzF90mAA9PcrO4lu%2F5IdYGA9%2BmgidCIPT%2BtEHx0d688%2BaTW%2FOQbAHwwP7GMQD87V6JNoi%2BBZ%2BPLmM6US7nyVzB7xTNBji0755ibGE4RqLk13N6NYZsPACf5rgTx9H1lXgNsiJrv4Ug6JDQ6bVY0FsxG%2FNVnd1YHLmEd2MnI%2F4TiDMHKyG%2FdcUYpnZ18aaoYF368LMc4u3OXqAJmdTL7Au3mPZBA6iiThZ2Pu3290edKywJkvY7b607fJh792nxnDZZWfk4GLhMrRZo7E9iIwe%2BukWsCvwTKSIsd%2FGum8G7vziCrncS0ZdWSKpVM3CIx5tGq6kkffr4siXbh10jyIITZ33RWwncqpN20dDmd%2FtaWSx8i4Y%2FDR5jAVNoNUXbaM%2BByW2XxZBsFm%2BhRwgYDzjCLd5JajTe9YyMSepbFqo9svncvzGuIOU9rOLq9uK9rKTlUTiu7YO3vKTqpc6dS8DWWHVDtCDQ5RnUwms6bKdeoRfsquSoCpwmfK2MBbXEPS7ybe160jyVMsRV9A6GBQzwsmL8dVEnb8opYkwyxTB%2BcbDF6VctTh4Pq1jV6yG3Yaja0AHmvIh0qvqYAV27VEOdUgnQ7TIi0QX%2B69wWP1hkfiFrMKrJ1tMGOpcB2cKDt41I6pgjogWa%2Bmdvl3kwgRY3XihUOFDsCl%2F8HUlhfdzEzllNCEnd12CGjvhpzHxxzh45tgKm32uQuAuako%2B%2FOSCjHRdKTn6l%2Fwv5%2BHMkJv9cFoxRYFkiXIs%2FlQtGNJHgn%2F5pLl5RcyDyK8cGs09Uk%2BKgj6yWmJbopN1KPh9oSkrmitEa27ikySkKwrMbyTsSx4iZew%3D%3D&Expires=1786098301)

### 2. 运行阶段

一旦启动完成并设置：

```cpp
running_ = true;
```

当前代码没有 watchdog 或后台监控线程。因此，如果 Xray 后续崩溃：

```text
Xray 进程退出
    ↓
running_ 仍然是 true
    ↓
isRunning() 仍然返回 true
    ↓
实例继续被分配给 worker
    ↓
gRPC connect() 返回 WSA10061
```

也就是说，`running_` 目前只是一个“曾经成功启动过”的状态标记，不是进程当前真实状态。

## 重试为什么不能解决崩溃

worker 的重试只能解决：

```text
Xray 仍然活着，但 API 端口稍晚才开放
```

例如：

```text
第一次连接：WSAECONNREFUSED
等待 200ms
第二次连接：成功
```

但如果 Xray 已经退出：

```text
第一次连接：WSAECONNREFUSED
等待 200ms
第二次连接：WSAECONNREFUSED
等待 400ms
第三次连接：WSAECONNREFUSED
最终 XRAY_ERROR
```

因此，延长重试时间只是提高了启动竞态的容错能力，并不会产生重启行为。 [ppl-ai-file-upload.s3.amazonaws](https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/attachments/82110049/9348fd54-560e-4109-b36e-f02de401e28f/2026-08-07-Bug-XRAY_ERROR.md?AWSAccessKeyId=ASIA2F3EMEYEQHAWCOER&Signature=RO%2FRcV9%2BomtCDAFduUseFHmubs8%3D&x-amz-security-token=IQoJb3JpZ2luX2VjEIr%2F%2F%2F%2F%2F%2F%2F%2F%2F%2FwEaCXVzLWVhc3QtMSJIMEYCIQDpLC6I9RWdf81W%2BNCZlGZ26z74L%2B0nOYifSgq3KSPfNgIhAPxZIaeFonA3dqjgMLilotdv0mh09Wc2o6aJ%2BcIvcdq8KvMECFMQARoMNjk5NzUzMzA5NzA1IgxZpwcrA5YxlWd3Qwgq0ATgnZrDy1b19m39KRRF1L7s8G6K79fdE8lPEWtKyDKziNWVzYBA8S65oPtkyQBTErI73NdLL0p30zlmdSuxzuZS6uCZqgq1TFqswJG4OtmHXX1AIkmEK1TjULHmhbzfPe1G89vQpdDzzINDRjfiKj4BuIQXOOQOoL0i%2FJwXGxhzF90mAA9PcrO4lu%2F5IdYGA9%2BmgidCIPT%2BtEHx0d688%2BaTW%2FOQbAHwwP7GMQD87V6JNoi%2BBZ%2BPLmM6US7nyVzB7xTNBji0755ibGE4RqLk13N6NYZsPACf5rgTx9H1lXgNsiJrv4Ug6JDQ6bVY0FsxG%2FNVnd1YHLmEd2MnI%2F4TiDMHKyG%2FdcUYpnZ18aaoYF368LMc4u3OXqAJmdTL7Au3mPZBA6iiThZ2Pu3290edKywJkvY7b607fJh792nxnDZZWfk4GLhMrRZo7E9iIwe%2BukWsCvwTKSIsd%2FGum8G7vziCrncS0ZdWSKpVM3CIx5tGq6kkffr4siXbh10jyIITZ33RWwncqpN20dDmd%2FtaWSx8i4Y%2FDR5jAVNoNUXbaM%2BByW2XxZBsFm%2BhRwgYDzjCLd5JajTe9YyMSepbFqo9svncvzGuIOU9rOLq9uK9rKTlUTiu7YO3vKTqpc6dS8DWWHVDtCDQ5RnUwms6bKdeoRfsquSoCpwmfK2MBbXEPS7ybe160jyVMsRV9A6GBQzwsmL8dVEnb8opYkwyxTB%2BcbDF6VctTh4Pq1jV6yG3Yaja0AHmvIh0qvqYAV27VEOdUgnQ7TIi0QX%2B69wWP1hkfiFrMKrJ1tMGOpcB2cKDt41I6pgjogWa%2Bmdvl3kwgRY3XihUOFDsCl%2F8HUlhfdzEzllNCEnd12CGjvhpzHxxzh45tgKm32uQuAuako%2B%2FOSCjHRdKTn6l%2Fwv5%2BHMkJv9cFoxRYFkiXIs%2FlQtGNJHgn%2F5pLl5RcyDyK8cGs09Uk%2BKgj6yWmJbopN1KPh9oSkrmitEa27ikySkKwrMbyTsSx4iZew%3D%3D&Expires=1786098301)

## 需要注意的一个细节

即使以后改成调用 `GetExitCodeProcess()`，也最好不要让 `isRunning()` 只检查：

```cpp
GetExitCodeProcess(...) == STILL_ACTIVE
```

更稳妥的判断应当包括：

- 进程句柄是否有效。
- `GetExitCodeProcess()` 是否调用成功。
- 退出码是否表示进程仍然运行。
- Xray 的 gRPC API 端口是否仍然可访问。

另外，Windows 的 `STILL_ACTIVE` 数值是 `259`，理论上进程也可以用 `259` 作为正常退出码，所以它更适合作为运行状态判断，而不应被当作普通业务退出码使用。

## 自动恢复应该怎么设计

后续可以增加三层机制：

### 1. 修正 `isRunning()`

让它实时查询进程状态，而不是只读取旧的原子变量：

```cpp
bool XrayInstance::isRunning() const {
    if (!processHandle_) {
        return false;
    }

    DWORD exitCode = 0;
    if (!GetExitCodeProcess(processHandle_.get(), &exitCode)) {
        return false;
    }

    return exitCode == STILL_ACTIVE;
}
```

同时，在发现进程退出后同步：

```cpp
running_.store(false);
```

### 2. 增加 watchdog

在 `XrayInstance` 中增加监控线程，定期检查：

```text
进程是否退出
API 端口是否仍然可用
```

发现异常后：

```text
running_ = false
通知 XrayManager
```

watchdog 最好只负责检测和通知，不要直接执行完整重启，避免和 `stop()`、析构函数、批量测试线程发生锁竞争。

### 3. 由 `XrayManager` 负责重启

Manager 可以执行：

```text
检测实例死亡
    ↓
停止并清理旧实例
    ↓
释放旧端口
    ↓
重新分配端口
    ↓
启动新 Xray
    ↓
等待 API ready
    ↓
重新加入 instances_
```

还需要设置：

- 最大重启次数。
- 重启冷却时间。
- 连续失败熔断。
- 重启日志和失败原因。
- 防止 worker 同时使用正在重启的实例。

## 结论

当前修复的范围可以准确表述为：

> 修复 Xray 启动时 API 端口尚未 ready 导致的间歇性 `WSA10061`，但不处理运行期间 Xray 进程崩溃后的自动恢复。

因此，文档最好明确写出：


否则读者可能会误以为 `pollApiPortReady()` 和 worker retry 已经覆盖了所有 `WSA10061` 场景。

---

## 2026-08-10 更新（Fix 2 已实现）

### 背景

上文中"运行期间 Xray 进程崩溃后不自动恢复"的结论已被本次修复推翻。现在：连续两次 gRPC connect 失败（counter==2）会触发 `connectFailureHook`，`ProxyBatchTester` worker 收到通知后停止重试，并调用 `XrayManager::evaluateInstanceHealth(apiPort)` 完成"判断挂死/崩溃 → 释放资源 → 用原配置拉起"的完整生命周期闭环。

### 修复内容

#### 1. gRPC 返回错误日志级别提升到 ERROR

涉及文件：`src/XrayApi.cpp`、`src/ProxyBatchTester.cpp`

以下 gRPC/API 失败路径的日志级别从 `DEBUG` 提升为 `ERR`：

| 位置 | 原级别 | 新级别 |
| --- | --- | --- |
| `XrayApi::addOutbound` FAILED（subprocess `ado` 路径，exitCode + lastError 两行） | DEBUG | ERR |
| `XrayApi::removeOutbound` FAILED | DEBUG | ERR |
| `XrayApi::removeOutboundDirect` FAILED | DEBUG | ERR |
| `XrayApi::addOutboundDirect` FAILED（grpcSendReceive 路径） | DEBUG | ERR |
| `XrayApi::listOutboundsDirect` FAILED | DEBUG | ERR |
| `ProxyBatchTester` worker `[Worker-N] 注入xray outbound 错误` | DEBUG | ERR |
| `ProxyBatchTester` worker `[Worker-N] XRAY_ERROR - <indexid> ...` | DEBUG | ERR |

刻意保留 `DEBUG` 的：`Xray output:` 原始输出转储（补充信息，避免污染 ERR 通道）；各 SUCCESS 日志。

#### 2. 挂死/崩溃后的资源释放与自动拉起

涉及文件：`src/XrayManager.cpp`、`include/XrayManager.h`

`evaluateInstanceHealth(int apiPort)` 完全重写为两阶段设计：

```
阶段 1（短锁 instancesMutex_）：
    isRunning() == false        → ERR 日志 "is DEAD — releasing resources and relaunching with original config"
    isRunning() == true（挂死） → WARN 日志 "process alive but API port unresponsive (possible hang) — releasing resources and relaunching with original config"
    捕获 socksPort；stale = std::move(*it)；instances_.erase(it)
    （stale->stop() + freePort(socksPort) + freePort(apiPort) 在锁外执行，避免持锁调 start()）

阶段 2（锁外）：
    replacement = make_unique<XrayInstance>(xrayPath_, socksPort, apiPort, configDir_)
    replacement->start() && pollApiPortReady(apiPort) → WARN "relaunched instance with original config"；重入锁 push_back；return true
    否则 → ERR "relaunch FAILED ... ports freed; instance will be re-created on next start()"；replacement->stop() + freePort 双端口；return true
```

关键设计点：

- **相同 socksPort → 相同配置文件**（`configDir_/xray_config_<socksPort>.json` 由 `start()` 的 `createConfigFile()` 重新生成），因此"用原配置拉起"是天然成立的。
- **挂死与崩溃统一处理**：只要 `isRunning()` 为真但 API 端口在 hook 触发时无响应（说明 gRPC 层已判定失败），同样视为需要恢复的实例。
- **锁安全**：`instancesMutex_` 仅在查找/erase/commit 时短暂持有；`start()`（含 ~2s 轮询）与 `stop()` 全部在锁外执行；`stale` 析构再次调用 `stop()` 幂等安全。
- **失败兜底**：relaunch 失败时双端口已释放，后续 `start()` 流程会重新创建实例，不遗留僵尸进程与端口占用。
- 钩子仅在连续失败计数达到 2 时触发一次（成功即清零），避免对慢启动但健康实例的重复评估。

### 验证

- 构建：`cmake --build build --parallel 8` → 302/302 全部链接成功（含 `tests/test_xray_api_direct.exe`、`bin/validproxy.exe`）。
- 测试：`ctest --test-dir build` → 21/21 通过（46.73s），`XrayApiDirectTest` 10.26s（94 用例，含 `ConsecutiveConnectFailuresTriggerHealthHook`、`GrpcConnectSuccessResetsFailureCounter`）。
- 说明：自动拉起路径依赖真实 `xray.exe` 进程，不做单元测试；hook/计数行为已由上述 2 个单元测试覆盖。

---

## 2026-08-10 更新（Fix 3/4 已实现）

### 背景

生产日志 `bin/worker/log/ui_20260810_100203.log` 验证：3 个实例死亡均在 ~6s 内被检测并自动拉起（10:13:05→10:13:11、10:14:25→10:14:31、10:16:21→10:16:27），拉起后继续接受测试（10:15:33 / 10:16:17 的真实 status=2 校验错误证明 relaunched 实例存活并响应 gRPC）。但发现触发死亡的 worker 在该实例被拉起的**同一秒**输出空错误消息的 XRAY_ERROR（如 `注入xray outbound 错误: ` 空串）。

### Fix 3 — 空错误消息根因与修复（`src/ProxyBatchTester.cpp`）

**根因链**：
1. worker 循环内 `addOutboundDirect` 失败 → `lastError_` 已写入失败原因；
2. 下一次迭代的 `removeOutboundDirect(tag)` 恰好命中**刚拉起成功的同端口实例** → grpcConnect 成功 → 走 `removeOutboundDirect` 成功路径 `lastError_.clear()`（`XrayApi.cpp` removeOutboundDirect 成功分支）→ 失败原因被清空；
3. 循环因 `instanceDead==true` 退出，最终日志 `getLastError()` 为空串。

**修复**：失败后立即保存 `injectError = xrayApi.getLastError()`（在 in-loop `removeOutboundDirect` **之前**）；最终错误日志统一使用 `displayErr = injectError.empty() ? xrayApi.getLastError() : injectError`；transient 判断改为基于 `injectError`。12 个 transient 子串与指数退避 `200*(1<<(retryCount-1))` 语义不变。

### Fix 4 — 在途代理重拉后重试一次（`src/ProxyBatchTester.cpp`）

- hook lambda 新增捕获 `relaunchedOk`：`relaunchedOk.store(xrayManager_->evaluateInstanceHealth(apiPort))`（evaluateInstanceHealth 返回"是否找到并成功重拉"）。
- 重试循环后、最终失败判定前新增**单次**在途重试块：
  `if (!addSuccess && instanceDead.load() && relaunchedOk.load())` → WARN 日志 `instance relaunched (api=...) — retrying proxy <indexid> once`，执行 `removeOutboundDirect` + `addOutboundDirect` 一次；成功则 `addSuccess=true`，失败则刷新 `injectError`。不做循环、不加重试计数。
- 效果：触发实例死亡的这条在途代理不再被直接丢弃，而是获得一次在刚拉起实例上的完整注入 + 连通性测试机会。

### `evaluateInstanceHealth` 返回值语义变更（`src/XrayManager.cpp` / `include/XrayManager.h`）

- **返回 true**：apiPort 在池中找到 **且** 重拉成功（实例就绪、可接受测试）；
- **返回 false**：apiPort 未找到，**或** 重拉失败（端口已释放，待下次 `start()` 重建）。
- 唯一调用方为 ProxyBatchTester hook（`src/ProxyBatchTester.cpp:118`），无其它调用点。

### 验证

- 构建：`cmake --build build --parallel 8` → 302/302 全部链接成功。
- 测试：`ctest --test-dir build` → 21/21 通过（47.52s）。
- 说明：重试路径依赖真实 xray.exe 死亡/重拉时序，不做单元测试；逻辑由代码审查 + 生产日志时序佐证。