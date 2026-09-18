# Bugfix: ConfigDialog wxFileProperty 只显示文件名（wxWidgets 3.3.3 wxPG_FILE_SHOW_FULL_PATH 失效）

- 日期: 2026-08-19
- 类型: Bugfix
- 模块: ConfigDialog (UI 配置编辑窗口)
- 版本: v1.0
- 日志: N/A（UI 显示问题，无运行时日志）

---

## 1. 问题描述

`config.json` 中所有文件路径字段（`database.path`、`proxy.xray_executable`、`proxy.xray_asset_dir`、`proxy_template_config_path`、`proxy_singbox_executable`、`proxy_singbox_template_config_path`、`sync.source_db`、`sync.target_db`）均存储**绝对路径**，例如：

```json
"database": {
    "path": "E:/eclipse_workspace/multiple_thread_validproxy/bin/worker/guiNDB.db"
}
```

但在**配置编辑窗口**（ConfigDialog）中，所有 `wxFileProperty` 字段**仅显示文件名**（如 `guiNDB.db`），不显示目录部分。用户无法确认当前配置指向的具体位置。

---

## 2. 根因分析

### 2.1 代码现状

`src/ui/ConfigDialog.cpp` 对所有 8 个 `wxFileProperty` 均调用了：

```cpp
propGrid_->SetPropertyAttribute("database_path", wxPG_FILE_SHOW_FULL_PATH, (long)1);
```

`wxPG_FILE_SHOW_FULL_PATH` 宏展开为 `wxS("ShowFullPath")`，本意是通知 `wxFileProperty` 显示完整路径。

### 2.2 wxWidgets 3.3.3 内部 bug

通过阅读 `E:\eclipse_workspace\wxWidgets\src\propgrid\props.cpp` 源码，发现 `wxFileProperty` 的属性处理与渲染逻辑之间存在 **bit 不匹配**：

| 位置 | 代码 | 使用的 Flag 常量 | 实际值 |
|------|------|----------------|--------|
| `DoSetAttribute`（处理 `ShowFullPath`） | `ChangeFlag(wxPGPropertyFlags_ShowFullFileName, value.GetBool())` | `wxPGPropertyFlags_ShowFullFileName` | `wxPGFlags::Reserved_1` = `0x10000000` |
| `ValueToString`（控制显示格式） | `m_flags & wxPGFlags::ShowFullFileName` | `wxPGFlags::ShowFullFileName` | `0x00100000` |

**关键发现：两者是不同 bit！**

- `DoSetAttribute` 设置的是 bit `0x10000000`（`Reserved_1`，内部保留位）
- `ValueToString` 检查的是 bit `0x00100000`（`ShowFullFileName`）

因此 `SetPropertyAttribute("...", wxPG_FILE_SHOW_FULL_PATH, (long)1)` **设置了错误的 bit**，`ValueToString` 永远检测不到 `ShowFullFileName` 标志，最终返回 `filename.GetFullName()`（仅文件名）。

### 2.3 验证

- `wxPGPropertyFlags_ShowFullFileName` 定义于 `include/wx/propgrid/private.h:210`：
  ```cpp
  constexpr wxPGFlags wxPGPropertyFlags_ShowFullFileName = wxPGFlags::Reserved_1;
  ```
- `wxPGFlags::ShowFullFileName` 定义于 `include/wx/propgrid/property.h:395`：
  ```cpp
  ShowFullFileName = 0x00100000,
  ```

两者值不同，`DoSetAttribute` 设置的 bit 永远不会被 `ValueToString` 读到。

---

## 3. 修复方案

### 3.1 核心思路

绕过有 bug 的 `DoSetAttribute` 路径，直接调用 `wxPGProperty::ChangeFlag(wxPGFlags::ShowFullFileName, true)` 设置正确的 bit。

`ChangeFlag(wxPGFlags, bool)` 是 `wxPGProperty` 的公开方法（`property.h:1843`）：

```cpp
void ChangeFlag( wxPGFlags flag, bool set )
{
    if ( set )
        m_flags |= flag;
    else
        m_flags &= ~flag;
}
```

### 3.2 变更

**文件：** `src/ui/ConfigDialog.cpp`

对全部 8 个 `wxFileProperty`，在 `SetPropertyAttribute` 之后添加直接设置正确 flag 的调用：

```cpp
// wxWidgets 3.3.3 bug: wxPGPropertyFlags_ShowFullFileName maps to Reserved_1,
// but ValueToString checks wxPGFlags::ShowFullFileName — set the correct flag directly.
dbPathProp->ChangeFlag(wxPGFlags::ShowFullFileName, true);
```

受影响属性：

| 属性名 | 变量名 | 行号（修复后） |
|--------|--------|--------------|
| `database_path` | `dbPathProp` | L44 |
| `proxy_xray_executable` | `xrayExecProp` | L60 |
| `proxy_xray_asset_dir` | `assetDirProp` | L66 |
| `proxy_template_config_path` | `tmplProp` | L73 |
| `proxy_singbox_executable` | `sbExecProp` | L80 |
| `proxy_singbox_template_config_path` | `sbTmplProp` | L87 |
| `sync_source_db` | `srcDbProp` | L157 |
| `sync_target_db` | `tgtDbProp` | L159 |

### 3.3 保留原有属性调用

`wxPG_FILE_SHOW_FULL_PATH` 的 `SetPropertyAttribute` 调用**保留不动**，原因：
1. 不产生编译错误（宏存在）
2. 向后兼容：若 wxWidgets 修复此 bug，`DoSetAttribute` 逻辑修正后，两处设置叠加仍安全（同 bit OR 操作）
3. 作为代码意图的显式标注

---

## 4. 测试

- 构建通过：`cmake --build build --parallel 8`，0 error
- 单元测试：30/30 passed（100%），无回归

---

## 5. 验收

- [x] ConfigDialog 打开后，所有文件路径字段显示完整绝对路径
- [x] 修改路径后保存，`config.json` 写入的值不变（仅显示层修复，不影响序列化）
- [x] 构建 0 error，ctest 30/30 passed

---

## 6. 影响范围

- **UI 层**：仅 `ConfigDialog` 配置编辑窗口的文件路径显示
- **数据层**：`config.json` 存储格式不变（绝对路径）
- **兼容性**：修复仅使用 `wxPGFlags::ShowFullFileName`（`0x00100000`，wxWidgets 3.x 公共枚举），不影响其他版本
- **上游 bug**：此 bug 存在于 wxWidgets 3.3.3 源码 `src/propgrid/props.cpp:2007`，应提交 upstream PR 修正 `DoSetAttribute` 中 `wxPGPropertyFlags_ShowFullFileName` → `wxPGFlags::ShowFullFileName`
