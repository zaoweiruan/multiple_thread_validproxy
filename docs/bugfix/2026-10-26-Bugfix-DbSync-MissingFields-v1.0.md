# 库表同步：`Region` 与运行统计字段未迁移

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:systematic-debugging to re-verify root cause before modifying this flow.

**Goal:** 修复 `syncDatabases` 在 worker → 副库同步时丢失 `ProfileItem.Region` 及 `ProfileExItem` 运行统计字段的问题，并补充回归测试。
**Tech Stack:** C++17、SQLite、Google Test、PowerShell。
**依据文档:** `docs/DEV-PROCESS.md`、`docs/bugfix/2026-10-26-Bugfix-DbSchema-Maintenance-v1.0.md`。
**约束:** 不使用 `auto`；同步方向为 `bin\worker\guiNDB.db` → `bin\guiNDB.db`。

---

## 0. 现场证据

### 0.1 Region 仍未同步的两个原因

用户删除目标记录后重新同步，Region 仍为空。原因有两个：

1. **运行的是旧 GUI 可执行文件。** `bin\validproxy.exe` 时间戳为 2026/10/26 11:50:05，而 Region 修复代码时间为 14:52:18；旧进程没有重新链接，实际执行的仍是未修复逻辑。关闭进程后重新构建，新 `bin\validproxy.exe` 时间为 15:17:23。
2. **受保护 Subid 仍会跳过。** 此前 worker 中 126 条有效代理全部属于 `5544178410297751350`，该 Subid 在 `dedup.subids` 中；当 `sync.sync_skip_subids=true` 时同步结果为 `Total: 126, Succeeded: 0, Failed: 0`。当前配置已为 `sync_skip_subids=false`。

`log.file_level=WARN` 会隐藏 DEBUG 跳过日志和 REPORT 汇总，因此旧日志里看不到同步细节。

### 0.2 数据对比

| 数据库 | ProfileItem 行数 | Region 非空 |
|--------|------------------|-------------|
| `bin\worker\guiNDB.db` | 381,015 | 4,701 |
| `bin\guiNDB.db`（修复前） | 320,746 | 1,719 |

目标端 `ProfileExItem` 的 `start_count/total_runtime_ms/crash_count` 合计仅为 `1 / 31436 / 0`，与源库运行统计明显不一致。

### 0.3 根因

`src/SubitemUpdaterV2.cpp`：

- `migrateProxy` 的 UPDATE 与 INSERT SQL 均未包含 `Region`；
- `migrateProfileExItem` 的 UPDATE 与 INSERT SQL 均未包含 `start_count`、`total_runtime_ms`、`crash_count`。

模型与数据库列本身均存在，只是迁移写入语句遗漏字段。

## 1. 修复

### 1.1 Region

`migrateProxy`：

1. UPDATE 追加 `Region = ?`（位于 `EchForceQuery` 与 `WHERE IndexId` 之间）；
   - `Region` 绑定位置 35；`IndexId` 由 35 调整为 36。
2. INSERT 列清单追加 `Region`，占位符增至 36；
   - `Region` 绑定位置 36。

空值通过 `bindTextOrNull` 写入 NULL。

### 1.2 ProfileExItem 运行统计

`migrateProfileExItem`：

1. UPDATE：
   - `SET ... consecutive_failures = ?, start_count = ?, total_runtime_ms = ?, crash_count = ? WHERE IndexId = ?`
   - 绑定：1-4 文本，5 `consecutive_failures`，6 `start_count`，7 `total_runtime_ms`（`sqlite3_bind_int64`），8 `crash_count`，9 `IndexId`。
2. INSERT：
   - 列清单追加 `start_count, total_runtime_ms, crash_count`，共 9 列/占位符；
   - 绑定：1 `IndexId`，2-5 文本，6 `consecutive_failures`，7 `start_count`，8 `total_runtime_ms`，9 `crash_count`。

## 2. 回归测试

`tests/test_sync_region.cpp` 已加入 `test_autotask`，覆盖 INSERT 与 UPDATE 两条路径：

1. 复制 `test/guiNDB_empty.db` 为临时源库/目标库；
2. 源库插入两条有效代理：
   - `region-test-id`：Region `US-TestRegion`，运行统计 `7 / 123456 / 2`，目标无记录（INSERT）；
   - `region-update-id`：Region `DE-TestRegion`，运行统计 `9 / 654321 / 3`，目标预置旧值（UPDATE）；
3. 关闭 `sync_skip_subids`，调用 `SubitemUpdaterV2::syncDatabases`；
4. 断言目标库的 Region、`start_count`、`total_runtime_ms`、`crash_count` 全部与源库一致。

CMake：

- `test_autotask` 追加 `tests/test_sync_region.cpp`；
- 增加 `TEST_DB_EMPTY_PATH="${CMAKE_SOURCE_DIR}/test/guiNDB_empty.db"` 编译定义。

## 3. 验证

| 项目 | 结果 |
|------|------|
| `test_autotask` 全量 | 10/10 PASS |
| `SyncRegionTest` | PASS（1/1） |
| 同步汇总 | `Total: 2, Succeeded: 2, Failed: 0` |
| INSERT 路径断言 | Region / 7 / 123456 / 2 全部通过 |
| UPDATE 路径断言 | Region / 9 / 654321 / 3 全部通过 |
| 构建 | `validproxy.exe`（15:17:23）、`validproxy-cli.exe`、`test_autotask.exe` 均成功 |

测试中的 `Failed to migrate subscription ...` 为预期 WARN：临时源库没有对应 SubItem，不影响代理迁移。

## 4. 残余事项

- 修复只影响之后的同步；已存在且未删除的目标记录不会自动补齐，需要再次执行同步（新 GUI 或 CLI）。
- 当前配置 `sync_skip_subids=false`，有效代理可正常迁移；若改回 `true`，需确认目标 Subid 不在 `dedup.subids` 中。
- 同步不复制 `SubItem` 的已有记录内容，仅在缺失时插入且 `enabled=0`，这是既有设计。
