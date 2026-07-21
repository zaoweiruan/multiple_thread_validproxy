# Bugfix: GUI 入口缺失 curl_global_init() 导致右键解析地区崩溃

**日期**: 2026-07-20
**版本**: 1.0

## 症状

GUI 模式下右键代理 → 解析地区，对特定代理（indexId=5681338939511710956）触发 SEH 访问违例崩溃。

其他 curl 操作（代理连通性测试、订阅更新）在 GUI 模式下可能正常工作，但此代理的 HTTPS 请求到 ipinfo.io 必崩。

## 日志分析

崩溃为即时 SEH 异常，无有效日志输出。崩溃发生在 `CurlEasyHandle::perform()` 内部的 `curl_easy_perform()` 调用中。

## 根因分析

### 根因: `main_gui.cpp` 未调用 `curl_global_init()`

libcurl 文档要求:

> *"This function must be called at least once within a program (a program is all the code that shares a memory space) before the program calls any other function in libcurl."*

| 入口文件 | curl_global_init | 状态 |
|----------|:---:|:----:|
| `src/main_cli.cpp` (CLI 模式) | ✅ 第 46 行 `curl_global_init(CURL_GLOBAL_ALL)` | 正常 |
| `src/main_gui.cpp` (GUI 模式) **修改前** | ❌ 缺失 | 崩溃 |

之前的重构计划 `docs/plans/2026-05-07-001-refactor-curl-raii-wrapper-plan.md` 明确要求将 `curl_global_init()/curl_global_cleanup()` 添加到 `main.cpp`，但 GUI 入口点被遗漏。

### 崩溃调用链

```
GUI 右键 → ProxyListPanel::onResolveRegion()
         → AppController::resolveSingleProxyRegionAsync(indexId, this)
             → std::thread → doResolveSingleProxyRegion()
                 → SQLite 查询: SELECT Address FROM ProfileItem WHERE IndexId = ?
                 → RegionBatchResolver::fetchRegionFromIpInfo(proxyAddress, token)
                     → isIpPattern(address) == false  (域名，非直连 IP)
                     → DnsCache::resolve(address)       (getaddrinfo DNS 解析)
                     → CurlEasyHandle curl;              ← curl_easy_init()
                     → curl.setUrl("https://api.ipinfo.io/lite/...")
                     → curl.perform()                     ← curl_easy_perform() ★ 崩溃
```

### 为什么特定 indexId 触发

代理 `5681338939511710956` 的 Address 字段为**域名格式**（非直连 IP），在 `fetchRegionFromIpInfo()` 中：

1. `isIpPattern(address)` 返回 `false` → 进入 DNS 解析路径
2. DNS 解析后构造 HTTPS URL → `curl_easy_perform()` 尝试建立连接
3. 由于 `curl_global_init()` 未调用，libcurl 内部 Winsock/SSL 子系统未正确初始化
4. `curl_easy_perform()` 内部访问未初始化内存 → 访问违例

其他 IP 直连的代理不走此路径，或通过不同代码分支侥幸通过了未初始化的 libcurl 状态。

## 修改内容

### `src/main_gui.cpp`

添加 `CurlGlobalGuard` RAII 守卫类，在 `main()` 开头 `curl_global_init(CURL_GLOBAL_ALL)`，在 `main()` 退出时 `curl_global_cleanup()`：

```cpp
// RAII guard for curl_global_init/cleanup — required before any libcurl function.
class CurlGlobalGuard {
public:
    CurlGlobalGuard() { curl_global_init(CURL_GLOBAL_ALL); }
    ~CurlGlobalGuard() { curl_global_cleanup(); }
    CurlGlobalGuard(const CurlGlobalGuard&) = delete;
    CurlGlobalGuard& operator=(const CurlGlobalGuard&) = delete;
};

int main(int argc, char* argv[]) {
    crash::installHandler();
    // ...
    CurlGlobalGuard curlGuard;  // RAII guard — before any libcurl call
    // ...
}
```

需包含 `<curl/curl.h>`（已存在）。

## 验证结果

- `cmake --build build --parallel 8` — 编译通过，0 错误
- `ctest -V` — 18/18 测试通过，100%，0 失败

## 受影响文件

| 文件 | 修改类型 | 说明 |
|------|----------|------|
| `src/main_gui.cpp` | 新增 8 行 | `CurlGlobalGuard` RAII 类定义 + 实例化 |
