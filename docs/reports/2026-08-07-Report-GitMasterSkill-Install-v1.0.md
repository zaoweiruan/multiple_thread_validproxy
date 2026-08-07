# git-master 技能安装报告

**日期**: 2026-08-07
**类型**: 工具/技能安装记录（只读操作，未改动项目代码）
**模块**: Agent 技能环境（全局技能目录 `C:\Users\dsm\.config\opencode\skills\`）
**版本**: v1.0
**状态**: 已安装完成

---

## 1. 背景与需求

`AGENTS.md` §6.2 智能路由跳转表的 `git` / `commit` / `分支` / `合并` / `push` / `PR` 等关键词路由要求加载 `skill(name="git-master")`，但本地环境缺失该技能：

- 项目 `.kilo/` 与全局技能目录均无 git-master；
- `skill` 工具调用报 not found。

需求：安装 git-master 技能到全局目录，使路由表可正常生效。

## 2. 技能搜索与选择

按 find-skills 技能流程执行 `npx -y skills find git-master`，返回 8 个同名候选：

| 候选源 | 安装量 | 结论 |
|---|---|---|
| `josiahsiegel/claude-plugin-marketplace@git-master` | **563** | ✅ 用户选定（推荐） |
| yeachan-heo/oh-my-codex@git-master | 99 | — |
| code-yeongyu/oh-my-opencode@git-master | 95 | — |
| yeachan-heo/oh-my-claudecode@git-master | 67 | — |
| cnife / arendon1 / rockcookies 等 | ≤22 | — |

## 3. 安装过程

### 3.1 直连尝试（失败）

`npx -y skills add josiahsiegel/claude-plugin-marketplace@git-master -g -y` 两次失败：

- `Recv failure: Connection was reset` / `Failed to connect to github.com port 443`
- 根因：GitHub 直连不通，且当时 `git config` 与系统环境变量均未配置代理。

### 3.2 镜像尝试（全部失败）

| 镜像 | 结果 |
|---|---|
| ghfast.top / gh-proxy.com / ghproxy.net | 403 |
| gitclone.com | 404 |
| github.moeyy.xyz | 域名不存在 |

### 3.3 配置代理后手动安装（成功）

1. 用户配置 git 全局代理：`git config --global http.proxy socks5://127.0.0.1:10808`、`git config --global https.proxy socks5://127.0.0.1:10808`；
2. 浅克隆源仓库：`git clone --depth 1 https://github.com/josiahsiegel/claude-plugin-marketplace.git C:\Users\dsm\AppData\Local\Temp\kilo\claude-plugin-marketplace`；
3. 技能位于仓库 `plugins\git-master\skills\git-master\`；
4. 复制到全局技能目录：`Copy-Item -Recurse -Force` → `C:\Users\dsm\.config\opencode\skills\git-master\`；
5. 验证 10 个文件齐全，删除临时克隆目录，`Test-Path ...\SKILL.md` = True。

## 4. 安装结果

**位置**：`C:\Users\dsm\.config\opencode\skills\git-master\`（全局，源自 josiahsiegel/claude-plugin-marketplace，563 安装量）

**SKILL.md**（149 行）：

- frontmatter `name: git-master`；
- description 覆盖：全部 Git 操作（基础/高级/危险）、仓库管理、分支策略与工作流、冲突解决、历史重写/恢复、平台特定操作（GitHub/Azure DevOps/Bitbucket）、高级命令（rebase/cherry-pick/filter-repo）；
- 提供破坏性操作安全护栏、平台最佳实践、reflog 恢复技巧；
- **自动询问用户偏好（自动提交 vs 手动控制）**；
- Windows 路径要求：Edit/Write 使用反斜杠 `\` 而非 `/`。

**references/ 9 个参考文档**：

| 文件 | 大小 |
|---|---|
| basic-operations.md | 6733 B |
| advanced-commands.md | 5053 B |
| dangerous-operations.md | 4845 B |
| hooks-and-security.md | 4364 B |
| merging-rebasing.md | 3658 B |
| cross-platform.md | 4616 B |
| platform-workflows.md | 2442 B |
| troubleshooting-recovery.md | 2570 B |
| performance-large-files.md | 1279 B |

## 5. 注意事项

1. **生效时机**：当前会话不可用，需新开会话/重启 Kilo 后出现在可用技能列表；
2. **git 全局代理**：`socks5://127.0.0.1:10808` 已生效，如后续不再需要可移除：`git config --global --unset http.proxy` / `git config --global --unset https.proxy`；
3. **替代方案**：本会话内可用 `ce-commit` / `ce-commit-push-pr` 技能替代 git-master。

## 6. 验证

- `Test-Path C:\Users\dsm\.config\opencode\skills\git-master\SKILL.md` = True；
- 目录内 10 个文件（SKILL.md + references/ 9 个）齐全；
- 源仓库临时克隆已删除，无残留。
