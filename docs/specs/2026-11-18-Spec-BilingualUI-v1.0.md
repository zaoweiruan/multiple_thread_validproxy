## 1. 文档信息

| 项 | 内容 |
|---|---|
| 标题 | 双语言版本方案（中文 / English） |
| 类型 | Spec（实施方案） |
| 模块 | `i18n`（新增）、`src/ui/**`、`src/config/**`、日志层、`tests/**`、`scripts/**` |
| 版本 | v1.0 |
| 日期 | 2026-11-18 |
| 分支 | `Bilingual-version`（worktree：`E:\eclipse_workspace\multiple_thread_validproxy-bilingual`） |
| 状态 | 待评审（draft） |
| 上游约束 | `AGENTS.md` §2.1（C++17 / 禁用 `auto`）、`docs/specs/2026-10-15-Spec-UiMenuChineseI18n-v1.0.md`（菜单文案集中登记于 `tests/ui/UIIds.h`） |

---

## 2. 背景与目标

### 2.1 背景

当前程序**只有中文一套文案**：菜单、工具栏、面板、对话框、托盘、悬浮窗、任务名、状态栏、日志均为硬编码中文字面量，少量位置混有英文（如状态栏 `Ready`、`Loaded subscription:`）。2026-11-17 的 Win7 修复已把所有 UI 非 ASCII 字面量统一为 `L"..."` 宽字面量，并建立 `scripts/check-ui-literals.ps1` + `UiStringEncodingTest` 门禁——这为引入多语言提供了干净的前提（UI 文本已经集中在可枚举的字面量上）。

现状扫描（本次新增盘点脚本 `temp/i18n_inventory.py`，排除行注释）：

| 范围 | 含中文的字符串字面量 | 文件数 |
|---|---|---|
| `src/**`（含 `src/ui`） | 699 | 35 |
| `include/**` | 2 | 1 |
| **合计** | **701** | **36** |

Top 文件：`src/ui/ConfigDialog.cpp` 176、`src/ui/SqlQueryManageDialog.cpp` 73、`src/main_gui.cpp` 67、`src/ui/MainFrame.cpp` 61、`src/ui/StandaloneFloatingWidget.cpp` 41、`src/ui/ProxyListPanel.cpp` 39、`src/ui/AppController.cpp` 36、`src/ui/TaskMonitorPanel.cpp` 30、`src/service/CloudSyncService.cpp` 25、`src/ui/SubscriptionPanel.cpp` 23、`src/task/TaskType.cpp` 21、`src/ui/LogPanel.cpp` 20。

### 2.2 目标

1. 提供**运行期可切换**的中文 / 英文两套界面，无需重启。
2. 语言通过**菜单**直接选择（「设置 → 语言」），并**写入配置文件**（`config.json` 的 `ui.language`），下次启动沿用。
3. 选择英文时，**菜单、工具栏、按钮、面板标题/列名、对话框、托盘与悬浮窗、任务名、状态栏、日志（含文件日志与日志面板）** 中的中文提示切换为英文。
4. 中文为默认语言；**旧配置文件（无 `ui` 节）保持原行为**，不产生行为回归。
5. 建立可维护的字符串资产与门禁，使「新增中文字面量」在 CI/脚本层被发现，避免双语能力随时间腐化。

### 2.3 成功定义（可验证）

- 语言切换后**不重启**，主窗口可见文案（菜单 5 个顶层 + 15 个叶子、工具栏 11 个按钮标签与 tooltip、底部视图 3 个单选项、代理列表列头）全部变为英文。
- 切换后新产生的日志行（日志面板 + 文件）不含 CJK 字符；切换前已渲染的历史日志保持原文。
- 写入 `config.json` 的 `ui.language` 在重启后生效。
- 中文模式下的现有 UI 自动化用例（`tests/ui/**`，68 个文案常量）全部保持通过。

---

## 3. 术语

| 术语 | 含义 |
|---|---|
| 字符串资产 | `resources/i18n/strings.json`，唯一翻译数据源（id / zh / en / note） |
| 生成表 | 由资产生成的 `include/i18n/StringId.h` 与 `src/i18n/StringTable.cpp`（入库） |
| 重译（retranslate） | 语言切换后，把已存在控件上的文本按当前语言重新设置一遍 |
| 语言优先级 | 环境变量 > 配置文件 > 内置默认（`zh-CN`） |

---

## 4. 需求

| ID | 需求 | 验收方式 |
|---|---|---|
| FR-1 | 配置新增 `ui.language`，取值 `"zh-CN"` / `"en-US"`（大小写不敏感，兼容 `zh`/`en`/`zh_CN`/`en_US`），缺省 `"zh-CN"` | 单测：默认值、别名、非法值回退 |
| FR-2 | 「设置」菜单下新增「语言 / Language」子菜单，两个单选（radio）项：`中文`、`English`，选中项与当前语言一致 | UI 自动化 |
| FR-3 | 点击菜单项立即切换运行期语言并重译主窗口全部可见文案 | UI 自动化 + 人工截图 |
| FR-4 | 切换后立即持久化到 `config.json`；保存失败时保留本次运行期语言并给出一次 WARN | 单测（保存失败分支）+ 日志检查 |
| FR-5 | 「配置」对话框新增「界面」分类与「界面语言」枚举属性；确定后生效并落盘 | UI 自动化 + 配置往返单测 |
| FR-6 | 英文模式下，日志（`Logger` 输出的文件/控制台/日志面板）中的中文提示为英文 | 单测 + 实跑任务抽查 |
| FR-7 | 任务类型显示名（`TaskType`）、状态栏提示、消息框、tooltip、列头、AUI 面板标题、托盘菜单、悬浮窗标签均随语言切换 | UI 自动化 + 人工巡检 |
| FR-8 | 中文模式输出与当前版本**逐字一致**（除新增菜单项外） | 现有 UI 测试 68 个文案常量不改仍通过 |
| FR-9 | 源码中不再出现「未登记的中文字面量」；门禁脚本可检出 | `scripts/check-i18n-coverage.ps1` 退出码 0 |
| FR-10 | 环境变量 `VALIDPROXY_LANG=zh-CN|en-US` 覆盖配置语言（供 UI 测试与支持排查） | 单测 + 沙箱脚本 |

---

## 5. 非目标（明确不做）

1. 不翻译：代码注释、标识符、配置键名、数据库内容（订阅备注/代理备注）、Xray/sing-box 自身输出、Windows 系统对话框、第三方库错误原文。
2. 不翻译 `docs/**` 与 README（README 另有中英双份，不属本方案）。
3. 不引入第三方 i18n 依赖（gettext / wxTranslations / ICU），不引入 `.po/.mo` 构建链。
4. 不做「语言热切换时重译已渲染的历史日志」——历史日志是当时语言的事实记录，保持原文。
5. 不改主窗口标题 `validproxy - Proxy Manager`（多套 UI 测试依赖，见 `2026-10-15-Spec-UiMenuChineseI18n-v1.0.md` §3.3）。
6. 不改变任何菜单项 ID、快捷键、助记符、AUI 布局、控件层级（只改文本设置方式）。
7. CLI 的**参数名与输出协议**不变；CLI 控制台提示文案随语言切换（同一套表），但 `--lang` 参数列为可选增强（P3-Optional），不阻塞验收。

---

## 6. 方案总览

```
resources/i18n/strings.json          ← 唯一翻译源（id / zh / en / note）
        │  scripts/gen-i18n.ps1（生成 + --check）
        ▼
include/i18n/StringId.h              ← enum class StrId（生成，含 kStrIdCount）
src/i18n/StringTable.cpp             ← 4 个静态数组：zh-w / zh-u8 / en-w / en-u8（生成）
        │
        ▼
include/i18n/I18n.h  (纯 C++，无 wx 依赖)
   i18n::setLanguage() / language()
   i18n::w(StrId)   -> const wchar_t*     UI（wxString 参数）
   i18n::u8(StrId)  -> const char*        Logger / std::string
   i18n::u8f(StrId, {args}) -> std::string         `{}` 顺序占位
include/i18n/I18nWx.h (仅 UI 层包含)
   i18n::wx(StrId) -> wxString
   i18n::wxf(StrId, {wxString args}) -> wxString
        │
        ├── src/ui/**          菜单/工具栏/面板/对话框/托盘/悬浮窗
        ├── src/**             日志、任务名、状态消息
        ├── src/config/**      ui.language 的解析/序列化/校验
        └── tests/**           单测 + UI 自动化
```

关键取舍：

- **不用 `wxTranslations`**：wx 的 catalog 在 `wxApp::Init` 前加载，运行期切换仍要手工重译所有控件；且引入 `.po/.mo` 构建链与 `msgfmt` 依赖（MinGW 工具链未保证）。自建表在编译期即可校验完整性，且**纯 C++ 无 wx 依赖**，可被 `UITests`（不包含 wx 头）直接链接复用。
- **不按「中文原文」做运行时查表**：以 `StrId` 枚举为键，缺项是编译错误（`static_assert` 表长）+ 单测兜底，不会出现「运行到某条路径才发现漏翻译」。中文原文仍可由生成器反查，便于检索。

---

## 7. 详细设计

### 7.1 语言模型与优先级

`include/i18n/Language.h`：

```cpp
enum class Language { ZhCN = 0, EnUS = 1 };

Language defaultLanguage();                       // ZhCN
bool parseLanguage(const std::string& text, Language& out);   // 兼容 zh/en/zh-CN/en-US/zh_CN/en_US
const char* languageCode(Language);               // "zh-CN" / "en-US"
```

优先级（启动时一次性解析，`main_gui.cpp` / `main_cli.cpp` 共用 `resolveStartupLanguage()`）：

1. 环境变量 `VALIDPROXY_LANG`（非法值 → WARN 并忽略）；
2. `config.ui.language`（非法/缺失 → 默认，缺失不告警，非法值一条 WARN）；
3. `Language::ZhCN`。

运行期切换只由 UI 线程调用 `i18n::setLanguage()`，并同步更新 `config.ui.language`。

### 7.2 字符串资产与代码生成

`resources/i18n/strings.json`（UTF-8，键序稳定，便于 diff）：

```json
{
  "version": 1,
  "strings": [
    { "id": "MainFrame.Menu.File",        "zh": "&文件",              "en": "&File",        "note": "顶层菜单，保留 & 助记符" },
    { "id": "MainFrame.Menu.UpdateAll",   "zh": "更新全部订阅\tCtrl+U", "en": "Update All Subscriptions\tCtrl+U", "note": "加速键不可翻译" },
    { "id": "MainFrame.Status.Ready",     "zh": "就绪",              "en": "Ready" },
    { "id": "Log.Batch.Started",          "zh": "批量测试开始：共 {} 个代理", "en": "Batch test started: {} proxies" }
  ]
}
```

`scripts/gen-i18n.ps1`：

- `-Check`：重新生成到临时目录并与入库文件逐字节比较，不一致 → 退出码 1（CI/门禁用）。
- 默认：生成 `include/i18n/StringId.h` 与 `src/i18n/StringTable.cpp`。
- 校验：id 唯一、id 正则 `^[A-Za-z][A-Za-z0-9]*(\.[A-Za-z][A-Za-z0-9]*)+$`、`zh`/`en` 非空、`{}` 数量一致、`en` 不含 CJK、`\t` 后加速键两语言一致。

生成的 `StringId.h`：

```cpp
enum class StrId : int {
    MainFrame_Menu_File = 0,
    MainFrame_Menu_UpdateAll,
    // ...
    Count
};
constexpr int kStrIdCount = static_cast<int>(StrId::Count);
```

生成的 `StringTable.cpp`（节选）：

```cpp
static const wchar_t* const kZhW[] = { L"&文件", L"更新全部订阅\tCtrl+U", /* ... */ };
static const wchar_t* const kZhU8[] = { u8"&文件", u8"更新全部订阅\tCtrl+U", /* ... */ };
static const wchar_t* const kEnW[] = { L"&File", L"Update All Subscriptions\tCtrl+U", /* ... */ };
static const wchar_t* const kEnU8[] = { u8"&File", u8"Update All Subscriptions\tCtrl+U", /* ... */ };
static_assert(sizeof(kZhW) / sizeof(kZhW[0]) == kStrIdCount, "zh table out of sync");
static_assert(sizeof(kEnU8) / sizeof(kEnU8[0]) == kStrIdCount, "en table out of sync");
```

> 直接生成两套编码字面量（`L"..."` 与 `u8"..."`），避免运行期宽窄转换与缓存同步问题；表为 `const` 静态存储，任意线程读取零锁。

ID 命名规范见附录 B。

### 7.3 运行时 API 与线程安全

```cpp
// include/i18n/I18n.h  （不含任何 wx 头）
namespace i18n {
void setLanguage(Language lang);              // UI 线程调用
Language language();
const wchar_t* w(StrId id);
const char*    u8(StrId id);
std::string    u8f(StrId id, std::initializer_list<std::string> args);
}
```

- 内部状态仅一个 `std::atomic<int> g_lang`；`setLanguage` 一次性写入，读取端单次原子读 → **一条消息内不会中英混排**。
- `w()`/`u8()` 返回静态存储指针，永不解引用悬垂；worker 线程可安全调用。
- 非法 id（越界）在 Debug 下 `assert`，Release 下返回 `u8"(missing translation)"` 并只打一次 WARN。
- `u8f` 按顺序替换 `{}`；参数数量少于占位符时保留剩余 `{}`，数量多于占位符时忽略多余项（单测覆盖）。

`include/i18n/I18nWx.h`（仅 `src/ui/**` 与 `main_gui.cpp` 包含）：

```cpp
inline wxString wx(StrId id) { return wxString(i18n::w(id)); }
inline wxString wxf(StrId id, std::initializer_list<wxString> args);
```

**编码规则（继承 2026-11-17 修复结论）**：

- wx 控件参数：`i18n::wx(...)` / `i18n::wxf(...)`，不得再出现裸 `L"中文"`。
- `std::string` / `Logger` / JSON 载荷：`i18n::u8(...)` / `i18n::u8f(...)`。
- 表内 `u8"..."` 由生成器产出，源码文件保持 UTF-8（无 BOM），与现有 `check-ui-literals.ps1` 规则一致。

### 7.4 配置持久化

`AppConfig` 新增：

```cpp
struct {
    std::string language = "zh-CN";   // "zh-CN" | "en-US"
} ui;
```

| 文件 | 改动 |
|---|---|
| `include/ConfigReader.h` | `AppConfig::ui` 字段 |
| `include/config/sections/UiConfigParser.h`（新增） | 解析 `ui.language`：类型错误 → WARN + 默认；未知值 → WARN + 默认；缺失 → 静默默认 |
| `src/ConfigReader.cpp` | 注册 `UiConfigParser().parse(...)`（与其余节同级） |
| `src/config/ConfigJsonSerializer.cpp` | 输出 `"ui": { "language": "zh-CN" }`（始终输出，保证可发现） |
| `src/config/ConfigValidator.cpp` | 校验取值属于白名单，非法 → 归一化为 `zh-CN` 并 WARN |
| `bin/config.json`、`bin/test_config.json` | 增加 `"ui":{"language":"zh-CN"}` |
| `src/ui/ConfigDialog.cpp` | 「界面」分类 + `wxEnumProperty(L"界面语言", "ui_language", {"中文 (Chinese)", "English (English)"})` |

配置对话框中的枚举项**始终双语**（`中文 (Chinese)` / `English (English)`），保证英文用户能切回中文。

保存路径复用现有 `ConfigReader::save()`；语言菜单的保存只改动 `config_.ui.language`（不触碰其它字段），并在保存前复制配置，避免与工作线程的 `config_` 读取产生写写竞态。

### 7.5 菜单与即时切换流程

新增常量（`src/ui/MainFrame.cpp` 菜单区，沿用 `wxID_HIGHEST + 1600` 段）：

```
ID_MENU_LANGUAGE_ZH = wxID_HIGHEST + 1600
ID_MENU_LANGUAGE_EN = wxID_HIGHEST + 1601
```

菜单结构（仅新增，不改动既有项）：

```
&设置 / &Settings
├── 配置… / Configuration…            (ID_MENU_CONFIG, Ctrl+, 不变)
├── ─────────────
└── 语言 / Language  ▸
    ├── ● 中文 / Chinese              (radio, ID_MENU_LANGUAGE_ZH)
    └── ○ English / English           (radio, ID_MENU_LANGUAGE_EN)
```

> 菜单标题自身也随语言变化（中文模式显示「设置 / 语言」，英文模式显示「Settings / Language」）。为让**英文用户不改配置也能看懂**，子菜单项标签固定双语：「中文 (Chinese)」「English」。

切换时序（UI 线程，同步执行，无后台任务）：

```
onMenuLanguage(event):
  1. 目标语言 = (id == ZH) ? ZhCN : EnUS
  2. if 目标 == 当前语言: 仅刷新 radio 选中状态; return
  3. i18n::setLanguage(目标)
  4. config_.ui.language = languageCode(目标)
  5. ConfigReader::save(configPath, config_)      // 失败 → WARN 一次 + 状态栏提示，不阻断
  6. applyLanguage()                              // 重译（见 7.6）
  7. UpdateMenuStates() / syncToolbarState()      // 保持既有状态机
  8. Logger::write(u8f(StrId::MainFrame_Log_LanguageChanged, {...}), INFO)
```

**不重建菜单栏**：`MainFrame.cpp` 注释已记录 `initMenuBar()` + `SetMenuBar()` 在 wxMSW 3.2.5/MinGW 上会间歇性挂起。因此重译采用 `wxMenuBar::SetMenuLabel(pos, ...)` 与 `wxMenuItem::SetItemLabel(...)` 原地更新。

### 7.6 重译矩阵

`MainFrame::applyLanguage()` 调用顺序：

| 组件 | 重译内容 | 实现要点 |
|---|---|---|
| `MainFrame` | 5 个顶层菜单标题、15 个叶子项（含加速键 `\tCtrl+X`）、语言子菜单 radio 选中态 | `SetMenuLabel` + `SetItemLabel`；加速键文本入表、不拼接 |
| 工具栏 `m_toolbar` | 11 个按钮 label + short help；详情切换按钮 | `SetToolLabel(id, ...)` / `SetToolShortHelp(id, ...)`；先 `Freeze()` 后 `Thaw()` 防闪烁 |
| AUI 面板 | `wxAuiPaneInfo::Caption`（订阅/代理/详情/日志/任务/队列/监控） | `auiManager_->GetPane(name).Caption(...)` + `Update()` |
| 状态栏 | field 0 默认 `就绪/Ready`、field 4 保持路径 | `SetStatusText`；进行中的动态状态不回溯 |
| 搜索区 | `wxChoice` 两项（代理/订阅）+ 占位提示 | `Clear()` + `Append()`；保持当前选择索引 |
| 底部视图 | `wxRadioBox` 3 项（任务/任务进度/日志）、队列徽标前缀 | `SetString(i, ...)`；徽标文本按当前语言重算 |
| 网络监控条 | `netMonPanel_` 文案、代理监控条 `proxyMonPanel_` 文案 | 各自 `retranslate()` |
| `SubscriptionPanel` | 按钮、右键菜单、列头、空态提示 | 列头更新后恢复列宽（见风险 R5） |
| `ProxyListPanel` | 按钮、右键菜单、8 个列头、过滤项、空态 | 同上 |
| `ProxyDetailPanel` | 字段标签、复制按钮 | — |
| `LogPanel` | 按钮（打开目录/清空/统计）、级别下拉、过滤标签 | 下拉项重建后恢复选中级别 |
| `TaskMonitorPanel` | 「当前任务/排队/操作」标签、按钮、右键菜单 | 任务**内容**由 `TaskType` 表输出，自动随语言 |
| `BatchQueuePanel` | 状态文本、百分比、待执行计数前缀 | — |
| `ComponentListPanel` / `BundleChainEditor` | 列头、按钮 | 仅主窗口存活实例；对话框内实例随对话框新建 |
| `TrayIcon` | 托盘 tooltip + 右键菜单（显示主界面/退出） | 重建 `wxMenu` 后重设；保留原菜单指针生命周期 |
| `StandaloneFloatingWidget` | 标题、列头、右键菜单、模式切换项 | 复用已有 `retranslate()` 位置（若不存在则新增） |
| `AppController` | 无控件持有；其状态字符串在生成时已按当前语言输出 | 见 7.7 |

约定：每个类新增 `void retranslate();`，构造函数末尾调用一次（用当前语言初始化），`MainFrame::applyLanguage()` 逐个调用。构造函数中原有的字面量改为 `i18n::wx(...)`。

### 7.7 日志与任务名

- 业务层日志：`Logger::write("中文", level)` → `Logger::write(i18n::u8(StrId::...), level)`；带参数用 `u8f`。
- 保持 `Logger` 接口不变（避免波及测试替身与统计逻辑），只在调用点替换文本来源。
- 日志行格式、级别、时间戳、`[Module]` 前缀**不翻译**（前缀是检索锚点）。
- `TaskType`：`const char* toString(TaskType)` 保留现有语义（测试与日志依赖），内部改为 `i18n::u8(StrId::Task_XXX)`；括号中的英文枚举名（如 `(TEST_ALL)`）保留，便于日志检索与排障。
- 现有**英文**日志（如 `[MainFrame] Constructor begin`）保持英文，不做中文化（见非目标 1 与 §2.2 目标 3 的最小充分性）。需要时可在表中登记 zh 译文，但不是验收条件。
- 已渲染的历史日志不重译；`LogPanel` 只对**新增**行按当前语言显示。

### 7.8 对话框策略

- `ConfigDialog`、`SqlQueryManageDialog`、`AddPoolMemberDialog`、`BundleSelectorDialog`、`TestOnlineResultDialog`、`BundleChainEditor` 均为**按需创建**：创建时读取当前语言即可，无需活体重译。
- `ConfigDialog` 通过 `ShowModal()` 打开，模态期间无法点击主菜单，因此不存在「对话框打开时语言被菜单切走」的窗口。
- 对话框内的语言项在点击「确定」后由 `MainFrame::onMenuConfig` 检测 `cfg.ui.language != config_.ui.language`，执行 `applyLanguage()`（复用同一路径）。
- `wxMessageBox` 的标题与正文全部走表；按钮为系统提供（`wxOK/wxYES_NO`），语言由 Windows 决定，不纳入范围。

---

## 8. 文件级改动清单

### 8.1 新增

| 文件 | 内容 |
|---|---|
| `resources/i18n/strings.json` | 翻译资产（约 701 条） |
| `include/i18n/Language.h` | 语言枚举与解析 |
| `include/i18n/StringId.h` | 生成：`StrId` 枚举 |
| `include/i18n/I18n.h` | 纯 C++ 查询/格式化 API |
| `include/i18n/I18nWx.h` | wx 便捷封装（仅 UI 层） |
| `src/i18n/StringTable.cpp` | 生成：4 张表 + `static_assert` |
| `src/i18n/I18n.cpp` | 语言原子量、越界保护、`u8f` 实现 |
| `src/config/sections/UiConfigParser.h` | `ui.language` 解析 |
| `tests/test_i18n.cpp` | 表完整性与格式化单测 |
| `tests/test_config_ui_language.cpp` | 配置解析/序列化/校验单测 |
| `tests/ui/TestLanguageSwitch.cpp` | 菜单切换 UI 自动化 |
| `scripts/gen-i18n.ps1` | 生成器（含 `-Check`） |
| `scripts/check-i18n-coverage.ps1` | 中文字面量门禁 |
| `scripts/i18n-allowlist.txt` | 允许保留中文字面量的文件/行说明 |
| `docs/specs/2026-11-18-Spec-BilingualUI-v1.0.md` | 本文档 |

### 8.2 修改（按字面量数量）

| 批次 | 文件（中文面量数） | 小计 |
|---|---|---|
| P1 主窗口 | `MainFrame.cpp`(61)、`StandaloneFloatingWidget.cpp`(41)、`ProxyListPanel.cpp`(39)、`TaskMonitorPanel.cpp`(30)、`SubscriptionPanel.cpp`(23)、`LogPanel.cpp`(20)、`BatchQueuePanel.cpp`(10)、`ComponentListPanel.cpp`(7)、`BundleSelectorDialog.cpp`(6)、`TestOnlineResultDialog.cpp`(5)、`BundleChainEditor.cpp`(5)、`RefreshCommands.cpp`(4)、`ProxyDetailPanel`(0 中文，但需接 `retranslate`)、`TrayIcon.cpp`(1) | 252 |
| P2 对话框 | `ConfigDialog.cpp`(176)、`SqlQueryManageDialog.cpp`(73)、`AddPoolMemberDialog.cpp`(12) | 261 |
| P3 日志/业务 | `main_gui.cpp`(67)、`AppController.cpp`(36)、`CloudSyncService.cpp`(25)、`TaskType.cpp`(21)、`ConflictDetector.cpp`(8)、`ConfigGenerator.cpp`(5)、`ComponentResolver.cpp`(4)、`WebDavCredentials.cpp`(3)、`ProfileitemDAO.cpp`(3)、`StreamSettingsBuilder.cpp`(2)、`SchemaValidator.cpp`(2)、`SubitemDAO.cpp`(2)、`ProfileExItemDAO.cpp`(2)、`include/DatabaseHelper.h`(2)、其余单条文件（`BundleChainResolver`/`SingBoxOutboundBuilderFactory`/`OutboundBuilderFactory`/`ProxyBatchTester`/`AutoTaskManager`/`include/SubitemUpdaterV2.h`） | 188 |
| 配置与构建 | `ConfigReader.h`、`ConfigReader.cpp`、`ConfigJsonSerializer.cpp`、`ConfigValidator.cpp`、`CMakeLists.txt`、`bin/config.json`、`bin/test_config.json` | — |
| 测试常量 | `tests/ui/UIIds.h`、`tests/ui/TestMenuAction.cpp`（适配语言子菜单） | — |
| 文档 | `docs/INDEX.md`、`docs/plans/project-plans-tracker.md`、`README.md`/`README.en.md` | — |

---

## 9. 分阶段实施计划

> 每个阶段结束必须：构建 EXIT 0 + 相关单测通过 + `check-ui-literals.ps1` / `check-i18n-coverage.ps1` 无违规，再进入下一阶段。

### P0 — 基础设施（不改变可见行为）

| 任务 | 产出 | 验证 |
|---|---|---|
| P0-1 | `Language.h` + `I18n.h/.cpp` + 空表骨架 + CMake 接入（`core` 库与 `test_i18n`） | 构建 EXIT 0 |
| P0-2 | `strings.json` 初始化：先用 P0 阶段确认的 12 条（菜单/状态栏/启动）作为种子 | `gen-i18n.ps1` 生成成功 |
| P0-3 | `ui.language` 全链路（AppConfig/Parser/Serializer/Validator/`bin/*.json`） | `test_config_ui_language` 全绿 |
| P0-4 | 启动语言解析（环境变量 + 配置），在 `main_gui`/`main_cli` 最早时机调用 | 手动：`VALIDPROXY_LANG=en-US` 启动，`[i18n] language=en-US` DEBUG 日志 |
| P0-5 | 「设置 → 语言」菜单 + radio 状态 + 持久化 + `applyLanguage()` 骨架（只重译菜单与状态栏） | UI 手动 + 新增 `TestLanguageSwitch`（先覆盖菜单） |

### P1 — 主窗口与长生命周期 UI（252 条）

| 任务 | 文件 | 验证 |
|---|---|---|
| P1-1 | `MainFrame.cpp`：菜单/工具栏/AUI 标题/搜索区/底部 radio/监控条 | `[menu]` + `[layout]` UI 用例 |
| P1-2 | `ProxyListPanel` + `ProxyDetailPanel`（列头/右键菜单/按钮/空态） | `[search]`、`[clear]` UI 用例；列宽回归 |
| P1-3 | `SubscriptionPanel` + `LogPanel` + `TaskMonitorPanel` + `BatchQueuePanel` | `[bottomview]`、`[taskledger]` UI 用例 |
| P1-4 | `StandaloneFloatingWidget` + `TrayIcon` | `[floatingwidget]` UI 用例 |
| P1-5 | `ComponentListPanel` / `BundleChainEditor` / 小对话框 | 手动巡检 |

### P2 — 对话框（261 条）

| 任务 | 文件 | 验证 |
|---|---|---|
| P2-1 | `ConfigDialog`：新增「界面」分类 + 176 条文案 + 保存后触发 `applyLanguage()` | 手动：中/英各打开一次，无截断；配置往返单测 |
| P2-2 | `SqlQueryManageDialog`（73 条） | 手动巡检 + 无 `auto` |
| P2-3 | `AddPoolMemberDialog`（12 条） | 手动巡检 |

### P3 — 日志与业务层（188 条）

| 任务 | 文件 | 验证 |
|---|---|---|
| P3-1 | `main_gui`（启动/退出/错误框/单实例提示） | 启动日志中英各一次 |
| P3-2 | `AppController` + `TaskType` + `CloudSyncService` | 实跑：更新订阅、批量测试、同步、云同步各一次，检查日志无 CJK |
| P3-3 | 配置/DAO 层残余（`ConflictDetector`、`ConfigGenerator`、DAO、Validator 等） | `test_config_reader*` 全绿 |
| P3-4 | `main_cli`（11 条）+ 可选 `--lang` | CLI 两种语言各跑 `-h`、无参测试 |

### P4 — 门禁、测试与文档

| 任务 | 产出 |
|---|---|
| P4-1 | `check-i18n-coverage.ps1` + allowlist，接入 `scripts/build-and-test.bat` |
| P4-2 | `test_i18n` 完整性用例 + `UIIds.h` 与表一致性断言 |
| P4-3 | `TestLanguageSwitch` 全量（菜单/工具栏/列头/底部视图/日志/配置文件） |
| P4-4 | `check-ui-literals.ps1` 扩展：新增 `src/i18n` 例外，其余目录仍为零违规 |
| P4-5 | `docs/INDEX.md`、跟踪器、README（中英）「界面语言」章节 |

---

## 10. 测试计划

### 10.1 单元测试

| 用例 | 断言 |
|---|---|
| 表完整性 | 每条 `StrId` 的 zh/en 均非空；en 不含 `[\u4e00-\u9fff]`；`{}` 数量一致；`\t` 后的加速键两语言相同 |
| 语言解析 | `"zh-CN"/"ZH-cn"/"zh"/"zh_CN"` → ZhCN；`"en-US"/"en"/"en_us"` → EnUS；`"fr"`/`""`/缺失 → 默认 + 无异常 |
| `u8f` | 0/1/3 个占位符；参数不足保留 `{}`；参数多余忽略；参数含 `{}` 不二次替换 |
| 默认语言 | `i18n::language()` 初值 ZhCN；`setLanguage(EnUS)` 后 `w()/u8()` 返回英文 |
| UIIds 一致性 | `ids::MenuFileCaption` 等 20 个中文常量 == `i18n::w(StrId::...)`（ZhCN 模式）— 防止测试常量与表漂移 |
| 配置 | 缺省 zh-CN；`en-US` 解析；非法值回退 + WARN；`save()` 往返保留 `ui.language`；`ui` 为错误类型时容错 |
| 保存失败 | 只读路径下切换语言：运行期语言已切换，日志仅一条 WARN，程序不崩溃 |

### 10.2 UI 自动化（`tests/ui`）

新增 `TestLanguageSwitch.cpp`（`[language]` 标签）：

1. 沙箱以 `VALIDPROXY_LANG=zh-CN` 启动 → 断言菜单为中文（复用现有常量）。
2. 点击「设置 → 语言 → English」→ 断言 5 个顶层菜单、15 个叶子、工具栏 11 个标签、3 个底部单选项、8 个列头均为英文。
3. 断言 `config.json` 的 `ui.language == "en-US"`（读取沙箱配置文件）。
4. 触发一次轻量操作（如刷新列表）→ 断言日志面板新行不含 CJK。
5. 切回中文 → 断言关键文案恢复中文。

`UIIds.h` 新增英文常量块（`MenuFileCaptionEn` 等 20 个）+ 语言子菜单项常量；不修改既有中文常量（FR-8）。测试选择器仍只在 `UIIds.h` 定义（沿用 2026-10-15 规范）。

### 10.3 手工巡检（记录到 PR 描述）

| 场景 | 中文 | 英文 |
|---|---|---|
| 启动 → 主窗口截图 | ✔ | ✔ |
| 配置对话框（全部 tab）截图 | ✔ | ✔ |
| 悬浮窗 + 托盘菜单 | ✔ | ✔ |
| 批量测试 / 更新 / 同步 / 云同步日志 | ✔ | ✔ |
| 100% / 125% / 150% DPI | ✔ | ✔ |

---

## 11. 质量门禁

| 门禁 | 规则 | 失败处理 |
|---|---|---|
| `scripts/check-i18n-coverage.ps1` | `src/**`（排除 `src/i18n/**`）不得出现含 CJK 的字符串字面量；allowlist 需写理由与到期复核日期 | 失败（退出码 1） |
| `scripts/gen-i18n.ps1 -Check` | 生成文件与 `strings.json` 同步 | 失败 |
| `scripts/check-ui-literals.ps1` | 保持既有规则；`src/i18n` 加白名单 | 失败 |
| `UiStringEncodingTest` | 扩展扫描范围，`src/ui` 仍零违规 | 失败 |
| `test_i18n` | 表完整性/编码/占位符 | 失败 |
| 全量 `ctest` | 与基线一致（当前 74/76；既有失败 `LogStatisticsEventTest`、flaky `UI_FLOATINGWIDGET`） | 新增失败即阻塞 |
| `scripts/build-and-test.bat` | UI 变更必跑 | 失败 |

---

## 12. 验收标准

| ID | 标准 | 证据 |
|---|---|---|
| AC-1 | 英文模式源码零未登记中文字面量 | `check-i18n-coverage.ps1` 退出码 0 |
| AC-2 | 菜单/工具栏/列头/底部视图/托盘/悬浮窗不重启即切换 | `TestLanguageSwitch` 通过 + 截图 |
| AC-3 | 语言写入 `config.json` 并重启生效 | 单测 + 手工重启 |
| AC-4 | 英文模式日志无 CJK（抽查 4 类任务） | 日志文件 grep |
| AC-5 | 中文模式与当前版本一致（除新增菜单项） | 既有 68 个 UI 常量与全部 UI 用例通过 |
| AC-6 | `test_i18n` / `test_config_ui_language` 全绿 | ctest 输出 |
| AC-7 | 无 `auto`、无新增编译警告 | `rg -n "\bauto\b" src include`（类型推导上下文）+ 构建输出 |
| AC-8 | 对话框在 125%/150% DPI 下无截断/重叠 | 截图 |

---

## 13. 边界条件与错误处理

| 场景 | 行为 |
|---|---|
| 旧 `config.json` 无 `ui` 节 | 静默使用 `zh-CN`，保存后补写 `ui` 节 |
| `ui.language` 为未知值（如 `"ja-JP"`） | 归一化 `zh-CN`，一条 WARN（含原值） |
| `VALIDPROXY_LANG` 非法 | 忽略环境变量，回落配置文件，一条 WARN |
| 语言切换时正在运行批量测试 | 允许切换（语言不影响 worker 数据路径）；日志切换点之后的行用新语言；任务名在下一次刷新时用新语言 |
| 语言切换时正在写日志 | 单条消息语言一致（原子读），不会半中半英 |
| `config.json` 只读/被占用 | 运行期语言生效；状态栏提示「语言已切换，但配置保存失败」；仅一条 WARN |
| 字符串表缺项（开发期） | 编译期 `static_assert` 失败；若绕过则 Debug `assert`，Release 返回占位文本 + 一条 WARN |
| 英文文案过长导致列头截断 | 保留用户列宽；提供巡检项；必要时在 P1-2 为该列设置按语言的最小宽度 |
| 切换语言时 AUI 布局抖动 | `Freeze()`/`Thaw()` + 不调用 `Update()` 之外的布局重排；保持 pane 尺寸 |
| 主窗口标题 | 两语言均保持 `validproxy - Proxy Manager`（测试依赖） |

---

## 14. 风险与缓解

| ID | 风险 | 影响 | 缓解 |
|---|---|---|---|
| R1 | `wxMenuItem::SetItemLabel` 在 wxMSW 上重新解析 `\t` 加速键，可能丢失/重复注册 | 快捷键失效 | 在 `TestMenuAction` 中逐个验证 8 个已知快捷键；表格中加速键不参与翻译；失败则回退「重建 `wxMenu` 对象但复用 `wxMenuBar`」 |
| R2 | `wxRadioBox::SetString` 在 MSW 上重建子控件 | 事件/焦点丢失 | 切换后恢复选中索引并 `SetFocus()`；`[bottomview]` 用例覆盖 |
| R3 | `wxListCtrl::SetColumn` 改列头可能重置列宽/对齐 | 布局回归 | 切换前后保存/恢复 `GetColumnWidth`；新增列宽回归断言 |
| R4 | 701 条替换引入编译错误或漏改 | 进度风险 | 按文件小批次提交，每批构建；`check-i18n-coverage` 兜底；生成器从扫描结果生成初始 `strings.json` |
| R5 | 英文文案更长导致对话框/工具栏拥挤 | 可用性 | 英文翻译遵循「短标签优先」（如 `Dedup` 而非 `Remove Duplicate Proxies`）；DPI 巡检；必要时调整最小宽度 |
| R6 | 运行期切换与 worker 线程竞争 | 数据竞争 | 仅原子语言索引；表只读；`config_` 写入前复制、只改 `ui.language` |
| R7 | 语言切换触发的重译在 UI 线程耗时（约 700 次 SetLabel） | 卡顿 | 一次性 `Freeze()` 顶层 + 各面板；实测目标 < 150 ms |
| R8 | 测试常量与翻译表漂移 | 测试假绿/假红 | `test_i18n` 断言 `UIIds.h` zh 常量 == 表值 |
| R9 | 生成文件被手改后与 JSON 不一致 | 维护混乱 | `gen-i18n.ps1 -Check` 入门禁；生成文件头部写「自动生成，勿手改」 |
| R10 | 新增文案绕过表（回归） | 双语腐化 | `check-i18n-coverage.ps1` 拒绝 CJK 字面量；allowlist 需评审 |

---

## 15. 发布与迁移

1. 版本号：作为 minor 特性随下一版本发布（`1.7.0`）。
2. 迁移：无数据库变更；`config.json` 首次保存自动补 `ui` 节。
3. README（中/英）新增「界面语言 / UI Language」小节：菜单路径、配置文件键、`VALIDPROXY_LANG` 用途。
4. 发布说明注明：中文为默认；英文覆盖 UI 与日志；历史日志不重译。
5. 打包口径不变（见 `docs/2026-11-17-Reference-ReleasePackaging-v1.0.md`）：`strings.json` **不随包**，全部编译进 exe。

---

## 16. 工作量估算

| 阶段 | 内容 | 人日 |
|---|---|---|
| P0 | 基础设施 + 配置 + 菜单 + 骨架 | 1.5 |
| P1 | 主窗口与长生命周期 UI（252 条） | 2.0 |
| P2 | 对话框（261 条） | 1.5 |
| P3 | 日志/业务（188 条）+ 翻译校对 | 1.5 |
| P4 | 门禁/测试/文档 | 1.0 |
| 合计 | | **7.5 人日**（不含评审与缺陷修复缓冲） |

主要不确定性：翻译质量与英文长度引起的布局返工（P2/P1 各预留 0.5 人日缓冲）。

---

## 17. 评审关注点（供独立评审）

1. **架构**：自建 `StrId` 表 vs `wxTranslations` 的取舍是否成立？两套编码字面量是否过度？生成器是否值得？
2. **完整性**：701 条是否覆盖全部用户可见中文？「英文日志不中文化」的取舍是否符合用户「日志中文提示切英文」的原始诉求？
3. **运行期切换**：MSW 上 `SetItemLabel`/`SetString`/`SetColumn` 的副作用是否被低估？是否存在必须重启的控件（如 `wxPropertyGrid` 分类名、`wxNotebook` 页签）？
4. **线程与配置**：菜单切换时写 `config_` 与 worker 读取的竞态是否被完全消除？保存失败路径是否可接受？
5. **测试策略**：以 UIIds 常量 + `test_i18n` 交叉断言是否足够？是否应改为仅按控件 ID/Name 定位、彻底摆脱文案耦合？
6. **范围**：CLI `--lang`、README、发布包是否需要纳入本期？
7. **可维护性**：`strings.json` 的 ID 命名与去重策略能否支撑长期演进（避免同一句被复制成多份）？

---

## 附录 A：`strings.json` 片段

```json
{
  "version": 1,
  "strings": [
    { "id": "MainFrame.Menu.File",       "zh": "&文件",   "en": "&File" },
    { "id": "MainFrame.Menu.Proxy",      "zh": "&代理",   "en": "&Proxy" },
    { "id": "MainFrame.Menu.Task",       "zh": "&任务",   "en": "&Task" },
    { "id": "MainFrame.Menu.Settings",   "zh": "&设置",   "en": "&Settings" },
    { "id": "MainFrame.Menu.Help",       "zh": "&帮助",   "en": "&Help" },
    { "id": "MainFrame.Menu.Language",   "zh": "语言",    "en": "Language" },
    { "id": "MainFrame.Menu.LangZh",     "zh": "中文 (Chinese)", "en": "中文 (Chinese)", "note": "固定双语，便于切回" },
    { "id": "MainFrame.Menu.LangEn",     "zh": "English (English)", "en": "English (English)", "note": "固定双语" },
    { "id": "MainFrame.Toolbar.Test",    "zh": "批量测试", "en": "Test All" },
    { "id": "MainFrame.Toolbar.TestTip", "zh": "测试全部代理", "en": "Test all proxies" },
    { "id": "TaskMonitor.Radio.Tasks",   "zh": "任务",     "en": "Tasks" },
    { "id": "TaskMonitor.Radio.Progress","zh": "任务进度", "en": "Progress" },
    { "id": "TaskMonitor.Radio.Log",     "zh": "日志",     "en": "Log" },
    { "id": "Log.Batch.Started",         "zh": "批量测试开始：共 {} 个代理", "en": "Batch test started: {} proxies" },
    { "id": "Log.Language.Changed",      "zh": "界面语言已切换为中文", "en": "UI language switched to English" }
  ]
}
```

## 附录 B：`StrId` 命名规范

```
<模块>.<区域>.<名称>
```

- 模块：`MainFrame` / `ProxyList` / `Subscription` / `LogPanel` / `TaskMonitor` / `BatchQueue` / `ConfigDialog` / `SqlDialog` / `Tray` / `FloatingWidget` / `AppController` / `CloudSync` / `Task` / `Cli` / `Common`
- 区域：`Menu` / `Toolbar` / `Status` / `Button` / `Column` / `Label` / `Message` / `Log` / `Error` / `Tooltip` / `Empty`
- 名称：语义化 `UpperCamelCase`；**不要**用序号（`Text1`）——序号会让翻译与代码失去可读对应。
- 共享策略：同模块内语义完全相同的文案可共用一个 ID；跨模块默认不共享（翻译上下文不同）。固定双语项（如语言名）在 `note` 标注。

## 附录 C：日志英文化示例

| 位置 | 改前 | 改后（zh） | 改后（en） |
|---|---|---|---|
| `TaskType.cpp` | `"批量测试 (TEST_ALL)"` | `u8(StrId::Task_TestAll)` → `批量测试 (TEST_ALL)` | `Batch Test (TEST_ALL)` |
| `AppController.cpp` | `"已加入队列: " + name` | `u8f(StrId::App_Queue.Enqueued, {name})` | `Queued: {name}` |
| `CloudSyncService.cpp` | `"云端配置已合并"` | `u8(StrId::CloudSync_Merged)` | `Cloud config merged` |
| `main_gui.cpp` | `"配置加载失败：..."` | `u8f(StrId::Gui_ConfigLoadFailed, {err})` | `Failed to load config: {err}` |
| `MainFrame.cpp` | `L"导出分享链接失败。"` | `wx(StrId::MainFrame_Status_ExportFailed)` | `Failed to export share links.` |
