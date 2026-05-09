# AGENTS.md - multiple_thread_validproxy

## 1. 项目概述

- **语言**: C++ 20/17
- **类型**: 代理验证工具
- **目标平台**: Windows
- **构建系统**: CMake + Ninja

## 2. 目录结构

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
| `-h, --help` | 显示帮助 |

### 核心模块
1. **XrayManager** - xray 实例单例管理
2. **ProxyFinder** - 代理查找模块
3. **ConfigGenerator** - 生成 JSON 配置
4. **ConfigReader** - 读取配置文件
5. **SubitemUpdater** - 订阅更新
6. **ProxyBatchTester** - 多线程并发测试

### 数据库路径
| 类型 | 路径 |
|------|------|
| 测试数据库 | `test/guindb.db` |
| 生产数据库 | `bin/worker/guindb.db` |

详细内容（完整参数列表、技术架构、构建命令、依赖、代码规范、最近变更）请查阅 **长期记忆文档** `memory/project_knowledge.md`

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

**注意**: AGENTS.md 只保存项目基础信息和文档引用，详细内容请查阅对应文档。
## File permission rules

Allow edit permission for Markdown files under the `docs` directory:

```rules
{
  "permission": "edit",
  "pattern": "docs/*.md",
  "action": "allow"
}
```