# RegionBatchResolver — 批量代理地区解析器

- **版本**: 1.0
- **日期**: 2026-07-08
- **状态**: Draft / 待实现
- **涉及模块**: RegionBatchResolver (新), AppController, ProxyListPanel, CMakeLists.txt

---

## 1. 目标

实现独立的多线程批量代理地区解析器，替代现有的静态 `RegionDetector::detect()` 逻辑。

## 2. 背景

现有地区检测通过 `utils::RegionDetector::detect(address, remarks)` 基于 geoip.dat/geosite.dat/域名后缀/关键词进行静态匹配，局限性大。需要改为通过代理实际访问 ipinfo.io 获取地域 JSON 中的 `country` 字段，准确性更高。

## 3. 设计约束

| 约束 | 说明 |
|------|------|
| 独立功能 | 不嵌入批量测试流程中，通过菜单/按钮单独触发 |
| 多线程 | 参照 `ProxyBatchTester` 的 worker thread 模式 |
| 每个 worker 需启动 Xray 实例 | 复现测试时的代理启动模式（SocksPort + ApiPort） |
| 已有 proxy test 数据 | 只查询 delay>0 的代理，无需重复连通性测试 |

## 4. 类设计: `RegionBatchResolver`

### 4.1 头文件 `include/RegionBatchResolver.h`

```cpp
class RegionBatchResolver {
public:
    RegionBatchResolver(sqlite3* db, const AppConfig& config);
    ~RegionBatchResolver();

    int run(const std::string& subId = "");
    void cancel();
    int getTotal() const;
    int getProcessed() const;
    int getSuccessCount() const;

private:
    struct ResolveTarget {
        std::string indexId;
        std::string address;
        std::string remarks;
    };

    void workerThreadFunc(int workerId, int socksPort, int apiPort);
    std::string fetchRegionFromIpInfo(int socksPort);
    std::string parseRegionFromJson(const std::string& jsonStr);
    void updateRegionBatch(const std::vector<std::pair<std::string, std::string>>& updates);
    std::vector<ResolveTarget> loadTargets(const std::string& subId);

    sqlite3* db_;
    const AppConfig& config_;
    XrayManager* xrayManager_;

    std::queue<int> queue_;
    std::vector<ResolveTarget> targets_;
    mutable std::mutex queueMutex_;
    std::atomic<int> processedCount_{0};
    std::atomic<int> successCount_{0};
    std::atomic<bool> cancelRequested_{false};

    // Batch DB update buffer
    std::vector<std::pair<std::string, std::string>> regionBuffer_;
    std::mutex bufferMutex_;
    static constexpr int BATCH_FLUSH_SIZE = 50;
};
```

### 4.2 主要流程

```
run(subId):
  1. loadTargets(subId)  → 查询 delay>0 且 region 为空的目标
  2. 若 targets_ 为空则提前返回
  3. xrayManager_ = XrayManager::getInstance(path, dir, workers)
  4. int actual = xrayManager_->start(numWorkers, startPort, apiPort)
  5. std::this_thread::sleep_for(2s)  // 等待 gRPC 端口就绪
  6. vector portPairs = xrayManager_->getPortPairs()
  7. 填充 queue_
  8. 启动 N 个 worker 线程, 每个绑定一对 socksPort/apiPort
  9. join 所有线程
  10. 刷新剩余 regionBuffer_ 到 DB
  11. xrayManager_->stopAll()

workerThreadFunc(workerId, socksPort, apiPort):
  - 创建 xray::XrayApi 实例
  - 循环从 queue_ 取出目标 index
  - 若目标被取消则跳出
  - 生成该代理的 Xray outbound config
  - removeOutbound(tag) + addOutbound(config, tag)
  - 短暂 sleep 等待注入
  - fetchRegionFromIpInfo(socksPort)  → 返回 JSON 字符串
  - parseRegionFromJson(jsonStr)     → 提取 "country" 字段
  - 若 region 非空: 写入 regionBuffer_, 达阈值则 flush
  - processedCount_++, 若成功则 successCount_++

fetchRegionFromIpInfo(socksPort):
  - curl_easy_init()
  - CURLOPT_PROXY = socks5://127.0.0.1:{socksPort}
  - CURLOPT_URL = https://ipinfo.io/json
  - CURLOPT_TIMEOUT = 15s
  - CURLOPT_SSL_VERIFYPEER = false
  - 执行并返回 body 字符串
  - 异常返回 ""

parseRegionFromJson(jsonStr):
  - boost::json::parse(jsonStr)
  - 提取 obj["country"] 转为 string
  - 异常或字段不存在返回 ""

loadTargets(subId):
  - SELECT p.IndexId, p.Address, p.Remarks
    FROM ProfileItem p
    INNER JOIN ProfileExItem e ON p.IndexId = e.IndexId
    WHERE CAST(e.delay AS INTEGER) > 0
      AND (p.Region IS NULL OR p.Region = '')
      [AND p.SubId = ?]  -- 可选 subId 过滤

updateRegionBatch(updates):
  - BEGIN TRANSACTION
  - 预编译 UPDATE ProfileItem SET Region = ? WHERE IndexId = ?
  - 循环 bind + step + reset
  - COMMIT / ROLLBACK
```

### 4.3 配置参数

- `xray_workers`: 最大并发 Xray 实例数 (来自 config_.xray_workers)
- `xray_start_port / xray_api_port`: SOCKS5/API 端口范围起点
- 每个 worker 绑定一个 socksPort (通过 ipinfo.io 请求地区)
- 单条请求超时: 15s

### 4.4 错误处理

- 网络请求失败 → 跳过该代理, 记录日志 WARN
- JSON 解析失败 → 跳过, 记录日志
- DB 更新异常 → 回滚当前批次, 记录 ERR
- 取消 → 各 worker 检查 `cancelRequested_` 标志后停止

## 5. UI 集成

### 5.1 入口

- **ProxyListPanel 右键菜单**: 新增 "批量解析地区" 菜单项
- **AppController**: 新增 `resolveRegionsForValidProxies()` 方法, 内部创建 RegionBatchResolver 并调用 run()

### 5.2 事件

- 解析完成 → 投递事件 刷新 ProxyListPanel
- 进度展示: 在 StatusBar 显示 "解析进度: {processed}/{total}"

## 6. 文件清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `include/RegionBatchResolver.h` | 新建 | 类声明 |
| `src/RegionBatchResolver.cpp` | 新建 | 类实现 |
| `src/ui/AppController.h` | 修改 | 添加方法声明 |
| `src/ui/AppController.cpp` | 修改 | 添加方法实现, 替换旧实现 |
| `src/ui/ProxyListPanel.cpp` | 修改 | 添加菜单项 + 事件处理 |
| `CMakeLists.txt` | 修改 | 添加 RegionBatchResolver.cpp 到 CORE_SOURCES |
| `docs/design/2026-07-08-Spec-RegionBatchResolver-v1.0.md` | 新建 | 本设计文档 |

## 7. 与 ProxyBatchTester 模式的异同

| 方面 | ProxyBatchTester | RegionBatchResolver |
|------|------------------|-------------------|
| 目的 | 连通性测试 | 地区解析 |
| Xray 启动 | start(N) → getPortPairs() | 相同 |
| Worker 模式 | workerThreadFunc(id, socks, api) | 相同 |
| outbound 注入 | 每个代理都做 | 每个代理都做 |
| 测试方式 | curl --proxy 目标 URL | curl --socks5 ipinfo.io/json |
| 结果写入 | updateTestResultBatch() | updateRegionBatch() |
| DB 交互 | ProfileExItem DAO | ProfileItem 直接 UPDATE |
| 配置生成 | 使用 Profileitem 完整数据 | 相同 |

## 8. 测试策略

- 集成测试: 使用 test/guindb.db 中的有效代理
  - 调用 run() 验证返回 >0
  - 验证 DB 中 Region 列被正确更新
- 单例 RegionBatchResolver 生命周期不受外部中断影响
