对，这个实证结果会**推翻前面“直接依赖 `ObservatoryService.GetOutboundStatus` 做动态成员健康检测”的方案**。

如果你已经通过 gRPC reflection 实测确认：

```text
Xray Start()
    ↓
Observatory 初始化
    ↓
固定观测标签集合
    ↓
此时 px-* 尚不存在
    ↓
运行期 AddOutbound(px-001)
    ↓
Outbound Manager 有 px-001
    ↓
Balancer 可能有 px-001
    ↓
Observatory 仍不知道 px-001
    ↓
GetOutboundStatus()
    ↓
0 条
```

那么你的判断是正确的：

> **`ObservatoryService.GetOutboundStatus` 不能作为这个动态代理池的成员健康数据源。**

而且给 Balancer 补 `observation` 块只是解决 **Xray 26.x 配置结构要求**，并不能改变 Observatory 已经固定的观测对象集合。

---

# 一、架构需要改成什么

我建议把原来的：

```text
px-* 
 ↓
Observatory
 ↓
GetOutboundStatus
 ↓
ProxyHealthEvaluator
```

彻底移除。

改成：

```text
                         Xray
                          │
                 ┌────────┴────────┐
                 │                 │
              Balancer         Outbound Manager
                 │                 │
                 │              px-001
                 │              px-002
                 │              px-003
                 │                 │
                 └────────┬────────┘
                          │
                    实际代理流量
                          │
                          ▼
                 ProxyHealthEvaluator
                          │
                    主动探测机制
                          │
             ┌────────────┼────────────┐
             ▼            ▼            ▼
           Alive        Delay       Performance
             │            │            │
             └────────────┼────────────┘
                          ▼
                       Score
                          │
                ┌─────────┴─────────┐
                ▼                   ▼
           autoPrune           autoOptimize
```

核心变化只有一个：

> **健康检测从 Xray Observatory 内部机制，迁移到 StandaloneProxyPool 自己的主动探测机制。**

---

# 二、最适合你的方案：独立 Probe Outbound

不要让测速请求直接影响生产流量。

给每个动态成员：

```text
px-001
px-002
px-003
```

建立一个**独立的测试任务**：

```text
ProbeScheduler
      │
      ├── px-001
      ├── px-002
      └── px-003
             │
             ▼
       Xray Proxy Route
             │
             ▼
          Internet
```

例如：

```text
ProxyHealthEvaluator
        │
        │ test("px-001")
        ▼
发送 HTTP HEAD/GET
        │
        ▼
通过指定 Xray outbound
        │
        ▼
connectivity-check endpoint
```

然后得到：

```cpp
struct ProbeResult
{
    std::string tag;

    bool success;

    int64_t connectMs;
    int64_t totalMs;

    int64_t timestamp;

    std::string error;
};
```

---

# 三、这里有一个非常关键的问题：怎么“指定使用 px-001”

这是整个改造中最重要的技术点。

如果你直接：

```text
HTTP 请求
 ↓
Xray inbound
 ↓
Balancer
 ↓
px-001 / px-002 / px-003
```

那么你实际上**无法知道这次测试到底测试了哪个节点**。

例如：

```text
Probe
 ↓
Balancer
 ↓
px-003
```

你不能简单认为：

```text
这就是 px-001 的测试结果
```

所以健康探测必须绕过 Balancer，**明确绑定一个 outbound tag**：

```text
Probe(px-001)
    ↓
指定 px-001
    ↓
HTTP request
    ↓
Result
```

而：

```text
正常生产流量
    ↓
Balancer
    ↓
px-001 / px-002 / px-003
```

是另外一条路径。

---

# 四、推荐新增一个“Probe Inbound”

你的 Xray 配置可以保持：

```text
生产 Inbound
       ↓
生产 Routing
       ↓
Balancer
       ↓
px-*
```

然后增加：

```text
Probe Inbound
       ↓
Probe Routing
       ↓
指定 outbound
       ↓
px-001
```

逻辑上：

```text
                    Xray
                     │
          ┌──────────┴──────────┐
          │                     │
       Production             Probe
          │                     │
          ▼                     ▼
       Routing              Probe Route
          │                     │
          ▼                     ▼
      Balancer             px-001
          │                     │
      ┌───┼───┐                 │
      ▼   ▼   ▼                 ▼
    px1 px2 px3             Internet
```

这样 Probe 可以明确做到：

```text
probe(px-001)
probe(px-002)
probe(px-003)
```

---

# 五、但不要为每个节点建立一个 Inbound

不要设计成：

```text
probe-001 → px-001
probe-002 → px-002
probe-003 → px-003
...
```

如果有 500 个代理，会非常难维护。

更好的办法是：

```text
一个 Probe Inbound
        +
动态 Probe Routing
```

或者，如果你的 Xray 版本支持通过 API 动态修改 routing，则：

```text
Probe request
     ↓
携带 node/tag 标识
     ↓
Probe Routing
     ↓
对应 outbound
```

否则可以采用**短生命周期测试进程/测试实例**，但这会增加开销。

---

# 六、另一种更简单的方案：每个成员启动一个独立测试连接

如果 Xray API 的 routing 动态指定不够方便，我反而建议考虑：

```text
StandaloneProxyPool
       │
       ├── px-001
       ├── px-002
       └── px-003
              │
              ▼
       ProbeExecutor
              │
        指定 outbound
              │
              ▼
       HTTP/TCP probe
```

关键不是“用什么 HTTP 库”，而是：

> **测试连接必须明确绑定目标 Xray outbound。**

否则你的测试结果没有节点归属意义。

---

# 七、健康数据结构应该重新设计

原来：

```text
Observatory
 ↓
Alive
Delay
LastError
```

现在应该由自己的 Probe 产生：

```cpp
struct ProxyProbeResult
{
    std::string indexId;
    std::string outboundTag;

    bool success = false;

    int64_t dnsMs = -1;
    int64_t connectMs = -1;
    int64_t ttfbMs = -1;
    int64_t totalMs = -1;

    int64_t bytesReceived = 0;

    double downloadMbps = -1.0;

    std::string error;

    int64_t timestamp = 0;
};
```

这样以后你就不仅能判断：

```text
Alive
```

还能判断：

```text
Latency
TTFB
Download
SuccessRate
```

---

# 八、这样反而比 Observatory 更适合你的项目

你的最终健康模型可以变成：

```text
                   ProbeScheduler
                        │
                        ▼
                 ProbeExecutor
                        │
        ┌───────────────┼───────────────┐
        ▼               ▼               ▼
     px-001          px-002          px-003
        │               │               │
        ▼               ▼               ▼
     Result           Result           Result
        │               │               │
        └───────────────┼───────────────┘
                        ▼
                ProxyHealthEvaluator
                        │
              ┌─────────┼─────────┐
              ▼         ▼         ▼
            Alive     Delay    Throughput
              │         │         │
              └─────────┼─────────┘
                        ▼
                     Score
```

---

# 九、Balancer 继续只负责“选”

这一点不要改变。

例如：

```text
Balancer
strategy = leastPing
selector = ["px-"]
```

Balancer 继续负责：

```text
新连接
 ↓
px-*
 ↓
选择
```

而你的健康系统负责：

```text
测试
 ↓
评分
 ↓
决定是否剔除
```

两者不要耦合成：

```text
HealthEvaluator
      ↓
直接控制 Balancer 内部成员
```

仍然通过：

```text
RemoveOutbound(px-001)
```

即可。

---

# 十、如果要实现“性能自动优化”，建议不要直接改 Balancer

例如：

```text
px-001   30ms    5Mbps
px-002   50ms   80Mbps
px-003   90ms  100Mbps
```

你的 evaluator：

```text
px-001 score = 45
px-002 score = 92
px-003 score = 88
```

可以：

```text
px-002 → ACTIVE
px-003 → ACTIVE
px-001 → ACTIVE
```

而不是直接：

```text
remove px-001
```

因为低带宽并不代表节点不可用。

可以把：

```text
Connectivity
Latency
Performance
Reliability
```

分别保存。

---

# 十一、建议把 `autoPruneDead` 与 `autoOptimize` 完全分开

### autoPruneDead

只处理：

```text
连续失败
timeout
connection refused
DNS failure
```

例如：

```text
failStreak >= 3
       ↓
DRAINING
       ↓
RemoveOutbound
```

### autoOptimize

处理：

```text
延迟
吞吐
成功率
```

但默认：

```text
只评分
不删除
```

否则很容易发生：

```text
测速
 ↓
remove
 ↓
add
 ↓
测速
 ↓
remove
 ↓
add
```

形成 churn。

---

# 十二、你当前 Spec 最重要的一处修改

原来：

```text
ProxyHealthEvaluator
       ↓
XrayApi::getOutboundStatusDirect()
       ↓
ObservatoryService.GetOutboundStatus
```

应该改成：

```text
ProxyHealthEvaluator
       ↓
ProbeScheduler
       ↓
ProbeExecutor
       ↓
指定 px-<indexId>
       ↓
ProxyProbeResult
```

也就是说：

```cpp
// 删除
XrayApi::getOutboundStatusDirect();

// 新增
ProbeExecutor::probe(
    const std::string& outboundTag);
```

然后：

```cpp
auto result =
    probeExecutor.probe("px-001");

healthEvaluator.update(result);
```

---

# 十三、我建议你的最终模块结构

```text
StandaloneProxyPool
│
├── XrayInstance
│
├── XrayApi
│   ├── addOutbound()
│   ├── removeOutbound()
│   └── get...
│
├── ProbeScheduler
│   └── 周期调度
│
├── ProbeExecutor
│   └── 指定 outbound 测试
│
├── ProxyHealthEvaluator
│   ├── failStreak
│   ├── successRate
│   ├── latency
│   ├── throughput
│   └── score
│
└── memberMap_
```

最终关系：

```text
                  Xray
                   │
        ┌──────────┴──────────┐
        │                     │
    Production              Probe
        │                     │
    Balancer            ProbeExecutor
        │                     │
        ▼                     ▼
      px-*              指定 px-*
        │                     │
        └──────────┬──────────┘
                   ▼
             Internet
```

---

## 最重要的结论

你这次 reflection 实证非常关键。**如果 `Observatory` 在 `Start()` 时就固定了 observation subjects，那么对“运行期 AddOutbound 的代理池”而言，`ObservatoryService.GetOutboundStatus` 从架构上就是错误的健康数据源。**

因此我建议把上一版评审结论修正为：

> **Balancer 可以继续使用 observation 配置以满足 Xray 26.x 的结构要求，但 StandaloneProxyPool 不再依赖 ObservatoryService.GetOutboundStatus 判断动态 `px-*` 成员健康。动态成员健康/性能由 Pool 自己的 ProbeScheduler + ProbeExecutor 主动测试，并将结果交给 ProxyHealthEvaluator；Balancer 只负责生产连接的节点选择。**

这实际上会让你的系统边界更清楚，而且还能从“单纯 Ping”升级到**延迟 + 成功率 + 吞吐 + 自动淘汰**。
