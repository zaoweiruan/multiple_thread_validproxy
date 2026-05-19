---
title: "错误分析报告 — Proxy Log Error Analysis"
type: analysis
status: completed
date: 2026-05-14
analyzer: Sisyphus
source: bin/worker/log/*.log (454 个日志文件)
---

# 代理测试日志错误分析报告 — 2026-05-14

## 1. 分析背景

对 `bin/worker/log/` 目录下全部日志文件进行错误模式扫描，覆盖 dedup / test / update / show-sub / find-proxy / findminproxy / generator / import-sub / sync / tourl 等全部操作类型的日志，共发现 **4 类错误/异常模式**。

## 2. 错误分类汇总

### 2.1 `CONFIG_ERROR: REALITY requires publicKey`（主要问题）

| 项目 | 内容 |
|------|------|
| **出现频率** | 极高，单次测试运行可产生 20+ 条 |
| **影响文件** | `test_20260513_142948.log` 等多份测试日志 |
| **触发路径** | `ProxyBatchTester.cpp:78` → `Profileitem::checkRequired()` (`Profileitem.h:85-88`) |
| **判定** | **基础字段值问题（数据层面）** |

**根因**：数据库中的 VLESS/REALITY 代理节点缺少 `PublicKey` 字段值。这些数据来自订阅源导入，导入流程（`SubitemUpdaterV2::parseSubscription`）未对必填字段做校验。

**SQL 层面**：所有查询语句（dedup / update / test 的 WHERE 子句）均**未预过滤** `StreamSecurity='reality' AND (PublicKey='' OR PublicKey IS NULL)` 的无效记录。

**代码验证逻辑**：`ConfigGenerator.cpp:160-161` 和 `Profileitem.h:86-88` 均要求 REALITY 必须配置 publicKey，逻辑正确，但属于最后防线。

---

### 2.2 `CONFIG_ERROR: REALITY requires sni`（同类别问题）

| 项目 | 内容 |
|------|------|
| **出现频率** | 高，伴随 publicKey 缺失同时出现 |
| **触发路径** | `Profileitem.h:89-91` |
| **判定** | **基础字段值问题（数据层面）** |

**根因**：与 2.1 完全相同，REALITY 代理缺少 `Sni`（ServerName）字段。

---

### 2.3 `Skipping ... invalid network: '...'`（订阅数据污染）

| 项目 | 内容 |
|------|------|
| **出现频率** | 中等，大量节点被跳过 |
| **典型日志** | `Skipping snapp.ir:80 - invalid network: 'wshttps://@freev2configs'` |
| **触发路径** | `ConfigGenerator.cpp:40-42` → `isValidNetwork()` |
| **判定** | **基础字段值问题（数据层面）+ 导入代码缺陷** |

**污染值示例**：
- `'wshttps://@freev2configs'` — 订阅源备注标签被解析为 network
- `'tcp🌐 tel : @v2ray_unit'` — emoji 和描述文本混入
- `'ws@freev2configs'` — @提及标签
- `'wsoliver soul'` — 订阅源名称
- `'tcpHajali-9002'` — 非标准后缀

**根因**：订阅解析器（`SubitemUpdaterV2::parseSubscription`）未对 `Network` 字段做白名单校验或清洗就写入数据库。

**后果**：单次查询返回数千条记录，其中大量被运行时 `isValidNetwork()` 跳过，浪费计算资源。

---

### 2.4 代码结构问题（次要）

| 项目 | 内容 |
|------|------|
| **表现** | `checkRequired()` 抛异常 vs `isValidNetwork()` 仅日志跳过的策略不一致 |
| **位置** | `Profileitem.h:85-92` vs `ConfigGenerator.cpp:40-42` |
| **影响** | 异常导致 worker 线程 catch-and-skip，无降级重试 |
| **判定** | 代码设计问题 |

---

## 3. 根因总结

```
订阅源 (远程/本地)
    │
    ▼
SubitemUpdaterV2::parseSubscription()  ← 未校验 publicKey/sni/network 有效性
    │
    ▼
SQLite ProfileItem 表  ← 脏数据写入（缺少 publicKey/sni，network 含垃圾）
    │
    ▼
SQL 查询 (WHERE 子句)  ← 未预过滤无效配置
    │
    ▼
ProxyBatchTester::workerThreadFunc()
    ├─ checkRequired()  ← REALITY 字段缺失 → throw → CONFIG_ERROR 日志
    └─ generateConfig()
        └─ buildStreamSettings()
            └─ REALITY 校验  ← 同上
```

**核心结论**：问题主要出在**数据导入阶段**（`SubitemUpdaterV2`），而非配置生成逻辑。`ConfigGenerator` 的校验是正确的，但承担了本应在数据入口层完成的过滤职责。

## 4. 建议修复方向

1. **订阅导入时校验**：`SubitemUpdaterV2::parseSubscription()` 对 REALITY 类型强制检查 `PublicKey`、`Sni`，不通过则记录 WARN 并跳过该节点
2. **Network 字段清洗**：导入时做白名单匹配，非法值标记为 `tcp`（默认）或跳过
3. **SQL 预过滤**：在 WHERE 子句中增加 `NOT (StreamSecurity='reality' AND (PublicKey='' OR PublicKey IS NULL))` 条件
4. **统一校验策略**：将 `checkRequired()` 的异常改为返回值 + 日志，与 `isValidNetwork()` 策略一致