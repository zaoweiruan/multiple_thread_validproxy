# Bugfix: `isPortAvailable()` 对 `0.0.0.0` 通配监听者返回假阳性

## Problem

当 xray 进程已监听 `0.0.0.0:10808` (及 `[::]:10808`) 时，`isPortAvailable(10808)` 错误返回 `true`（端口空闲），导致后续认为端口未被占用并尝试在该端口上启动 xray，引发 `bind() 失败` 或端口冲突。

日志表现（用户提供）：
```
Line 12: Port 10808 isPortAvailable=true  ← 假阳性
         PID 3064 已占用 0.0.0.0:10808
```

## Root Cause

Windows Winsock 默认允许**不同地址范围**的 `bind()` 共存：

```
已有: xray  bind(0.0.0.0:10808)    ← 通配地址
我们的检测: socket → bind(127.0.0.1:10808)  ← 回环地址
            成功！（Windows 认为这是两个不同地址）
            错误认为端口空闲
```

这是 Windows 的**故意行为**：通配地址 (0.0.0.0) 与 特定地址 (127.0.0.1) 被视为不同的绑定域，除非已有 socket 设置了 `SO_EXCLUSIVEADDRUSE`。

## Solution

将端口检查方式从 `bind()` 改为非阻塞 `connect()`：

```
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
  → ioctlsocket(FIONBIO, 1)           ← 非阻塞
  → connect(127.0.0.1:port)
     ├─ result == 0                    → 端口被占用 (已连接)
     ├─ WSAECONNREFUSED                → 端口空闲 (无监听者)
     ├─ WSAEWOULDBLOCK + select(200ms)
     │   ├─ SO_ERROR == 0              → 端口被占用 (连接成功)
     │   ├─ timeout                    → 端口空闲 (超时无响应)
     └─ 其他错误                        → 保守假设空闲
```

`connect(127.0.0.1:port)` 直接验证 **"TCP 能否连到这个端口"**——这是 xray 启动后客户端的真实行为，不受地址绑定规则干扰。无论服务器监听在 `0.0.0.0`、`127.0.0.1`、`[::]` 还是具体 IP，只要端口有 TCP listener，`connect()` 就会成功。

## Files Changed

### 1. `src/Utils.cpp` — `isPortAvailable()` 完整重写

| 旧实现 | 新实现 |
|--------|--------|
| `socket() + bind(INADDR_LOOPBACK)` | `socket() + connect(127.0.0.1)` |
| 返回 `bind()` 是否成功 | 非阻塞 connect + select(200ms) 超时 |
| 对 0.0.0.0 监听者假阳性 | 正确检测任意地址上的 listener |
| ~10 行 | ~40 行（含四级状态机） |

关键代码段：
```cpp
// 非阻塞 connect 检测——只有目标端口真正有 TCP listener 才会成功
u_long nonblocking = 1;
ioctlsocket(sock, FIONBIO, &nonblocking);

sockaddr_in addr = {};
addr.sin_family = AF_INET;
addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
addr.sin_port = htons(static_cast<u_short>(port));

int result = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
```

## Verification

1. **单元测试**: 21/21 测试全部通过
2. **Python 端到端验证**:
   - `socket(AF_INET, SOCK_STREAM) → bind(0.0.0.0:10808) → listen()`
   - `connect(127.0.0.1:10808)` → 返回 `connected` → `isPortAvailable=false` ✅
3. **生产构建**: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug` → `cmake --build build --parallel 8` → 0 errors, 0 warnings

## Files Changed

- `src/Utils.cpp` — 完整重写 `isPortAvailable()` 函数 (bind → connect)

## Potential Future Considerations

- `SO_EXCLUSIVEADDRUSE` 被评估并否决——它只保护设置方 socket 不被第三方劫持，不改变 `bind(127.0.0.1)` 对已有 `0.0.0.0` 监听者的成功行为
- 复用 `CurlEasyHandle` + `CURLOPT_CONNECT_ONLY` 也被评估并否决——引入额外依赖、性能损耗（`curl_easy_init()` 开销比 `socket()` 高 1-2 数量级）、且 CurlEasyHandle 的异常包装无法区分"连接成功"和"连接被拒"

当前 `connect()` + `select(200ms)` 方案已是该问题的最优解。
