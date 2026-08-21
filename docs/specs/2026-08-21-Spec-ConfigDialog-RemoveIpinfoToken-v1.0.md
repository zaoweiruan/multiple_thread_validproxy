# Spec: 配置编辑窗口移除 ipinfo.io Token 项

- **日期**: 2026-08-21
- **模块**: `src/ui/ConfigDialog`
- **类型**: 配置 UI 清理（过时项移除）
- **关联**: `docs/bugfix/2026-08-20-Bugfix-RegionBatchResolver-ApiMigration-UnitTest-v1.0.md`、`docs/bugfix/2026-08-21-Bugfix-RegionBatchResolver-IpWhoIs-Accuracy-v1.0.md`

---

## 1. 背景与动机

Region 解析数据源经历两次迁移（ipinfo.io → ip-api.com → ipwho.is），当前 ipwho.is 免费、无需 Token。`config.test.ipinfo_token` 自 2026-08-20 迁移起已无任何代码消费者（仅静默保留），配置编辑窗口中继续展示该输入框会误导用户以为它仍有效。

## 2. 变更内容

`src/ui/ConfigDialog.cpp` 移除 3 处：

| 位置 | 原内容 | 处理 |
|------|--------|------|
| 属性创建（测试分类） | `wxStringProperty(L"ipinfo.io Token", "ipinfo_token", ...)` | 删除 |
| 加载赋值 | `SetPropertyValue("ipinfo_token", ...)` | 删除 |
| 保存读取 | `editedConfig_.ipinfo_token = GetPropertyValueAsString("ipinfo_token")` | 删除，附注释说明 |

### 设计要点

- **范围限定为 UI 层**：`ConfigReader::ipinfo_token` 成员、`TestConfigParser` 解析、`ConfigJsonSerializer` 序列化全部保留——已有 config.json 中的该字段仍被解析并原样写回，无 breaking change（延续 2026-08-20 迁移文档的向后兼容决策）。
- **保存不丢数据**：删除读取行后 `editedConfig_.ipinfo_token` 保持构造时从当前配置拷贝的原值，保存时序列化器原样写回。

## 3. 验证

- 构建：`cmake --build build --parallel 8` ✅ 0 error
- 回归：`ctest --parallel 8` ✅ **31/31 全部通过**
- 人工验证点：配置窗口"测试"分类不再显示 Token 输入框；保存后 config.json 中 `ipinfo_token` 字段值保持不变。

---

## 4. Phase 2：死代码彻底清理（2026-08-21 用户确认后执行）

Phase 1 保留的底层兼容链路经用户确认后移除：

| 位置 | 原内容 | 处理 |
|------|--------|------|
| `include/ConfigReader.h:19` | `std::string ipinfo_token;` 字段声明 | 删除 |
| `include/config/sections/TestConfigParser.h:35-39` | JSON 解析分支（含 wrong-type 警告） | 删除，替换为迁移说明注释 |
| `src/config/ConfigJsonSerializer.cpp:26-28` | 非空时写回 `test.ipinfo_token` | 删除 |
| `src/ui/ConfigDialog.cpp:284` | Phase 1 的过时注释（"serializer writes it back unchanged" 已不成立） | 删除 |

### 行为变化

- **旧 config.json 中已存的 `test.ipinfo_token` 键将被静默忽略**：解析器不再读取、序列化器不再写回——下次通过配置窗口保存后该键从文件中消失。
- 无编译期消费者残留（grep 核验仅剩说明注释）。

### Phase 2 验证

- 构建：✅ 0 error（65/65 目标）
- 回归：ctest ✅ **31/31 全部通过**
- grep 核验：源码中无任何 `ipinfo_token` 功能引用
