# AGENTS.md - multiple_thread_validproxy

## 1. 项目概述

- **语言**: C++ 20/17 (标准: C++17)
- **类型**: 代理验证工具
- **目标平台**: Windows (MinGW/GCC)
- **构建系统**: CMake + Ninja
- **版本**: 1.0.3
- **入口文件**: `src/main.cpp`

## 2. 目录结构

```
multiple_thread_validproxy/
├── include/              # 公共头文件（19 个 .h 文件）
├── src/                  # 源文件（21 个 .cpp 文件）
│   └── main.cpp          # 程序入口
├── bin/                  # 配置(config.json) + 生产数据库(worker/guindb.db)
├── test/                 # 测试数据库（test/guindb.db）
├── tests/                # 单元测试（Google Test）
├── skills/               # agent 技能（14 个子目录）
├── build/                # 构建输出
├── docs/                 # 文档
│   ├── design/           # 功能设计文档
│   └── plans/            # 实施计划文档
├── memory/               # 长期记忆（仅 project_knowledge.md）
└── AGENTS.md
```

## 3. 功能与配置

### 命令行功能
| 参数 | 说明 |
|------|------|
| `-c, --config <path>` | 配置文件路径 (默认: config.json) |
| `-show-sub` | 显示所有订阅 |
| `-G, -generator <id>` | 根据 indexId 生成 outbound JSON |
| `-F, -find-proxy` | 查找第一个可用的代理 |
| `-FMIN, -findminproxy` | 查找延迟最小的代理 |
| `-U, -update <id>` | 更新单个订阅 |
| `-UA, -update-all` | 更新所有启用的订阅 |
| `-T, -test-sub <id>` | 测试订阅中的代理 |
| `-TU, -tourl` | 导出分享链接（导出 delay>0 的代理） |
| `-D, -dedup` | 移除重复代理 |
| `-S, -sync [src[:dst]]` | 同步数据库 |
| `-IS, -import-sub-config <file\|url>` | 批量导入订阅 |
| `-h, --help` | 显示帮助 |

### 核心模块
1. **XrayManager** - xray 实例单例管理
2. **ProxyFinder** - 代理查找模块
3. **ConfigGenerator** - 生成 JSON 配置
4. **ConfigReader** - 读取配置文件
5. **SubitemUpdaterV2** - 订阅更新
6. **ProxyBatchTester** - 多线程并发测试
7. **XrayInstance** - 单个 xray 实例生命周期管理
8. **XrayApi** - xray gRPC API 通信（addOutbound/removeOutbound）
9. **ProxyTester** - 单个代理测试（CURL + Xray 注入）
10. **PortManager** - 端口分配与回收
11. **Logger** - 统一日志系统（时间戳+级别）
12. **UrlFetcher** - URL 内容获取（基于 CURL）
13. **ShareLink** - 分享链接导出
14. **Utils** - 通用工具函数
15. **CurlEasyHandle** - CURL RAII 封装
16. **DatabaseHelper** - SQLite DAO 数据库访问

### 数据库路径
| 类型 | 路径 |
|------|------|
| 测试数据库 | `test/guindb.db` |
| 生产数据库 | `bin/worker/guindb.db` |

### 构建与测试
```bash
# Debug 构建
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=debug
cmake --build build --parallel 8

# 运行
./bin/validproxy.exe

# 运行所有测试
ctest -V

# 运行单个测试
ctest -R CurlEasyHandleTest -V
ctest -R DedupTest -V

# Release 构建
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=release
cmake --build build --parallel 8
```

### 第三方依赖
| 库 | 版本要求 | 安装方式 | 路径 |
|---|---------|---------|------|
| Boost | ≥ 1.80 | 手动 | D:\boost_1_88_0 |
| curl | latest | vcpkg | E:\vcpkg\installed\x64-mingw-static |
| SQLite3 | latest | vcpkg | E:\vcpkg\installed\x64-mingw-static |
| xray-core | latest | 手动 | ../Xray-core |
| Google Test | latest | 源码构建 | E:\eclipse_workspace\googletest |

详细内容（技术架构、代码规范、最近变更）请查阅 **长期记忆文档** `memory/project_knowledge.md`

## 4. 设计文档

| 文档 | 路径 | 说明 |
|------|------|------|
| 订阅去重与黑名单方案 | `docs/design/dedup-blacklist-design.md` | 订阅更新去重 + 代理测试黑名单完整设计方案 |

## 5. 长期记忆

- **位置**: `memory/project_knowledge.md`
- **说明**: memory 目录中的唯一文件，存储项目长期记忆、最近变更、技术架构等
- **内容**: 命令行功能清单、技术架构、工具模式、最近变更记录、文件索引

## 6. 文档组织

| 目录 | 用途 | 说明 |
|------|------|------|
| `memory/` | 长期记忆 | 仅包含 `project_knowledge.md`，存储项目持久化知识 |
| `docs/` | 项目文档 | 存放需求、设计、计划等文档 |
| `docs/design/` | 设计文档 | 功能设计方案，如去重黑名单方案 |
| `docs/plans/` | 实施计划 | 所有实施计划文档（统一目录） |
| `docs/superpowers/specs/` | 功能规格 | 详细功能规格文档 |
| `docs/glossary.md` | 项目术语表 | 代理协议、模块、数据模型等术语定义 |

**注意**: AGENTS.md 只保存项目基础信息和文档引用，详细内容请查阅对应文档。

## 7. Agent 操作指南

### 工作流程
1. 读取 AGENTS.md 了解项目整体结构
2. 读取 `memory/project_knowledge.md` 获取技术细节
3. 读取相关设计文档 `docs/design/` 或 `docs/plans/`
4. **创建计划文档** → **审核** → 再执行代码修改
5. 修改后运行 `cmake --build build` 进行构建验证
6. 运行 `ctest` 确认无回归

### ⚠️ 核心开发规则

> **先创建计划文档、审核后再执行**
>
> 任何源代码修改前，必须先在 `docs/plans/` 下创建或更新计划文档（状态为 `draft`），经审核确认后再执行代码变更。完整流程见 [`docs/plans/DEV-PROCESS.md`](docs/plans/DEV-PROCESS.md)。

### 安全边界
- ✅ 允许：编辑 `docs/*.md` 文档
- ✅ 允许：读取 `src/`、`include/` 源码
- ✅ 允许：运行 `cmake --build` 和 `ctest`
- ❌ 禁止：直接修改 `bin/` 目录下的数据库文件
- ⚠️ 测试数据库为 `test/guindb.db`，生产数据库为 `bin/worker/guindb.db`

## File permission rules

Allow edit permission for Markdown files under the `docs` directory:

```rules
{
  "permission": "edit",
  "pattern": "docs/*.md",
  "action": "allow"
},
{permission: edit, pattern: docs/**/*.md, action: allow}
```
