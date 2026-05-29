# AGENTS.md 完善计划

---
title: "docs: Improve AGENTS.md quality and completeness"
type: docs
status: completed
date: 2026-05-11
---

> 计划版本: v1.0
> 当前 AGENTS.md 评分: 6.5/10 (参见 `docs/plans/agents-md-review.md`)
> 目标: 9.5/10

---

## 一、改动清单（共 8 项修改）

### P1 — 必改项（影响 agent 工作效率）

| 优先级 | 改动项 | 当前状态 | 工作量 |
|--------|--------|----------|--------|
| P1 | 补充完整 CLI 参数表 | 8 个 → 补齐完整 13 个 | ~5 行 |
| P1 | 更新目录结构 | 缺少 `src/`、`tests/`、`skills/` 等实际目录 | ~10 行 |
| P1 | 扩展核心模块列表 | 6 个 → 补齐 16 个含职责说明 | ~20 行 |
| P1 | 添加核心构建/测试命令 | 完全缺失 | ~10 行 |
| P2 | 添加第三方依赖快速参考 | 只在 memory 中有 | ~8 行 |
| P2 | 添加数据模型说明 | 完全缺失 | ~8 行 |
| P3 | 完善文档索引 | 只列了 1 个文档 | ~5 行 |
| P3 | 添加入口文件和版本信息 | 完全缺失 | ~3 行 |

---

## 二、各改动项详细内容

### 改动 1：补充完整 CLI 参数表（P1）

**位置**: 第 3 节「功能与配置」→「命令行功能」

**当前内容**（8 个参数）：
```
-c, --config      配置文件路径
-show-sub         显示所有订阅
-G, -generator   根据 indexId 生成 outbound JSON
-F, -find-proxy   查找第一个可用的代理
-FMIN, -findminproxy  查找延迟最小的代理
-U, -update      更新单个订阅
-UA, -update-all 更新所有启用的订阅
-T, -test-sub    测试订阅中的代理
-h, --help       显示帮助
```

**改为**（13 个参数，增加 5 个缺失项）：
```
-c, --config <path>         配置文件路径 (默认: config.json)
-show-sub                   显示所有订阅
-G, -generator <id>         根据 indexId 生成 outbound JSON
-F, -find-proxy             查找第一个可用的代理
-FMIN, -findminproxy        查找延迟最小的代理
-U, -update <id>            更新单个订阅
-UA, -update-all            更新所有启用的订阅
-T, -test-sub <id>          测试订阅中的代理
-TU, -tourl                 导出分享链接 (delay>0 的代理)
-D, -dedup                  移除重复代理（去重功能）
-S, -sync [src[:dst]]       同步数据库（从源库同步有效代理到目标库）
-IS, -import-sub-config <file|url>  批量导入订阅（从文件或 URL）
-h, --help                  显示帮助
```

**操作方式**: 编辑替换整个命令表格行。

---

### 改动 2：更新目录结构（P1）

**位置**: 第 2 节「目录结构」

**当前内容**：
```
multiple_thread_validproxy/
├── include/              # 公共头文件
├── bin/                  # 配置与数据库
├── test/                 # 测试数据库
├── build/                # 构建输出
├── docs/                 # 设计文档和方案
│   ├── design/           # 功能设计文档
│   └── plans/            # 实施计划文档（统一目录）
├── memory/               # 长期记忆（仅 project_knowledge.md）
└── AGENTS.md
```

**改为**（增加 `src/`、`tests/`、`skills/`）：
```
multiple_thread_validproxy/
├── include/              # 公共头文件（19 个 .h 文件）
├── src/                  # 源文件（21 个 .cpp 文件）
│   └── main.cpp          # 程序入口
├── bin/                  # 配置与数据库
│   ├── config.json       # 配置文件
│   └── worker/           # 生产数据库目录
├── test/                 # 测试数据库（test/guindb.db）
├── tests/                # 单元测试源码（Google Test）
├── build/                # 构建输出
├── docs/                 # 设计文档和方案
│   ├── design/           # 功能设计文档
│   ├── plans/            # 实施计划文档（统一目录）
│   └── architecture.md   # 架构文档
├── skills/               # 自定义技能
├── memory/               # 长期记忆（仅 project_knowledge.md）
└── AGENTS.md
```

**操作方式**: 编辑替换整个目录树块。

---

### 改动 3：扩展核心模块列表（P1）

**位置**: 第 3 节「功能与配置」→「核心模块」

**当前内容**（6 个模块，仅名称）：
```
1. XrayManager          - xray 实例单例管理
2. ProxyFinder          - 代理查找模块
3. ConfigGenerator      - 生成 JSON 配置
4. ConfigReader         - 读取配置文件
5. SubitemUpdater       - 订阅更新
6. ProxyBatchTester     - 多线程并发测试
```

**改为**（16 个模块，含职责和关键方法）：
```
#### 核心模块

| 模块 | 职责 | 关键方法 |
|------|------|---------|
| **XrayManager** | xray 实例单例管理 | `start()`、`stop()`、`getInstance()` |
| **XrayInstance** | 单个 xray 实例生命周期 | 启动/停止/配置生成 |
| **XrayApi** | xray gRPC API 通信 | `addOutbound()`、`removeOutbound()` |
| **ProxyFinder** | 代理查找与测试 | `findFirstWorkingProxy()`、`findWorkingProxy()` |
| **ProxyBatchTester** | 多线程并发测试 | 批量测试代理 |
| **ProxyTester** | 单个代理测试 | CURL + Xray 注入测试 |
| **ConfigGenerator** | 生成 Xray JSON 配置 | 支持 SS/VMess/VLESS/Trojan |
| **ConfigReader** | 读取 config.json 配置 | 解析所有配置段 |
| **SubitemUpdaterV2** | 订阅更新（含去重） | 更新+去重 |
| **PortManager** | 端口分配管理 | 端口分配/回收 |
| **Logger** | 统一日志系统 | 带时间戳+级别 |
| **UrlFetcher** | URL 内容获取 | 基于 CURL 的 HTTP 请求 |
| **ShareLink** | 分享链接导出 | 导出 delay>0 代理 |
| **Utils** | 通用工具函数 | 字符串处理/时间戳 |
| **CurlEasyHandle** | CURL RAII 封装 | 安全的 CURL 生命周期 |
| **DatabaseHelper** | 数据库访问层 | SQLite DAO 操作 |

#### 数据模型
| 模型 | 说明 |
|------|------|
| **Profileitem** | 代理配置项 |
| **Subitem** | 订阅项 |
| **ProfileExItem** | 扩展代理配置项 |
```

**操作方式**: 用表格替换现有列表。

---

### 改动 4：添加核心构建/测试命令（P1）

**位置**: 第 3 节「功能与配置」末尾，新增「构建与测试」小节

**添加内容**：
```
### 构建与测试

#### 构建命令
```bash
# 配置（Debug 模式）
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=debug

# 编译（并行 8 线程）
cmake --build build --parallel 8

# 运行
./build/validproxy.exe

# 清理
cmake --build build --target clean

# Release 模式
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=release
cmake --build build --parallel 8
```

#### 测试命令
```bash
# 运行所有测试
ctest -V

# 运行单个测试
ctest -R DedupTest -V
ctest -R CurlEasyHandleTest -V

# 直接运行测试二进制
./build/tests/test_dedup
./build/tests/test_curl_easy_handle
```

#### 测试框架
- **框架**: Google Test
- **测试文件**: tests/test_dedup.cpp, tests/test_curl_easy_handle.cpp
- **数据库**: 测试使用 `test/guindb.db`
```

**操作方式**: 在「数据库路径」表格后新增一个三级标题。

---

### 改动 5：添加第三方依赖快速参考（P2）

**位置**: 第 3 节「功能与配置」末尾，新增「第三方依赖」小节

**添加内容**：
```
### 第三方依赖

| 库 | 版本要求 | 安装方式 | 路径 |
|---|---------|---------|------|
| Boost | ≥ 1.80 | 手动 | D:\boost_1_88_0 |
| curl | latest | vcpkg | E:\vcpkg\installed\x64-mingw-static |
| SQLite3 | latest | vcpkg | E:\vcpkg\installed\x64-mingw-static |
| xray-core | latest | 手动 | ../Xray-core |
| Google Test | latest | 源码构建 | E:\eclipse_workspace\googletest |
```

**操作方式**: 在「构建与测试」后新增表格。

---

### 改动 6：完善文档索引（P3）

**位置**: 第 4 节「设计文档」

**当前内容**（仅 1 条）：
```
| 文档 | 路径 | 说明 |
| 订阅去重与黑名单方案 | docs/design/dedup-blacklist-design.md | ... |
```

**改为**（4 条，包含所有文档类型的索引）：
```
### 设计文档
| 文档 | 路径 | 说明 |
|------|------|------|
| 架构概览 | `docs/architecture.md` | 核心模块、数据流、构建系统 |
| 项目上下文 | `docs/context.md` | 关联项目（v2rayn、xray）及配置 |
| 订阅去重与黑名单方案 | `docs/design/dedup-blacklist-design.md` | 去重+黑名单完整设计方案 |

### 实施计划
所有实施计划文档统一存放于 `docs/plans/` 目录。
```

**操作方式**: 扩展表格内容。

---

### 改动 7：添加入口文件和版本信息（P3）

**位置**: 第 1 节「项目概述」

**当前内容**：
```
- **语言**: C++ 20/17
- **类型**: 代理验证工具
- **目标平台**: Windows
- **构建系统**: CMake + Ninja
```

**改为**：
```
- **语言**: C++ 20/17 (标准: C++17)
- **类型**: 代理验证工具
- **目标平台**: Windows (MinGW/GCC)
- **构建系统**: CMake + Ninja
- **版本**: 1.0.3
- **入口文件**: `src/main.cpp`
```

**操作方式**: 编辑列表，增加 2 行。

---

### 改动 8：添加 Agent 操作指南（P2）

**位置**: 文件末尾新增一节

**添加内容**：
```
## 7. Agent 操作指南

### 基本规则
- 源代码在 `src/` 目录，头文件在 `include/` 目录
- 构建产物输出到 `build/` 目录
- 所有 `.md` 文档修改需先经过评审

### 安全边界
- ✅ 允许：编辑 `docs/*.md` 文档
- ✅ 允许：读取 `src/`、`include/` 源码
- ✅ 允许：运行 `cmake --build` 和 `ctest` 命令
- ❌ 禁止：直接修改 `bin/` 目录的数据库文件
- ❌ 禁止：修改生产数据库 `bin/worker/guindb.db`
- ⚠️ 注意：测试数据库为 `test/guindb.db`

### 工作流程
1. 读取 AGENTS.md 了解项目整体结构
2. 读取 `memory/project_knowledge.md` 获取详细信息
3. 读取相关设计文档 `docs/design/` 或 `docs/plans/`
4. 实施方案前先创建计划文档
5. 修改后进行构建验证 (`cmake --build build`)
6. 运行测试 (`ctest`) 确认无回归
```

**操作方式**: 在「6. 文档组织」后、File permission rules 前新增一节。

---

## 三、实施顺序

```
第 1 轮（P1，核心可用性）
  ├── 改动 4：添加构建/测试命令        ← 最高优先级
  ├── 改动 1：补充 CLI 参数
  ├── 改动 2：更新目录结构
  └── 改动 3：扩展核心模块列表

第 2 轮（P2，信息完整性）
  ├── 改动 5：添加第三方依赖
  └── 改动 8：添加 Agent 操作指南

第 3 轮（P3，锦上添花）
  ├── 改动 6：完善文档索引
  └── 改动 7：添加版本和入口信息
```

---

## 四、预期效果

完善后 AGENTS.md 将具备：

| 检查项 | 当前 | 完善后 |
|--------|------|--------|
| 项目概述 | ✅ 基本信息 | ✅ 含版本+入口 |
| 目录结构 | ⚠️ 不完整 | ✅ 完整反映实际结构 |
| CLI 参数 | ⚠️ 8 个 | ✅ 完整 13 个 |
| 核心模块 | ⚠️ 6 个无职责 | ✅ 16 个含职责+KPI |
| 构建命令 | ❌ 缺失 | ✅ 完整构建流程 |
| 测试命令 | ❌ 缺失 | ✅ ctest + 单测 |
| 第三方依赖 | ❌ 缺失 | ✅ 快速参考表 |
| 数据模型 | ❌ 缺失 | ✅ 核心模型 |
| 文档索引 | ⚠️ 1 条 | ✅ 完整索引 |
| Agent 规则 | ⚠️ 仅权限 | ✅ 完整操作指南 |
| **综合评分** | **6.5/10** | **9.5/10** |
