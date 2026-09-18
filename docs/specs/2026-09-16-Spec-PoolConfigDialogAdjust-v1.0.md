# Spec: 代理池配置窗口调整（PoolConfigDialogAdjust）

**日期**: 2026-09-16  
**类型**: Spec（功能调整）  
**模块**: 独立代理池 / ConfigDialog  
**版本**: v1.0

---

## 1. 背景

评估结论（2026-09-16）：

1. `standalone_pool.observatory.destination`（观测探测地址兜底）**运行时恒被 `test.url` 覆盖**：
   - `AppController::startProxyPool` L1869 `poolCfg.probeUrl = config_.test_url`
   - `StandaloneProxyPool::evaluateAll` L280 `testUrl = probeUrl.empty() ? destination : probeUrl`
   - `test_url` 在 ConfigDialog 已暴露（"测试 URL"）且 config.json 恒有值 → destination 永不生效
   - 处理：**从 UI 移除**（后端字段 + fallback 逻辑保留，向后兼容；test_url 为空的极端场景仍可用）

2. `evaluate.probeWorkers`（常驻探针 worker 数）**真实运行时消费**（ProxyProbePool），但**未暴露在 UI**——遗漏项。
   - 探针池 enable 判断条件 = `probeWorkers > 0`（`ProxyProbePool::start()` L83 `workerCount <= 0 → disabled`）
   - **当前 parser 只接受 `v > 0`**，无法通过配置 0 禁用探针池
   - 处理：**新增暴露** + **parser 允许 0**（0 = 禁用探针池）

3. **发现隐患**：`ConfigJsonSerializer` 的 evaluate 块**未序列化 probeWorkers** → 解析后写回 config.json 会丢失（当前只进不出）。需补序列化。

## 2. 目标

| 项 | 改动 |
|----|------|
| UI 移除 | `pool_obs_destination`（观测探测地址兜底）——Append/load/save/validate 4 处 |
| UI 新增 | `pool_eval_probe_workers`（探针 worker 数，0=禁用，1-N=worker）——Append/load/save/validate |
| Parser | `probeWorkers` 接受 `v >= 0`（原 `v > 0`），上限 64 保护 |
| 序列化 | `ConfigJsonSerializer` evaluate 块补 `probeWorkers` 写回 |

## 3. 改动清单

### 3.1 `src/ui/ConfigDialog.cpp` — 独立代理池块

**Append 区**（L193-219 附近）：
- 删除：
  ```cpp
  propGrid_->Append(new wxStringProperty(L"观测探测地址(兜底)", "pool_obs_destination",
                      wxString(cfg.standalone_pool.observatory.destination)));
  ```
- 新增（放在 `pool_eval_auto_optimize` 后）：
  ```cpp
  propGrid_->Append(new wxIntProperty(L"探针 worker 数(0=禁用)", "pool_eval_probe_workers",
                      cfg.standalone_pool.evaluate.probeWorkers));
  ```

**loadConfig**：删 `pool_obs_destination` 行；加 `pool_eval_probe_workers` 行。

**saveConfig**：删 `observatory.destination` 读回行；加 `evaluate.probeWorkers` 读回行（0-64 钳制）。

**validateConfig**：删除 destination 校验（L608-617）；
新增：
```cpp
if (editedConfig_.standalone_pool.evaluate.probeWorkers < 0 ||
    editedConfig_.standalone_pool.evaluate.probeWorkers > 64) {
    wxMessageBox("探针 worker 数必须在 0 到 64 之间（0=禁用探针池）", "验证错误", wxOK | wxICON_ERROR);
    return false;
}
```

### 3.2 `include/config/sections/StandalonePoolConfigParser.h`

```cpp
if (ev.contains("probeWorkers") && ev.at("probeWorkers").is_int64()) {
    int v = static_cast<int>(ev.at("probeWorkers").as_int64());
    if (v >= 0 && v <= 64) config.standalone_pool.evaluate.probeWorkers = v;  // 0=禁用探针池
}
```

### 3.3 `src/config/ConfigJsonSerializer.cpp`

```cpp
evObj["probeWorkers"] = config.standalone_pool.evaluate.probeWorkers;
```

### 3.4 不变项

- 后端 `observatory.destination` 字段 / 序列化 / fallback 逻辑保留（test_url 为空的极端兜底；旧 config.json 值不丢）
- `StandaloneProxyPool` / `ProxyProbePool` / `ProxyHealthEvaluator` 运行时逻辑零改动（`workerCount <= 0` disabled 分支已存在）
- 其余 11 项 UI 属性不变

## 4. 行为对照

| 场景 | 改动前 | 改动后 |
|------|--------|--------|
| 配置窗口独立代理池项数 | 12 | 12（-destination +probeWorkers） |
| 配置 `probeWorkers = 0` | parser 拒绝（保持默认 2） | 接受 → 探针池禁用（ProxyProbePool::start L83 disabled） |
| 保存配置 | probeWorkers 不写回（丢默认 2） | 写回（用户值持久化） |
| 旧 config.json 含 destination | 读入（UI 显示） | 读入但不显示、保存时透传保留 |

## 5. 验证

- 构建 0 error
- parser 单测：probeWorkers=0 接受 / 负数拒绝 / 65 拒绝 / 缺省默认 2
- 序列化往返：设置 probeWorkers=4 → 保存 → 重载 = 4