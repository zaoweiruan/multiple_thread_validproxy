## 1. 文档信息

| 项 | 内容 |
|---|---|
| 标题 | 双语言版本方案（中文 / English） |
| 类型 | Spec（实施方案） |
| 模块 | `i18n`（新增）、`src/ui/**`、`src/config/**`、日志层、`tests/**`、`scripts/**` |
| 版本 | v1.1（评审修订版，修订记录见附录 D） |
| 日期 | 2026-11-18 |
| 分支 | `Bilingual-version`（worktree：`E:\eclipse_workspace\multiple_thread_validproxy-bilingual`） |
| 状态 | 待实施（评审已完成，结论见附录 D） |
| 上游约束 | `AGENTS.md` §2.1（C++17 / 禁用 `auto`）、`docs/specs/2026-10-15-Spec-UiMenuChineseI18n-v1.0.md`（菜单文案集中登记于 `tests/ui/UIIds.h`）、`docs/bugfix/2026-11-17-Bugfix-Win7-UIEncoding-MenuAssertCrash-v1.0.md`（宽字面量规则） |

---

## 2. 背景与目标

### 2.1 背景

当前程序**只有中文一套文案**：菜单、工具栏、面板、对话框、托盘、悬浮窗、任务名、状态栏、日志均为硬编码中文字面量，少量位置混有英文（如状态栏 `Ready`、`Loaded subscription:`、`Network OK`）。2026-11-17 的 Win7 修复已把所有 UI 非 ASCII 字面量统一为 `L"..."` 宽字面量，并建立 `scripts/check-ui-literals.ps1` + `UiStringEncodingTest` 门禁——这为引入多语言提供了干净前提（UI 文本已经集中在可枚举的字面量上）。

**现状扫描（v1.1 已修正口径）**：扫描器必须先做词法级注释剥离，再逐条匹配字面量。v1.0 使用的 `line.split("//")[0]` 会把含 `//` 的行（URL、以 `//` 开头的提示串）截断成未闭合引号，导致正则跨行吞串并**系统性漏计**。修正后（`temp/i18n_inventory2.py`，字符串感知的注释剥离）：

| 范围 | 含中文的字符串字面量 | 文件数 |
|---|---|---|
| `src/**`（含 `src/ui`） | 822 | 38 |
| `include/**` | 2 | 1 |
| **合计** | **824** | **39** |

Top 文件：`src/ui/ConfigDialog.cpp` 265、`src/ui/MainFrame.cpp` 88、`src/ui/SqlQueryManageDialog.cpp` 73、`src/main_gui.cpp` 68、`src/ui/StandaloneFloatingWidget.cpp` 41、`src/ui/ProxyListPanel.cpp` 39、`src/ui/AppController.cpp` 36、`src/ui/TaskMonitorPanel.cpp` 30、`src/service/CloudSyncService.cpp` 25、`src/ui/SubscriptionPanel.cpp` 24、`src/task/TaskType.cpp` 21、`src/ui/LogPanel.cpp` 20。

> v1.0 的 701 条/36 文件与各批次小计（252/261/188）全部作废，`ConfigDialog` 实际为 265 条（不是 176），`MainFrame` 为 88 条（不是 61），`main_cli` 为 3 条（不是 11）。

### 2.2 目标

1. 提供**运行期可切换**的中文 / 英文两套界面，无需重启。
2. 语言通过**菜单**直接选择，并**写入配置文件**（`config.json` 的 `ui.language`），下次启动沿用。
3. 选择英文时，**菜单、工具栏、按钮、面板标题/列名、对话框、托盘与悬浮窗、任务名、状态栏、日志（含文件日志与日志面板）** 中的中文提示切换为英文。
4. 中文为默认语言；**旧配置文件（无 `ui` 节）保持原行为**，不产生行为回归。
5. 建立可维护的字符串资产与门禁，使「新增中文字面量」在脚本层被发现，避免双语能力随时间腐化。

### 2.3 成功定义（可验证）

- 语言切换后**不重启**，主窗口可见文案全部变为英文：**6 个顶层菜单**（新增语言菜单）、**19 个叶子项**（17 既有 + 2 语言项）、工具栏 11 个按钮标签与 tooltip、底部 `wxRadioBox` 组标题与 3 个单选项、订阅列表 7 个列头、日志面板标签、状态栏。
- 切换后**新产生**的日志行中不再出现登记过的中文**模板文案**；切换前已渲染的历史日志保持原文；用户数据（订阅名/代理备注/服务器原文）不纳入判定。
- 写入配置文件（UI 沙箱中为 `--config` 指定的文件）的 `ui.language` 在重启后生效。
- 中文模式下的现有 UI 自动化用例（`tests/ui/**`）全部保持通过。

---

## 3. 术语

| 术语 | 含义 |
|---|---|
| 字符串资产 | `resources/i18n/strings.json`，唯一翻译数据源（id / zh / en / note / fixed） |
| 生成表 | 由资产生成的 `include/i18n/StringId.h` 与 `src/i18n/StringTable.cpp`（入库） |
| 重译（retranslate） | 语言切换后，把已存在控件上的文本按当前语言重新设置一遍 |
| 固定双语项 | 两个语言下值相同的表项（语言名本身），用 `"fixed": true` 标记 |
| 语言来源 | 仅两个：`config.ui.language`、内置默认 `zh-CN` |

---

## 4. 需求

| ID | 需求 | 验收方式 |
|---|---|---|
| FR-1 | 配置新增 `ui.language`，取值 `"zh-CN"` / `"en-US"`（大小写不敏感，兼容 `zh`/`en`/`zh_CN`/`en_US`），缺省 `"zh-CN"` | 单测：默认值、别名、非法值回退 |
| FR-2 | 菜单栏新增**顶层**「语言 / Language」菜单（`zh` 值为 `语言 / Language`，`en` 值为 `Language`），内含两个单选（radio）项：`中文 (Chinese)`、`English (English)`，选中项与当前语言一致 | UI 自动化 |
| FR-3 | 点击菜单项立即切换运行期语言并重译主窗口全部可见文案 | UI 自动化 + 人工截图 |
| FR-4 | 切换后立即持久化到**当前生效的配置文件**；保存失败时保留本次运行期语言，并用一次 `wxMessageBox` + 一条 WARN 告知 | 单测（保存失败分支）+ 日志检查 + 手动 |
| FR-5 | 英文模式下，日志（`Logger` 输出的文件/控制台/日志面板）中的中文提示为英文 | 单测 + 实跑任务反查 |
| FR-6 | 任务类型显示名（`TaskType`）、自动任务子任务名、状态栏提示、消息框、tooltip、列头、托盘菜单、悬浮窗标签均随语言切换 | UI 自动化 + 人工巡检 |
| FR-7 | 中文模式输出与当前版本**逐字一致**（除新增的顶层语言菜单与其两个子项外） | 现有 UI 测试常量不改仍通过 |
| FR-8 | 源码中不再出现「未登记的中文字面量」；门禁脚本可检出 | `scripts/check-ui-literals.ps1 -Mode cjk` 退出码 0 |
| FR-9 | 语言持久化必须写入本次进程实际使用的配置文件（`-c/--config` 优先），不得写死到 `bin/config.json` | 单测 + UI 沙箱断言 |

**已删除的 v1.0 需求**：FR-5「配置对话框新增界面语言枚举」（与菜单入口重复，且引入第二条写路径与回滚风险）、FR-10「`VALIDPROXY_LANG` 环境变量」（用户未要求，且会让「菜单选了英文、重启又变回中文」静默发生）。

---

## 5. 非目标（明确不做）

1. 不翻译：代码注释、标识符、配置键名、数据库内容（订阅备注/代理备注）、Xray/sing-box 自身输出、Windows 系统对话框、第三方库错误原文。
2. 不翻译 `docs/**` 与 README 正文；P4 仅在 README 中**新增一节**说明语言入口与配置键（不改既有叙述）。
3. 不引入第三方 i18n 依赖（gettext / wxTranslations / ICU），不引入 `.po/.mo` 构建链。
4. 不做「语言热切换时重译已渲染的历史日志」——历史日志是当时语言的事实记录，保持原文。
5. 不改主窗口标题 `validproxy - Proxy Manager`（多套 UI 测试依赖，见 `2026-10-15-Spec-UiMenuChineseI18n-v1.0.md` §3.3）。
6. 不改变**既有**菜单项 ID、快捷键、助记符、AUI 布局与控件层级；本期唯一新增控件为顶层语言菜单及其两个 radio 项。
7. CLI 的参数名与**机器可解析输出**（JSON、导出文本、退出码）不变；CLI 控制台的人可读提示随语言切换（同一套表）。不新增 `--lang` 参数。
8. 不把现有英文文案（`Ready`、`Loading data...`、`Network OK`、`Region/Latency/...` 列头）改成中文。

---

## 6. 方案总览

```
resources/i18n/strings.json          ← 唯一翻译源（id / zh / en / note / fixed）
        │  scripts/gen-i18n.ps1（生成 + -Check）
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
        ├── src/config/**      ui.language 的解析/序列化
        └── tests/**           单测 + UI 自动化
```

关键取舍：

- **不用 `wxTranslations`**：wx 的 catalog 在 `wxApp::Init` 前加载，运行期切换仍要手工重译所有控件；且引入 `.po/.mo` 构建链与 `msgfmt` 依赖（MinGW 工具链未保证）。自建表在编译期即可校验完整性，且**纯 C++ 无 wx 依赖**，可被 `UITests`（不包含 wx 头）直接链接复用。
- **不按「中文原文」做运行时查表**：以 `StrId` 枚举为键，缺项是编译错误（`static_assert` 表长）+ 单测兜底，不会出现「运行到某条路径才发现漏翻译」。中文原文仍可由生成器反查，便于检索。
- **保留「即时切换」**：见 §17.1 的取舍记录。选择即时切换是本方案的既定前提，代价（MSW 上重设控件文本的副作用）在 §14 显式登记为 R1–R3、R7。

---

## 7. 详细设计

### 7.1 语言模型

`include/i18n/Language.h`：

```cpp
enum class Language { ZhCN = 0, EnUS = 1 };

Language defaultLanguage();                       // ZhCN
bool parseLanguage(const std::string& text, Language& out);   // 兼容 zh/en/zh-CN/en-US/zh_CN/en_US
const char* languageCode(Language);               // "zh-CN" / "en-US"
```

启动解析（`main_gui.cpp` / `main_cli.cpp` 共用 `resolveStartupLanguage()`）：

1. `config.ui.language`（缺失 → 默认，不告警；非法值 → 一条 WARN（含原值）+ 默认）；
2. `Language::ZhCN`。

**没有环境变量覆盖层**。首次启动固定 `zh-CN`，英文用户的可发现性由顶层「语言 / Language」菜单保证（中文模式下该菜单标题即双语）。

运行期切换只由 UI 线程调用 `i18n::setLanguage()`，并经 `AppController::setUiLanguage()` 同步权威配置副本与磁盘。

### 7.2 字符串资产与代码生成

`resources/i18n/strings.json`（UTF-8，键序稳定，便于 diff）：

```json
{
  "version": 1,
  "strings": [
    { "id": "MainFrame.Menu.File",      "zh": "&文件", "en": "&File", "note": "顶层菜单，保留 & 助记符" },
    { "id": "MainFrame.Menu.UpdateAll", "zh": "更新全部订阅\tCtrl+U", "en": "Update All Subscriptions\tCtrl+U", "note": "加速键不可翻译" },
    { "id": "MainFrame.Status.Ready",   "zh": "Ready", "en": "Ready", "note": "现状即英文，保持逐字一致" },
    { "id": "MainFrame.Menu.Language",  "zh": "语言 / Language", "en": "Language", "fixed": true, "note": "英文用户在中文界面下的唯一入口，标题本身双语" },
    { "id": "MainFrame.Menu.LangZh",    "zh": "中文 (Chinese)", "en": "中文 (Chinese)", "fixed": true, "note": "固定双语，便于切回" },
    { "id": "MainFrame.Menu.LangEn",    "zh": "English (English)", "en": "English (English)", "fixed": true, "note": "固定双语" },
    { "id": "MainFrame.Log.LanguageChanged", "zh": "界面语言已切换为中文", "en": "UI language switched to English" }
  ]
}
```

`scripts/gen-i18n.ps1`：

- `-Check`：重新生成到临时目录并与入库文件逐字节比较，不一致 → 退出码 1。
- 默认：生成 `include/i18n/StringId.h` 与 `src/i18n/StringTable.cpp`。
- 校验：
  - id 唯一、正则 `^[A-Za-z][A-Za-z0-9]*(\.[A-Za-z][A-Za-z0-9]*)+$`；
  - `zh`/`en` 非空；`{}` 数量一致（顺序由评审保证，见 7.3）；
  - `en` 不含 `[\u4e00-\u9fff]`，**`"fixed": true` 的条目不适用本条**；
  - `\t` 后的加速键两语言一致；
  - **助记符唯一性**：同一 `模块.区域` 前缀下（如 `MainFrame.Menu.*`），`&` 后的字母在 zh 与 en 各自不得重复。

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

生成的 `StringTable.cpp`（节选，**注意窄表是 `const char*`**）：

```cpp
static const wchar_t* const kZhW[] = { L"&文件", L"更新全部订阅\tCtrl+U", /* ... */ };
static const char*    const kZhU8[] = { u8"&文件", u8"更新全部订阅\tCtrl+U", /* ... */ };
static const wchar_t* const kEnW[]  = { L"&File", L"Update All Subscriptions\tCtrl+U", /* ... */ };
static const char*    const kEnU8[] = { u8"&File", u8"Update All Subscriptions\tCtrl+U", /* ... */ };
static_assert(static_cast<int>(sizeof(kZhW) / sizeof(kZhW[0])) == kStrIdCount, "zh-w table out of sync");
static_assert(static_cast<int>(sizeof(kZhU8) / sizeof(kZhU8[0])) == kStrIdCount, "zh-u8 table out of sync");
static_assert(static_cast<int>(sizeof(kEnW) / sizeof(kEnW[0])) == kStrIdCount, "en-w table out of sync");
static_assert(static_cast<int>(sizeof(kEnU8) / sizeof(kEnU8[0])) == kStrIdCount, "en-u8 table out of sync");
```

> 直接生成两套编码字面量（`L"..."` 与 `u8"..."`），避免运行期宽窄转换；表为 `const` 静态存储，任意线程读取零锁。`u8"..."` 在 C++17 的类型是 `const char[N]`，因此窄表必须是 `const char* const[]`（v1.0 写成 `const wchar_t* const[]` 无法编译）。
>
> `static_assert` 比较的是 `size_t` 与 `int`，必须显式 `static_cast<int>`，否则 `-Wsign-compare` 会新增警告（违反 AC-7）。

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
- 非法 id（越界）在 Debug 下 `assert`，Release 下返回 `"(missing translation)"` 并只打一次 WARN。
- `u8f` 按顺序替换 `{}`；参数不足时保留剩余 `{}`，多余参数忽略，参数内容中的 `{}` 不二次替换（单测覆盖）。

`include/i18n/I18nWx.h`（仅 `src/ui/**` 与 `main_gui.cpp` 包含）：

```cpp
inline wxString wx(StrId id) { return wxString(i18n::w(id)); }
inline wxString wxf(StrId id, std::initializer_list<wxString> args);
```

**编码规则（继承 2026-11-17 修复结论）**：

- wx 控件参数：`i18n::wx(...)` / `i18n::wxf(...)`，不得再出现裸 `L"中文"`。
- `std::string` / `Logger` / JSON 载荷：`i18n::u8(...)` / `i18n::u8f(...)`。
- 表内字面量由生成器产出，源码文件保持 UTF-8（无 BOM）。

**printf 风格格式化迁移（v1.0 遗漏，全仓 26 处含中文的 `wxString::Format`）**：

- 统一改为 `i18n::wxf(StrId, {...})`，参数先转 `wxString`：整数用 `wxString::Format("%lld", static_cast<long long>(v))` 或 `std::to_string` 后 `wxString::FromUTF8`，**禁止**把 `%zu/%ld/%d/%s` 留在译文里。
- 译文不得保留 U+3000（全角空格）对齐符（如 `剩余 %d　成功 %d`），改用普通空格或 ` · ` 分隔。
- `{}` 顺序由评审保证（生成器只能校验数量）；译文必须保持占位符相对顺序，避免参数错位。
- 迁移清单随 P1/P2/P3 各自批次执行，`grep -rn "Format(" src --include=*.cpp` 中带中文的行数在 P3 收尾时必须为 0。

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
| `include/config/sections/UiConfigParser.h`（新增） | 解析 `ui.language`：类型错误 → WARN + 默认；未知值 → WARN + 默认；缺失 → 静默默认。**归一化只在此处发生**（`zh`/`zh_CN`/`ZH-cn` → `zh-CN`） |
| `src/ConfigReader.cpp` | 注册 `UiConfigParser().parse(...)`（与其余节同级） |
| `src/config/ConfigJsonSerializer.cpp` | 输出 `"ui": { "language": "zh-CN" }`（始终输出，保证可发现） |
| `bin/config.json`、`bin/test_config.json` | 增加 `"ui":{"language":"zh-CN"}` |

**不做的事（评审结论）**：

- `ConfigValidator` **不改**：签名是 `validate(const AppConfig&, const std::string&)`，只返回 warnings，无法写回归一化结果；两处都做会对同一非法值打两条 WARN。
- `ConfigDialog` **不新增**语言枚举属性：用户只要求菜单入口，双入口会带来第二条写路径与「对话框保存把语言回滚」的风险。

#### 7.4.1 生效配置路径（新增，修复既有不一致）

现状存在三套路径且互不一致：

| 位置 | 解析结果 |
|---|---|
| `main()`（`-c/--config`） | `exeDir/config.json`，可被命令行覆盖；仅存在于 `main` 局部变量 |
| `ConfigReader::getDefaultConfigPath()`（`AppController::saveConfig` 使用） | `exeDir/config.json` |
| `ConfigPathResolver::defaultConfigPath()`（`MainFrame` 打开 `ConfigDialog` 使用） | `exeDir/../bin/config.json` |

UI 测试以 `--config=<repo>\test\ui-sandbox\config.json` 启动，因此若沿用上述任一路径，语言会写进生产配置，且沙箱断言永远看不到 `en-US`（同时违反 `AGENTS.md` §2.3 测试数据隔离）。

方案：新增进程级生效路径，`main` 解析完参数后立即发布。

新增 `include/config/ActiveConfigPath.h` + `src/config/ActiveConfigPath.cpp`：

```cpp
namespace config {
void setActiveConfigPath(const std::string& path);   // main_gui / main_cli 各调用一次
const std::string& activeConfigPath();               // 未设置时回落 ConfigReader::getDefaultConfigPath()
}
```

- `main_gui.cpp`：`-c/--config` 解析完成后立即 `config::setActiveConfigPath(configPath)`，早于 `ConfigReader::load`。
- `main_cli.cpp`：同样在参数解析后设置。
- `AppController::saveConfig`：`ConfigReader::save(config::activeConfigPath(), cfg)`（普通运行时与现状同文件，行为不变）。
- `MainFrame::onMenuConfig`：`new ConfigDialog(this, controller_->getConfig(), db_, config::activeConfigPath())`（消除与 `saveConfig` 的分叉）。

### 7.5 菜单与即时切换流程

新增常量（`src/ui/UiControlIds.h`，沿用 `wxID_HIGHEST + 1600` 段）：

```
ID_MENU_LANGUAGE_ZH = wxID_HIGHEST + 1600
ID_MENU_LANGUAGE_EN = wxID_HIGHEST + 1601
```

菜单结构（**顶层新增**，不改动既有项）：

```
&文件  &代理  &任务  &设置  [语言 / Language]  &帮助
                                 ├── ● 中文 (Chinese)      (radio, ID_MENU_LANGUAGE_ZH)
                                 └── ○ English (English)   (radio, ID_MENU_LANGUAGE_EN)
```

> 为什么放顶层而不是「设置」子菜单：默认语言是中文，英文用户首次启动面对「文件/代理/任务/设置/帮助」五个纯中文标题，**无法猜出哪个是 Settings**。把语言菜单做成顶层、且中文模式下标题为 `语言 / Language`，英文用户一眼可见——这是 v1.0 评审 P1「英文用户找不到入口」的修复。
>
> 该菜单是**新增项**，不触碰任何既有标题，因此不违反 FR-7「中文模式逐字一致」。两个 radio 项标签固定双语，任何语言下都能切回。

切换时序（UI 线程，同步执行）：

```
onMenuLanguage(event):
  1. target = (id == ID_MENU_LANGUAGE_ZH) ? ZhCN : EnUS
  2. if target == i18n::language(): 仅同步 radio 选中态; return
  3. bool saved = controller_->setUiLanguage(target)   // 权威副本 + 落盘，见下
  4. i18n::setLanguage(target)                         // 先落盘再切显示，失败提示用新语言
  5. config_ = controller_->getConfig()                // MainFrame 副本同步
  6. applyLanguage()                                   // 重译（见 7.6）
  7. syncLanguageMenu() / UpdateMenuStates()
  8. Logger::write(i18n::u8f(StrId::MainFrame_Log_LanguageChanged, {languageCode(target)}), INFO)
  9. if (!saved) wxMessageBox(...)                     // 每会话最多一次，见 §13
```

**权威配置副本与唯一写点**：本工程有两份 `AppConfig`——`MainFrame::config_` 与 `AppController::config_`，后者由 `AppController::saveConfig` 在 `configMutex_` 下更新并落盘。语言切换必须走后者，否则用户随后打开「配置…」（以 `controller_->getConfig()` 初始化）点确定，会把旧 `ui.language` 整份写回，语言被静默回滚。

`AppController` 新增：

```cpp
// src/ui/AppController.h
bool setUiLanguage(i18n::Language lang);   // 更新 config_.ui.language 并落盘；返回保存是否成功
```

实现要点：取 `configMutex_` → 复制 `config_` → 改 `ui.language` → 赋值回 `config_` → `ConfigReader::save(config::activeConfigPath(), config_)`。语言不参与 worker 数据路径，**不需要** `isRunning()` 守卫（该守卫在 `MainFrame::onMenuConfig`，针对数据库路径切换）。

**不重建菜单栏**：`MainFrame.cpp:845-846` 已记录 `initMenuBar()` 内直接 `SetMenuBar()` 在 wxMSW 3.2.5/MinGW 上会间歇性挂起。因此重译采用 `wxMenuBar::SetMenuLabel(pos, ...)` 与 `wxMenuItem::SetItemLabel(...)` 原地更新。

### 7.6 重译矩阵（已按源码核对）

`MainFrame::applyLanguage()` 调用顺序：

| 组件 | 重译内容 | 实现要点 |
|---|---|---|
| `MainFrame` | 6 个顶层菜单标题、19 个叶子项（含 `\tCtrl+X` 加速键）、语言 radio 选中态 | `SetMenuLabel` + `SetItemLabel`；加速键文本入表、不拼接 |
| 工具栏 `m_toolbar` | 11 个 `AddTool` 的 label + short help（含 `ID_TOOL_DETAIL_TOGGLE`） | `SetToolLabel(id, ...)` / `SetToolShortHelp(id, ...)`；`Freeze()`/`Thaw()` 防闪烁 |
| AUI 面板 | **无需动作**：仅 `detailPane` 有 Caption，且值为英文 `Proxy Details` | 保持 `Proxy Details` 不变（FR-7/非目标 8） |
| 状态栏 | field 0 恢复当前状态的 `Ready` / 本地化静态文本；field 4 保持路径 | `SetStatusText`；进行中的动态状态不回溯（见 §13） |
| 搜索区 | `wxChoice` 两项（代理/订阅）+ 占位提示 | `Clear()` + `Append()`；保持当前选择索引 |
| 底部视图 | 组标题 `监控 / Monitor` + 3 个选项（任务/任务进度/日志）+ 队列徽标前缀 | `wxRadioBox::SetLabel` 改组标题、`SetString(i, ...)` 改选项；徽标按当前语言重算 |
| 网络监控条 | **无需动作**：`netMonPanel_` 的 `Network OK` / `Disconnected` 已是英文且写死在 paint lambda | 不改（改则违反 FR-7） |
| `SubscriptionPanel` | 按钮、右键菜单、**7 个列头**（`#`/`启用`/`名称 ↕`/`有效 ↕`/`代理数 ↕`/`更新 ↕`/`ID ↕`）、空态 | `wxDataViewColumn::SetTitle`；` ↕` 后缀并入译文；更新前后保存/恢复列宽 |
| `ProxyListPanel` | 按钮、右键菜单、过滤项、空态 | **13 个列头已是英文**（Region/Latency/…），不做列头重译，但要断言中文模式下仍为英文 |
| `ProxyDetailPanel` | 字段标签、复制按钮 | 新增 `retranslate()` |
| `LogPanel` | 按钮（打开目录/清空/统计）、`Level:` 标签、过滤标签 | 下拉项重建后恢复选中级别 |
| `TaskMonitorPanel` | 「当前任务/排队/操作」标签、按钮、右键菜单 | 任务名渲染时查表（见 7.7） |
| `BatchQueuePanel` | 状态文本、百分比、待执行计数前缀 | 同 7.3 的 `{}` 迁移（原为 `剩余 %d　成功 %d　失败 %d`） |
| `ComponentListPanel` | 列头、按钮 | 主窗口存活实例；对话框内实例随对话框新建 |
| `TrayIcon` | 右键菜单（仅「退出」一项，`TrayIcon.cpp:78`）；tooltip 为 `validproxy`（不翻译） | **复用成员 `wxMenu*`**，逐项 `SetItemLabel`；不得每次切换新建 `wxMenu`（避免悬垂/双重释放） |
| `StandaloneFloatingWidget` | 标题、列头、右键菜单、模式切换项、启动/停止池按钮 | 按钮文案是**状态派生**（`running ? "停止池" : "启动池"`），`retranslate()` 必须复用 `running` 状态，不得无条件写死「启动池」 |
| `AppController` | 无控件持有；其状态字符串在生成时按当前语言输出 | 见 7.7 |

**必须保持的瞬态状态**（`TestLanguageSwitch` 逐项断言）：

| 控件 | 保持项 |
|---|---|
| 订阅/代理 `wxDataViewCtrl` | 滚动位置（首个可见行）、选中行、排序指示、过滤框已输入文本 |
| `LogPanel` | 滚动位置（是否处于尾部）、级别下拉选中项 |
| 底部 `wxRadioBox` | 当前页索引、焦点 |
| 搜索区 `wxChoice` | 当前选择索引 |
| 列头 | 每列宽度（切换前 `GetWidth`，切换后 `SetWidth`） |

约定：每个类新增 `void retranslate();`，构造函数末尾调用一次（用当前语言初始化），`MainFrame::applyLanguage()` 逐个调用。构造函数中原有的字面量改为 `i18n::wx(...)`。

### 7.7 日志与任务名

- 业务层日志：`Logger::write("中文", level)` → `Logger::write(i18n::u8(StrId::...), level)`；带参数用 `u8f`。
- 保持 `Logger` 接口不变（避免波及测试替身与统计逻辑），只在调用点替换文本来源。
- 日志行格式、级别、时间戳、`[Module]` 前缀**不翻译**（前缀是检索锚点）。
- `TaskType`：`const char* toString(TaskType)` 保留现有签名（测试与日志依赖），内部改为 `i18n::u8(StrId::Task_XXX)`；括号中的英文枚举名（如 `(TEST_ALL)`）保留，便于日志检索与排障。
- **自动任务子任务名**（v1.0 遗漏）：`AppController.cpp:4215 autoStepDisplayName()` 把中文步骤名写进 `TaskHandle::targetId` 后缓存，`TaskMonitorPanel.cpp:159` 直接渲染该字段，UI 刷新不会重算。改法：子任务行只保留 `AutoTaskStepType`（新增字段或替换 `targetId` 语义），显示名在 `TaskMonitorPanel` 渲染时查表；`taskId` 展示文本（原 `"auto-3-<中文步骤名>"`）同样改为渲染期拼接，不再把中文写入持久字段。
- 现有**英文**日志（如 `[MainFrame] Constructor begin`）保持英文，不做中文化（见非目标 1 与 §2.2 目标 3 的最小充分性）。
- 已渲染的历史日志不重译；`LogPanel` 只对**新增**行按当前语言显示。中英日志混排是已知且接受的观感（见 §17.4）。

### 7.8 对话框策略

- `ConfigDialog`、`SqlQueryManageDialog`、`AddPoolMemberDialog`、`BundleSelectorDialog`、`TestOnlineResultDialog`、`BundleChainEditor` 均为**按需创建**：创建时读取当前语言即可，无需活体重译。
- `ConfigDialog` 通过 `ShowModal()` 打开，模态期间无法点击主菜单，因此不存在「对话框打开时语言被菜单切走」的窗口。
- `wxMessageBox` 的标题与正文全部走表；按钮为系统提供（`wxOK/wxYES_NO`），语言由 Windows 决定，不纳入范围。
- `wxPropertyGrid` 的分类名、`wxNotebook` 页签在构造期一次性生成，随对话框创建取当前语言，不做运行期更新。

---

## 8. 文件级改动清单

### 8.1 新增

| 文件 | 内容 |
|---|---|
| `resources/i18n/strings.json` | 翻译资产（824 条） |
| `include/i18n/Language.h` | 语言枚举与解析 |
| `include/i18n/StringId.h` | 生成：`StrId` 枚举 |
| `include/i18n/I18n.h` | 纯 C++ 查询/格式化 API |
| `include/i18n/I18nWx.h` | wx 便捷封装（仅 UI 层） |
| `src/i18n/StringTable.cpp` | 生成：4 张表 + `static_assert` |
| `src/i18n/I18n.cpp` | 语言原子量、越界保护、`u8f` 实现 |
| `include/config/sections/UiConfigParser.h` | `ui.language` 解析（与其余 section 同目录） |
| `include/config/ActiveConfigPath.h` + `src/config/ActiveConfigPath.cpp` | 进程级生效配置路径 |
| `tests/test_i18n.cpp` | 表完整性与格式化单测 |
| `tests/test_config_ui_language.cpp` | 配置解析/序列化/生效路径单测 |
| `tests/ui/TestLanguageSwitch.cpp` | 菜单切换 UI 自动化 |
| `scripts/gen-i18n.ps1` | 生成器（含 `-Check`） |
| `scripts/i18n-allowlist.txt` | 允许保留中文字面量的文件 + 理由 + 到期复核日期（逐阶段清空） |
| `docs/specs/2026-11-18-Spec-BilingualUI-v1.1.md` | 本文档 |

> 不新增 `check-i18n-coverage.ps1`：扩展现有 `scripts/check-ui-literals.ps1`（见 §11），避免两套扫描脚本、两份 allowlist 互相打架。

### 8.2 修改（按字面量数量，合计 824 / 39 文件）

| 批次 | 文件（中文字面量数） | 小计 |
|---|---|---|
| P1 主窗口 | `MainFrame.cpp`(88)、`StandaloneFloatingWidget.cpp`(41)、`ProxyListPanel.cpp`(39)、`TaskMonitorPanel.cpp`(30)、`SubscriptionPanel.cpp`(24)、`LogPanel.cpp`(20)、`BatchQueuePanel.cpp`(10)、`ComponentListPanel.cpp`(7)、`RefreshCommands.cpp`(4)、`TrayIcon.cpp`(1)、`ProxyDetailPanel.cpp`(0，仅接 `retranslate`) | 264 |
| P2 对话框 | `ConfigDialog.cpp`(265)、`SqlQueryManageDialog.cpp`(73)、`AddPoolMemberDialog.cpp`(12)、`BundleSelectorDialog.cpp`(6)、`TestOnlineResultDialog.cpp`(5)、`BundleChainEditor.cpp`(5) | 366 |
| P3 日志/业务/入口 | `main_gui.cpp`(68)、`AppController.cpp`(36)、`CloudSyncService.cpp`(25)、`TaskType.cpp`(21)、`ConflictDetector.cpp`(8)、`ConfigGenerator.cpp`(5)、`ComponentResolver.cpp`(4)、`WebDavCredentials.cpp`(3)、`main_cli.cpp`(3)、`ProfileitemDAO.cpp`(3)、`StreamSettingsBuilder.cpp`(2)、`SchemaValidator.cpp`(2)、`SubitemDAO.cpp`(2)、`ProfileExItemDAO.cpp`(2)、`include/DatabaseHelper.h`(2)、`include/SubitemUpdaterV2.h`(1)、`WebDavClient.cpp`(1)、`SingBoxOutboundBuilderFactory.cpp`(1)、`OutboundBuilderFactory.cpp`(1)、`BundleChainResolver.cpp`(1)、`ProxyBatchTester.cpp`(1)、`FullConfigTemplateDAO.cpp`(1)、`AutoTaskManager.cpp`(1) | 194 |
| 配置与构建 | `ConfigReader.h`、`ConfigReader.cpp`、`ConfigJsonSerializer.cpp`、`ActiveConfigPath.cpp`、`main_gui.cpp`（设置生效路径）、`CMakeLists.txt`、`bin/config.json`、`bin/test_config.json` | — |
| 测试常量 | `tests/ui/UIIds.h`（新增英文常量块 + 语言菜单常量 + 云同步两项）、`tests/ui/TestMenuAction.cpp`（`leaves[]` 补两项） | — |
| 文档 | `docs/INDEX.md`、`docs/plans/project-plans-tracker.md`、`README.md`/`README.en.md`（仅新增一节） | — |

> 三批小计 264 + 366 + 194 = **824**，与 §2.1 总数一致。

---

## 9. 分阶段实施计划

> 每个阶段结束必须：构建 EXIT 0 + 相关单测通过 + `check-ui-literals.ps1 -Mode cjk` 无**新增**违规（allowlist 只减不增），再进入下一阶段。

### P0 — 基础设施

| 任务 | 产出 | 验证 |
|---|---|---|
| P0-0 | `check-ui-literals.ps1` 增加 `-Mode cjk` + `-AllowList`；生成基线 allowlist（39 文件，含理由） | 脚本在当前树退出码 0（全部命中 allowlist） |
| P0-1 | `Language.h` + `I18n.h/.cpp` + 空表骨架 + CMake 接入（`core` 库与 `test_i18n`） | 构建 EXIT 0 |
| P0-2 | `strings.json` 初始化（菜单/状态栏/查找框等种子 ~20 条）+ `gen-i18n.ps1` | 生成成功；`-Check` 通过 |
| P0-3 | `ui.language` 全链路（AppConfig/Parser/Serializer/`bin/*.json`） | `test_config_ui_language` 全绿 |
| P0-4 | `ActiveConfigPath` + `main_gui`/`main_cli` 发布 + `saveConfig`/`ConfigDialog` 改用 | 单测：`--config` 指向临时文件时保存落到该文件 |
| P0-5 | `AppController::setUiLanguage` + 顶层语言菜单 + radio 状态 + `applyLanguage()` 骨架（只重译菜单与状态栏） | 手动 + `TestLanguageSwitch`（先覆盖菜单与配置文件） |

### P1 — 主窗口与长生命周期 UI（264 条）

| 任务 | 文件 | 验证 |
|---|---|---|
| P1-1 | `MainFrame.cpp`：菜单/工具栏/搜索区/底部 radio（含组标题）/状态栏 | `[menu]` + `[layout]` + `[bottomview]` |
| P1-2 | `ProxyListPanel` + `ProxyDetailPanel`（按钮/右键菜单/空态；列头已是英文） | `[search]`、`[clear]`；列宽与滚动回归 |
| P1-3 | `SubscriptionPanel`（7 列头）+ `LogPanel` + `TaskMonitorPanel`（含子任务名）+ `BatchQueuePanel` | `[bottomview]`、`[taskledger]` |
| P1-4 | `StandaloneFloatingWidget` + `TrayIcon` | `[floatingwidget]` |
| P1-5 | `ComponentListPanel` + `RefreshCommands` | 手动巡检 |

### P2 — 对话框（366 条）

| 任务 | 文件 | 验证 |
|---|---|---|
| P2-1 | `ConfigDialog`（265 条；不含语言枚举） | 中/英各打开一次，无截断；配置往返单测 |
| P2-2 | `SqlQueryManageDialog`（73 条） | 手动巡检 |
| P2-3 | `AddPoolMemberDialog` / `BundleSelectorDialog` / `TestOnlineResultDialog` / `BundleChainEditor`（28 条） | 手动巡检 |

### P3 — 日志与业务层（194 条）

| 任务 | 文件 | 验证 |
|---|---|---|
| P3-1 | `main_gui`（启动/退出/错误框/单实例提示） | 启动日志中英各一次 |
| P3-2 | `AppController`（含 printf 风格迁移）+ `TaskType` + `CloudSyncService` | 实跑：更新订阅、批量测试、同步、云同步各一次；日志按模板反查无中文 |
| P3-3 | 配置/DAO/网络层残余（`ConflictDetector`、`ConfigGenerator`、DAO、Validator、`WebDavClient` 等） | `test_config_reader*` 全绿 |
| P3-4 | `main_cli`（3 条） | CLI 两种语言各跑 `-h`、无参测试 |

### P4 — 门禁、测试与文档

| 任务 | 产出 |
|---|---|
| P4-1 | allowlist 清空到 0（逐文件移除），`check-ui-literals.ps1 -Mode cjk` 全仓零违规 |
| P4-2 | `test_i18n` 完整性用例 + `UIIds.h` 与表一致性断言 |
| P4-3 | `TestLanguageSwitch` 全量（菜单/底部视图/列头/日志/配置文件/状态保持） |
| P4-4 | 接入 `scripts/build-and-test.bat`；`gen-i18n.ps1 -Check` 入门禁 |
| P4-5 | `docs/INDEX.md`、跟踪器、README（中英）「界面语言 / UI Language」新增一节 |

---

## 10. 测试计划

### 10.1 单元测试

| 用例 | 断言 |
|---|---|
| 表完整性 | 每条 `StrId` 的 zh/en 均非空；`en` 不含 CJK **除非** `fixed`；`{}` 数量一致；`\t` 后的加速键两语言相同 |
| 助记符唯一性 | 生成器对每个 `模块.区域` 前缀校验 `&` 字母不重复（zh、en 各一遍） |
| 语言解析 | `"zh-CN"/"ZH-cn"/"zh"/"zh_CN"` → ZhCN；`"en-US"/"en"/"en_us"` → EnUS；`"fr"`/`""`/缺失 → 默认 + 无异常 |
| `u8f` | 0/1/3 个占位符；参数不足保留 `{}`；参数多余忽略；参数含 `{}` 不二次替换 |
| 默认语言 | `i18n::language()` 初值 ZhCN；`setLanguage(EnUS)` 后 `w()/u8()` 返回英文 |
| UIIds 一致性 | `ids::MenuFileCaption` 等既有中文常量 == `i18n::w(StrId::...)`（ZhCN 模式）— 防止测试常量与表漂移 |
| 配置 | 缺省 zh-CN；`en-US` 解析；非法值回退 + **恰好一条** WARN；`save()` 往返保留 `ui.language`；`ui` 为错误类型时容错 |
| 生效路径 | `setActiveConfigPath(tmp)` 后 `saveConfig` 写入 tmp；未设置时回落 `exeDir/config.json` |
| 保存失败 | 只读路径下切换语言：运行期语言已切换，`setUiLanguage` 返回 false，日志仅一条 WARN，程序不崩溃 |

### 10.2 UI 自动化（`tests/ui`）

新增 `TestLanguageSwitch.cpp`（`[language]` 标签），**使用独立沙箱目录** `test/ui-sandbox-lang/`（复制模板配置），避免污染其余 UI 用例共用的 `test/ui-sandbox/config.json`。

1. 沙箱配置预置 `"ui":{"language":"zh-CN"}` → 启动 → 断言菜单为中文（复用现有常量）。
2. 断言顶层菜单数量为 6，且第 5 项为 `语言 / Language`。
3. 点击 `语言 / Language → English (English)` → 断言：6 个顶层菜单、17 个既有叶子 + 2 个语言项、底部 radio 组标题与 3 个选项、订阅列表 7 个列头、`Ready` 状态为英文。
4. 断言沙箱配置文件 `ui.language == "en-US"`，且 `bin/config.json` 的 mtime/内容未变。
5. 触发一次轻量刷新 → 断言日志面板新增行中不含 `strings.json` 里任何 `zh` 模板值（反查）。
6. 断言瞬态状态保持：订阅/代理列表首个可见行、选中行、列宽、底部当前页、搜索框文本。
7. 切回 `中文 (Chinese)` → 断言关键文案恢复中文，配置回到 `zh-CN`。
8. Teardown（RAII）：无论用例成败，恢复沙箱配置文件原始字节。

**工具栏不通过 UIA 断言**：`tests/ui/UIIds.h` 已记录 `wxAuiToolBar` 是单个自绘 `wxWindowNR` 节点，不暴露子 HWND/UIA 元素（"no measurable locator can exist for an individual tool"）。工具栏英文由 `test_i18n` 断言 `StrId::MainFrame_Toolbar_*` 的 en 值与 `UIIds.h` 新增英文常量一致 + 中英各一张截图存 `temp/`。

`UIIds.h` 新增：英文常量块（菜单/列头/底部视图/按钮）、语言菜单的 `语言 / Language`、`中文 (Chinese)`、`English (English)`、云同步两项（`从云端同步订阅` / `上传配置与订阅到云端`）的中英常量。不修改既有中文常量（FR-7）。测试选择器仍只在 `UIIds.h` 定义（沿用 2026-10-15 规范）。

### 10.3 手工巡检（记录到 PR 描述）

| 场景 | 中文 | 英文 |
|---|---|---|
| 启动 → 主窗口截图 | ✔ | ✔ |
| 工具栏 11 个按钮截图 | ✔ | ✔ |
| 配置对话框（全部 tab）截图 | ✔ | ✔ |
| 悬浮窗 + 托盘菜单 | ✔ | ✔ |
| 批量测试 / 更新 / 同步 / 云同步日志 | ✔ | ✔ |
| 100% / 125% / 150% DPI | ✔ | ✔ |

---

## 11. 质量门禁

| 门禁 | 规则 | 失败处理 |
|---|---|---|
| `scripts/check-ui-literals.ps1 -Mode cjk -Root src -AllowList scripts/i18n-allowlist.txt` | `src/**`（自动排除 `src/i18n/**`）不得出现含 CJK 的字符串字面量；allowlist 每项需写理由与到期复核日期，只减不增 | 退出码 1 |
| `scripts/check-ui-literals.ps1`（既有默认模式） | `src/ui` 窄字节非 ASCII 零违规（保持不变） | 退出码 1 |
| `scripts/gen-i18n.ps1 -Check` | 生成文件与 `strings.json` 同步 | 退出码 1 |
| `UiStringEncodingTest`（`tests/test_ui_string_encoding.cpp`） | 保持既有扫描范围与断言 | 失败 |
| `test_i18n` | 表完整性/占位符/助记符 | 失败 |
| 全量 `ctest` | 与基线一致（当前 74/76；既有失败 `LogStatisticsEventTest`、flaky `UI_FLOATINGWIDGET`） | 新增失败即阻塞 |
| `scripts/build-and-test.bat` | UI 变更必跑 | 失败 |

**门禁能力边界（显式记录）**：`-Mode cjk` 只能发现「中文写进了源码」，发现不了「新增的纯英文文案没走表」。该方向由评审检查单兜底：任何新增 UI 文本必须同时新增 `StrId` 并出现在 `ui.language` 两条路径中。`UiStringEncodingTest` 已对 `L"..."` 前缀字面量免检，因此生成的 `StringTable.cpp` 不会误报，无需额外白名单。

---

## 12. 验收标准

| ID | 标准 | 证据 |
|---|---|---|
| AC-1 | 源码零未登记中文字面量 | `check-ui-literals.ps1 -Mode cjk` 退出码 0（allowlist 为空） |
| AC-2 | 菜单/底部视图/列头/日志/状态栏不重启即切换 | `TestLanguageSwitch` 通过 + 截图 |
| AC-3 | 语言写入生效配置文件并重启生效 | 单测 + 手工重启（沙箱文件断言） |
| AC-4 | 英文模式日志中**不再出现登记过的中文模板**（用户数据与第三方原文不含在内） | 对 `strings.json` 的 `zh` 值反查日志文件 |
| AC-5 | 中文模式与当前版本一致（除新增语言菜单） | 既有 UI 常量未改 + 全部 UI 用例通过 |
| AC-6 | `test_i18n` / `test_config_ui_language` 全绿 | ctest 输出 |
| AC-7 | 无 `auto`、无新增编译警告 | `rg -n "\bauto\b" src include`（类型推导上下文）+ 构建输出 |
| AC-8 | 对话框与主窗口在 125%/150% DPI 下无截断/重叠 | 截图 |
| AC-9 | 工具栏 11 个标签/tooltip 英文一致 | `test_i18n` 表断言 + 截图（UIA 不可用） |
| AC-10 | UI 测试不改写生产配置 | `bin/config.json` 前后哈希一致 |

---

## 13. 边界条件与错误处理

| 场景 | 行为 |
|---|---|
| 旧 `config.json` 无 `ui` 节 | 静默使用 `zh-CN`，保存后补写 `ui` 节 |
| `ui.language` 为未知值（如 `"ja-JP"`） | 归一化 `zh-CN`，**恰好一条** WARN（含原值），由 `UiConfigParser` 唯一产生 |
| 语言切换时正在运行批量测试 | 允许切换（语言不参与 worker 数据路径）；`saveConfig` 在 `configMutex_` 下安全执行；日志切换点之后的行用新语言 |
| 语言切换时正在写日志 | 单条消息语言一致（原子读），不会半中半英 |
| 切换后未刷新的运行中任务 | 任务名在渲染时查表，**下一次绘制即为新语言**（不需要等状态迁移）；子任务名不再缓存中文 |
| `config.json` 只读/被占用 | 运行期语言生效；`setUiLanguage` 返回 false → 一次 `wxMessageBox`（标题/正文入表）+ 一条 WARN，每会话最多一次 |
| 字符串表缺项（开发期） | 编译期 `static_assert` 失败；若绕过则 Debug `assert`，Release 返回 `(missing translation)` + 一条 WARN |
| 英文文案过长导致列头截断 | 保留用户列宽；为订阅面板 5 个列头设置按语言的最小宽度；DPI 巡检 |
| 切换语言时 AUI 布局抖动 | `Freeze()`/`Thaw()`；不重建 pane，仅 `SetMenuLabel`/`SetItemLabel` |
| 悬浮窗在切换时可见 | `retranslate()` 复用 `running` 状态，按钮文案保持「启动池/停止池」语义 |
| 主窗口标题 | 两语言均保持 `validproxy - Proxy Manager` |

---

## 14. 风险与缓解

| ID | 风险 | 影响 | 缓解 |
|---|---|---|---|
| R1 | `wxMenuItem::SetItemLabel` 在 wxMSW 上重新解析 `\t` 加速键，可能丢失/重复注册 | 快捷键失效 | `TestMenuAction` 逐个验证 8 个已知快捷键；加速键不参与翻译；失败则回退「重建 `wxMenu` 对象但复用 `wxMenuBar`」 |
| R2 | `wxRadioBox::SetString`/`SetLabel` 在 MSW 上重建子控件（`wxCompositeWindow<wxStaticBox>`） | 事件/焦点丢失、组标题不刷新 | 切换后恢复页索引与焦点；组标题先用 `SetLabel`，实测不生效则改由 `wxStaticBox` 父控件设置；`[bottomview]` 覆盖 |
| R3 | `wxDataViewColumn::SetTitle` 改列头可能重置列宽/对齐（`SubscriptionPanel` 6 列 + 切换列） | 布局回归 | 切换前后 `GetWidth`/`SetWidth`；新增列宽回归断言（`ProxyListPanel` 只断言「中文模式下仍为英文」） |
| R4 | 824 条替换引入编译错误或漏改 | 进度风险 | 按文件小批次提交，每批构建；allowlist 只减不增；生成器从修正后的扫描结果生成初始资产 |
| R5 | 英文文案更长导致对话框/工具栏拥挤 | 可用性 | 英文翻译遵循「短标签优先」（`Dedup` 而非 `Remove Duplicate Proxies`）；DPI 巡检；必要时调整最小宽度 |
| R6 | 运行期切换与 worker 线程竞争 | 数据竞争 | 仅原子语言索引；表只读；配置写入统一走 `AppController::saveConfig`（`configMutex_`） |
| R7 | 语言切换触发的重译在 UI 线程耗时（约 800 次 SetLabel） | 卡顿 | 顶层 `Freeze()`/`Thaw()`；实测目标 < 150 ms；超 200 ms 时改为 `CallAfter` 分批重译 |
| R8 | 测试常量与翻译表漂移 | 测试假绿/假红 | `test_i18n` 断言 `UIIds.h` zh 常量 == 表值 |
| R9 | 生成文件被手改后与 JSON 不一致 | 维护混乱 | `gen-i18n.ps1 -Check` 入门禁；生成文件头部写「自动生成，勿手改」 |
| R10 | 新增纯英文文案绕过表 | 双语腐化 | 评审检查单（门禁只能查 CJK，见 §11 边界） |
| R11 | 生效配置路径改造影响既有「配置…」保存位置 | 配置写到意外文件 | 普通运行时三者本就同为 `exeDir/config.json`；新增单测覆盖 `--config` 分支；`ConfigDialog` 与 `saveConfig` 统一走 `activeConfigPath()` |
| R12 | 新增语言菜单改变菜单栏索引 | 依赖索引的旧断言失效 | 已核：`tests/ui` 通过 UIA Name / `UIIds.h` 定位，无索引依赖；仍保留 `TestMenuAction` 全量回归 |
| R13 | 语言测试污染共享沙箱配置 | 后续 UI 用例连锁假失败 | 独立沙箱目录 + RAII 还原（见 §10.2） |

---

## 15. 发布与迁移

1. 版本号：作为 minor 特性随下一版本发布（`1.7.0`）。
2. 迁移：无数据库变更；`config.json` 首次保存自动补 `ui` 节。
3. README（中/英）新增一节「界面语言 / UI Language」：菜单位置、配置文件键。
4. 发布说明注明：中文为默认；英文覆盖 UI 与日志模板；历史日志不重译。
5. 打包口径不变（见 `docs/2026-11-17-Reference-ReleasePackaging-v1.0.md`）：`strings.json` **不随包**，全部编译进 exe。该目录仅作**编译期资产**，不是运行时可替换资源（避免误解，在 `resources/i18n/README.md` 写一行说明）。

---

## 16. 工作量估算

| 阶段 | 内容 | 人日 |
|---|---|---|
| P0 | 门禁基线 + 基础设施 + 配置 + 生效路径 + 菜单 + 骨架 | 1.5 |
| P1 | 主窗口与长生命周期 UI（264 条） | 2.5 |
| P2 | 对话框（366 条） | 3.0 |
| P3 | 日志/业务/入口（194 条）+ 翻译校对 | 1.5 |
| P4a | allowlist 清零 + 门禁接入 | 0.5 |
| P4b | `test_i18n` + `TestLanguageSwitch` + 文档 | 1.5 |
| 缓冲 | R1/R3/R5 触发的返工 | 0.5 |
| 合计 | | **11.0 人日** |

> v1.0 的 7.5 人日基于漏计 123 条字面量的基线（701 vs 824），且把 P4 的脚本、UI 自动化与文档压进 1.0 人日，评审判定失真，已重估。

---

## 17. 评审遗留问题（需 owner 决策）

1. **即时切换 vs 提示重启**：R1–R3、R7 与 §7.6 的全部复杂度都来自「运行期即时切换」。若接受「切换后提示重启」，风险面可收敛为「一个设置项 + 启动时读一次表」，工作量约减半。当前方案按用户「选择英文时把各种中文提示切换为英文」的原始表述选择即时切换；如需改为重启生效，请明确。
2. **英文模式是否也需要 UI 自动化**：当前只有 `TestLanguageSwitch` 覆盖 4 个表面，其余 14 个 UI 用例仍假设中文。是否接受该覆盖度。
3. **日志混排**：英文模式下新日志为英文、历史日志为中文，同面板并存。是否需要视觉分隔（分隔线/时间标记）。
4. **首次启动语言**：维持 `zh-CN`（不探测 Windows 用户界面语言），可发现性由顶层双语菜单保证。是否接受。
5. **订阅面板列名**：`名称/有效/代理数/更新/ID` 的英文译法（`Name/Valid/Proxies/Updated/ID`）是否需要在 PR 中作为可评审清单单独确认。

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
    { "id": "MainFrame.Menu.CloudPull",  "zh": "从云端同步订阅", "en": "Sync Subscriptions from Cloud" },
    { "id": "MainFrame.Menu.CloudPush",  "zh": "上传配置与订阅到云端", "en": "Upload Config and Subscriptions" },
    { "id": "MainFrame.Menu.Language",   "zh": "语言 / Language", "en": "Language", "fixed": true },
    { "id": "MainFrame.Menu.LangZh",     "zh": "中文 (Chinese)",   "en": "中文 (Chinese)", "fixed": true },
    { "id": "MainFrame.Menu.LangEn",     "zh": "English (English)", "en": "English (English)", "fixed": true },
    { "id": "MainFrame.Toolbar.Test",    "zh": "批量测试", "en": "Test All" },
    { "id": "MainFrame.Toolbar.TestTip", "zh": "测试全部代理", "en": "Test all proxies" },
    { "id": "MainFrame.Status.Ready",    "zh": "Ready", "en": "Ready", "note": "现状即英文" },
    { "id": "MainFrame.BottomView.Group", "zh": "监控", "en": "Monitor", "note": "wxRadioBox 组标题，同时是 UI 测试定位锚点" },
    { "id": "MainFrame.BottomView.Tasks", "zh": "任务", "en": "Tasks" },
    { "id": "MainFrame.BottomView.Progress", "zh": "任务进度", "en": "Progress" },
    { "id": "MainFrame.BottomView.Log",  "zh": "日志", "en": "Log" },
    { "id": "Subscription.Column.Name",  "zh": "名称 ↕",   "en": "Name ↕" },
    { "id": "Subscription.Column.Enabled", "zh": "启用",   "en": "On" },
    { "id": "MainFrame.Log.LanguageChanged", "zh": "界面语言已切换为中文", "en": "UI language switched to English" },
    { "id": "Log.Batch.Started",         "zh": "批量测试开始：共 {} 个代理", "en": "Batch test started: {} proxies" }
  ]
}
```

## 附录 B：`StrId` 命名规范

```
id:   <Module>.<Area>.<Name>          → 枚举: StrId::<Module>_<Area>_<Name>
```

- 模块（`Module`）：`MainFrame` / `ProxyList` / `ProxyDetail` / `Subscription` / `LogPanel` / `TaskMonitor` / `BatchQueue` / `ComponentList` / `ConfigDialog` / `SqlDialog` / `PoolMemberDialog` / `BundleDialog` / `TestResultDialog` / `Tray` / `FloatingWidget` / `AppController` / `CloudSync` / `Task` / `Gui` / `Cli` / `Config` / `Db` / `Net` / `Common`
- 区域（`Area`）：`Menu` / `Toolbar` / `Status` / `Button` / `Column` / `Label` / `Message` / `Log` / `Error` / `Tooltip` / `Empty` / `Radio` / `Check` / `Dialog` / `Tab`
- 名称（`Name`）：语义化 `UpperCamelCase`；**不要**用序号（`Text1`）。
- 共享策略：同模块内语义完全相同的文案可共用一个 ID；跨模块默认不共享（翻译上下文不同）。固定双语项用 `"fixed": true`；两语言值相同的现状英文项在 `note` 注明。
- 同一控件 ID 在不同用途下**不得**共用一条文案：`ID_TOOL_CANCEL` 同时被菜单「取消任务」（`MainFrame.Menu.CancelTask`）与工具栏「终止当前测试」（`MainFrame.Toolbar.StopTest`）使用，必须拆成两条 ID。

## 附录 C：日志英文化示例

| 位置 | 改前 | 改后（zh） | 改后（en） |
|---|---|---|---|
| `TaskType.cpp` | `"批量测试 (TEST_ALL)"` | `u8(StrId::Task_TestAll)` → `批量测试 (TEST_ALL)` | `Batch Test (TEST_ALL)` |
| `AppController.cpp` | `"已加入队列: " + name` | `u8f(StrId::AppController_Log_Enqueued, {name})` | `Queued: {name}` |
| `CloudSyncService.cpp` | `"云端配置已合并"` | `u8(StrId::CloudSync_Log_Merged)` | `Cloud config merged` |
| `main_gui.cpp` | `"配置加载失败：..."` | `u8f(StrId::Gui_Error_ConfigLoadFailed, {err})` | `Failed to load config: {err}` |
| `MainFrame.cpp` | `L"导出分享链接失败。"` | `wx(StrId::MainFrame_Error_ExportFailed)` | `Failed to export share links.` |
| `MainFrame.cpp` | `wxString::Format(L"已加入队列：自动任务，前方还有 %zu 个任务", ahead)` | `wxf(StrId::MainFrame_Status_QueueAhead, {std::to_string(ahead)})` | `Queued: auto task, {} ahead` |
| `BatchQueuePanel.cpp` | `Format(u8"剩余 %d　成功 %d　失败 %d")` | `wxf(StrId::BatchQueue_Status_Summary, {...})` | `Remaining {} · OK {} · Failed {}` |

## 附录 D：评审修订记录（v1.0 → v1.1）

独立评审（5 位只读评审员：coherence / feasibility / scope-guardian / adversarial / design-lens）共 44 条发现，其中 **P1 9 条**。已采纳并落入本版的修订：

| # | 来源 | 问题 | 本版修订 |
|---|---|---|---|
| 1 | adversarial | 盘点脚本跨行吞串，真实 824 条（ConfigDialog 实际 265） | §2.1 重算；§8.2 重排 P1/P2/P3 = 264/366/194；§16 重估 11.0 人日 |
| 2 | feasibility / adversarial / coherence | 菜单实为 17 个叶子（云端两项漏计），不是 15 | §2.3、§7.6、§10.2 改 17 + 2；`UIIds.h` 补云同步两项 |
| 3 | feasibility / adversarial | 语言只写 `MainFrame::config_`，后续「配置…」保存会静默回滚 | 新增 `AppController::setUiLanguage`，唯一写点（§7.5） |
| 4 | feasibility | 配置路径三套且都落到 `bin/config.json`，UI 沙箱断言必红 | 新增 `ActiveConfigPath`，`-c/--config` 贯通（§7.4.1、FR-9、AC-10） |
| 5 | feasibility | `wxAuiToolBar` 无 UIA 子元素，工具栏断言不可实现 | §10.2 改为表断言 + 截图；新增 AC-9 |
| 6 | feasibility / adversarial | `ProxyListPanel` 是 13 列**已是英文**；`SubscriptionPanel` 才是中文列头 | §7.6 修正；R3 改为 `wxDataViewColumn::SetTitle` |
| 7 | coherence | 「en 不含 CJK」会判死固定双语的语言项 | `"fixed": true` 豁免（§7.2、§10.1） |
| 8 | coherence / adversarial | `kZhU8` 声明为 `const wchar_t*` 却赋 `u8"..."`，编译不过 | 窄表改 `const char* const[]`（§7.2） |
| 9 | coherence | `static_assert` 比较 `size_t`/`int` 新增 `-Wsign-compare` | 显式 `static_cast<int>`（§7.2、AC-7） |
| 10 | scope / design / adversarial | `VALIDPROXY_LANG` 是范围外第三层优先级，导致菜单选择重启后静默失效 | **删除**（FR-10、§7.1、§13、§15 全部移除） |
| 11 | scope | 配置对话框语言枚举与菜单重复，且是回滚路径 | **删除**（FR-5、§7.4、P2-1） |
| 12 | scope / adversarial | AC-4「日志零 CJK」与「不翻译用户数据」矛盾，必假红 | 改为「无登记过的中文模板」+ 反查判据（AC-4、§2.3） |
| 13 | scope / design | 门禁脚本排在 P4，却要求 P0 起每阶段执行 | 提前为 P0-0，allowlist 只减不增（§9） |
| 14 | scope | 新脚本与 `check-ui-literals.ps1` 职责重叠、两份 allowlist | 合并为 `-Mode cjk` + `-AllowList`（§11） |
| 15 | scope / feasibility | `Ready` 的 zh 写成「就绪」，违反中文逐字一致 | zh 值固定 `Ready`（§7.2、附录 A） |
| 16 | scope / design | README 同时是「非目标」与「交付项」 | §5.2 改为「不翻译正文，仅新增一节」 |
| 17 | design-lens | 中文模式下英文用户找不到语言入口 | 语言菜单改**顶层**且标题双语（FR-2、§7.5） |
| 18 | design-lens | 保存失败只靠状态栏（200px 截断且会被覆盖） | 改为一次 `wxMessageBox` + WARN（§13、FR-4） |
| 19 | design-lens | 重译后滚动/选中/排序状态无约定 | §7.6 新增「必须保持的瞬态状态」表 + 用例断言 |
| 20 | design-lens / adversarial | 运行中任务名切换时机自相矛盾；子任务名缓存中文 | 统一为「渲染时查表」；`autoStepDisplayName` 改存枚举（§7.7） |
| 21 | design-lens | 助记符唯一性无门禁 | 生成器新增同前缀 `&` 唯一校验（§7.2、§10.1） |
| 22 | coherence | `main_cli` 11 条未计入任何批次（实为 3 条） | 补入 P3 并修正数字（§8.2、P3-4） |
| 23 | coherence | §7.4 示例用裸 `L"界面语言"`，违反自定门禁 | 该示例随「删除对话框语言项」一并移除 |
| 24 | coherence | 附录 C `StrId::App_Queue.Enqueued` 不是合法标识符 | 统一为 `Module_Area_Name` 并修正全部示例（附录 B/C） |
| 25 | coherence | §5.6「不改控件层级」与 FR-2 冲突 | 改为「不改**既有**项；本期唯一新增为语言菜单」（§5.6） |
| 26 | coherence | 列宽风险误引 R5，应为 R3 | 已修正（§7.6、§14） |
| 27 | coherence / feasibility | AUI 标题清单与源码不符（只 `detailPane` 有 Caption 且为英文）；监控条已英文 | §7.6 改为「无需动作」，避免把英文改中文 |
| 28 | feasibility | 底部 `wxRadioBox` 组标题「监控」漏项 | §7.6 补组标题 + `SetLabel` 方案（R2） |
| 29 | feasibility | `ConfigValidator` 无法写回归一化值 | 归一化只在 `UiConfigParser`（§7.4、§13） |
| 30 | feasibility / adversarial | UI 沙箱配置被语言用例污染 | 独立沙箱目录 + RAII 还原（§10.2、R13） |
| 31 | adversarial | 26 处 printf 风格 `Format` 未纳入迁移规则 | 新增 §7.3 迁移小节（含 U+3000） |
| 32 | adversarial | 门禁只认 CJK，纯英文新文案可静默通过 | §11 显式记录能力边界 + 评审检查单（R10） |
| 33 | adversarial | P4-4 把 `check-ui-literals` 扩到全 `src` 不可达（124 处非 UI 窄字面量） | 改为 `-Mode cjk` 扫 CJK 字面量 + 既有默认模式保持 `src/ui`（§11） |
| 34 | adversarial | 托盘「显示主界面」不存在；重设菜单指针有双重释放风险；悬浮窗按钮是状态派生 | §7.6 三行修正 |
| 35 | adversarial | `temp/wx333-full` 不存在，`wxRadioBox::SetString` 行为无法从源码核实 | R2 标注为「需实测，失败则改 `wxStaticBox`」 |
| 36 | adversarial | `main_cli` 的可选 `--lang` 使 P3 完成判定模糊 | 明确移出本期（§5.7、§8.2） |

**未采纳/降级**：无。所有 P0/P1 发现均已修复；P2/P3 中属「主观偏好」的两条（重命名 `Log.Language.Changed` 的模块归属、AUI 行措辞）已并入 #24/#27。
