## 1. 文档信息

| 项 | 内容 |
|---|---|
| 标题 | 双语言版本方案（中文 / English） |
| 类型 | Spec（实施方案） |
| 模块 | `i18n`（新增）、`src/ui/**`、`src/config/**`、日志层、`tests/**`、`scripts/**` |
| 版本 | v1.3（v1.2 owner 决策定稿 + 2026-11-20 用户追加项；修订记录见附录 D） |
| 日期 | 2026-11-18 |
| 分支 | `Bilingual-version`（worktree：`E:\eclipse_workspace\multiple_thread_validproxy-bilingual`） |
| 状态 | 已定稿，待实施 |
| 上游约束 | `AGENTS.md` §2.1（C++17 / 禁用 `auto`）、`docs/specs/2026-10-15-Spec-UiMenuChineseI18n-v1.0.md`（菜单文案集中登记于 `tests/ui/UIIds.h`）、`docs/bugfix/2026-11-17-Bugfix-Win7-UIEncoding-MenuAssertCrash-v1.0.md`（宽字面量规则） |

---

## 2. 背景与目标

### 2.1 背景

当前程序**只有中文一套文案**：菜单、工具栏、面板、对话框、托盘、悬浮窗、任务名、状态栏、日志均为硬编码中文字面量，少量位置混有英文（如状态栏 `Ready`、`Loaded subscription:`、`Network OK`）。2026-11-17 的 Win7 修复已把所有 UI 非 ASCII 字面量统一为 `L"..."` 宽字面量，并建立 `scripts/check-ui-literals.ps1` + `UiStringEncodingTest` 门禁——这为引入多语言提供了干净前提（UI 文本已经集中在可枚举的字面量上）。

**现状扫描**（口径 = `scripts/check-ui-literals.ps1` 的单遍词法扫描，与 `UiStringEncodingTest` 同构；行注释 / 块注释 / 原始字符串 `R"delim(...)delim"` 均跳过）：

| 范围 | 含中文的字符串字面量 | 文件数 |
|---|---|---|
| `src/**`（含 `src/ui`） | 820 | 36 |
| `include/**` | 3 | 2 |
| **合计** | **823** | **38** |

> 口径演进：v1.0 用 `split("//")[0]` 截断 → 701/36；v1.1 用字符串感知剥离 → 824/39；v1.2 修正重复计数 → **823/38**（见附录 D.3：多出的 1 条是 `src/FullConfigTemplateDAO.cpp:669-671` 的普通注释，因旧脚本不识别原始字符串 `R"JSON(...)JSON"` 内的裸引号而失步误计）。

Top 文件：`src/ui/ConfigDialog.cpp` 265、`src/ui/MainFrame.cpp` 88、`src/ui/SqlQueryManageDialog.cpp` 73、`src/main_gui.cpp` 68、`src/ui/StandaloneFloatingWidget.cpp` 41、`src/ui/ProxyListPanel.cpp` 39、`src/ui/AppController.cpp` 36、`src/ui/TaskMonitorPanel.cpp` 30、`src/service/CloudSyncService.cpp` 25、`src/ui/SubscriptionPanel.cpp` 24、`src/task/TaskType.cpp` 21、`src/ui/LogPanel.cpp` 20。

### 2.2 目标

1. 提供中文 / 英文两套界面；语言在**启动时**确定，**切换后重启生效**。
2. 语言通过**菜单**直接选择，并**写入配置文件**（`config.json` 的 `ui.language`），下次启动沿用。
3. 选择英文后（重启），**菜单、工具栏、按钮、面板标题/列名、对话框、托盘与悬浮窗、任务名、状态栏、日志（含文件日志与日志面板）** 中的中文提示为英文。
4. 中文为默认语言；**旧配置文件（无 `ui` 节）保持原行为**，不产生行为回归。
5. 建立可维护的字符串资产与门禁，使「新增中文字面量」在脚本层被发现，避免双语能力随时间腐化。

### 2.3 成功定义（可验证）

- 配置文件 `ui.language = "en-US"` 启动后，主窗口全部可见文案为英文：**6 个顶层菜单**、**19 个叶子项**（17 既有 + 2 语言项）、工具栏 11 个按钮标签与 tooltip、底部 `wxRadioBox` 组标题与 3 个单选项、订阅列表 7 个列头、日志面板标签、状态栏。
- 在中文界面点击「语言 / Language → English (English)」后：配置文件写入 `en-US`，弹出一次「重启后生效」提示，**当前界面保持中文不变**（不静默切换、不假装已切换）。
- 中文模式下的现有 UI 自动化用例（`tests/ui/**`）全部保持通过。
- 英文模式下界面由人工截图验收（owner 决策：英文模式**不跑** UI 自动化，见 §5.9）。

### 2.4 owner 已确认的决策（2026-11-18）

| # | 决策 | 对本方案的影响 |
|---|---|---|
| D1 | **切换后重启生效**，不做运行期即时重译 | 删除全部 `retranslate()`、原子语言热切换、菜单/列宽/选中态保持、`SetItemLabel` 加速键重注册等设计；风险与工作量大幅下降 |
| D2 | **英文模式不跑 UI 自动化** | 英文侧验收 = `test_i18n` 表断言 + 人工截图；UI 自动化只覆盖中文模式下的菜单接线与配置写入 |
| D3 | 日志混排需要视觉分隔 | 实现为「会话语言横幅 + 跨会话语言变更分隔线」（见 §7.7.1；重启模型下同一面板内不会混排，详见该节说明） |
| D4 | 首次启动维持 `zh-CN`，不探测 Windows 界面语言 | 可发现性由顶层双语菜单标题 `语言 / Language` 保证 |
| D5 | 订阅列表列名英文为 `Name / ValidProxies / TotalProxies / UpdatedTime / ID` | 见 §7.6 与附录 A |

---

## 3. 术语

| 术语 | 含义 |
|---|---|
| 字符串资产 | `resources/i18n/strings.json`，唯一翻译数据源（id / zh / en / note / fixed） |
| 生成表 | 由资产生成的 `include/i18n/StringId.h` 与 `src/i18n/StringTable.cpp`（入库） |
| 固定双语项 | 两个语言下值相同的表项（语言名本身），用 `"fixed": true` 标记 |
| 语言来源 | 仅两个：`config.ui.language`、内置默认 `zh-CN` |
| 待生效语言 | 已写入配置但需重启才生效的语言 |

---

## 4. 需求

| ID | 需求 | 验收方式 |
|---|---|---|
| FR-1 | 配置新增 `ui.language`，取值 `"zh-CN"` / `"en-US"`（大小写不敏感，兼容 `zh`/`en`/`zh_CN`/`en_US`），缺省 `"zh-CN"` | 单测：默认值、别名、非法值回退 |
| FR-2 | 菜单栏新增**顶层**「语言 / Language」菜单（`zh` 值为 `语言 / Language`，`en` 值为 `Language`），内含两个单选（radio）项：`中文 (Chinese)`、`English (English)`；选中态反映**配置中的语言** | UI 自动化（中文模式） |
| FR-3 | 点击菜单项后**只写配置**，弹出一次「已保存，重启后生效」提示；当前界面文案不变 | UI 自动化（配置写入 + 提示框）+ 人工 |
| FR-4 | 保存失败时保留配置原值并**不**改变 radio 选中态，弹一次失败提示 + 一条 WARN | 单测（保存失败分支）+ 日志检查 |
| FR-5 | 英文模式下，日志（`Logger` 输出的文件/控制台/日志面板）中的中文提示为英文 | 单测 + 实跑任务反查 |
| FR-6 | 任务类型显示名（`TaskType`）、自动任务子任务名、状态栏提示、消息框、tooltip、列头、托盘菜单、悬浮窗标签在启动时按当前语言生成 | 人工巡检 + `test_i18n` |
| FR-7 | 中文模式输出与当前版本**逐字一致**（除新增的顶层语言菜单与其两个子项外） | 现有 UI 测试常量不改仍通过 |
| FR-8 | 源码中不再出现「未登记的中文字面量」；门禁脚本可检出 | `scripts/check-ui-literals.ps1 -Mode cjk` 退出码 0 |
| FR-9 | 语言持久化必须写入本次进程实际使用的配置文件（`-c/--config` 优先），不得写死到 `bin/config.json` | 单测 + UI 沙箱断言 |
| FR-10 | 每次启动在日志中写入**语言横幅**；若上一次会话语言不同，额外写入一条语言变更分隔线 | 单测 + 日志文件检查 |

**已删除的 v1.0/v1.1 需求**：「配置对话框界面语言枚举」（与菜单入口重复，且引入第二条写路径与回滚风险）、「`VALIDPROXY_LANG` 环境变量」（用户未要求，且会让「菜单选了英文、重启又变中文」静默发生）、「运行期即时重译」（被 D1 取代）。

---

## 5. 非目标（明确不做）

1. 不翻译：代码注释、标识符、配置键名、数据库内容（订阅备注/代理备注）、Xray/sing-box 自身输出、Windows 系统对话框、第三方库错误原文。
2. 不翻译 `docs/**` 与 README 正文；P4 仅在 README 中**新增一节**说明语言入口与配置键。
3. 不引入第三方 i18n 依赖（gettext / wxTranslations / ICU），不引入 `.po/.mo` 构建链。
4. **不做运行期语言切换与重译**（D1）：不新增 `retranslate()`，不在切换时重建/重设任何控件文本，不保存/恢复滚动、选中、列宽等瞬态状态。
5. 不改主窗口标题 `validproxy - Proxy Manager`（多套 UI 测试依赖，见 `2026-10-15-Spec-UiMenuChineseI18n-v1.0.md` §3.3）。
6. 不改变**既有**菜单项 ID、快捷键、助记符、AUI 布局与控件层级；本期唯一新增控件为顶层语言菜单及其两个 radio 项。
7. CLI 的参数名与**机器可解析输出**（JSON、导出文本、退出码）不变；CLI 控制台的人可读提示随语言切换（同一套表）。不新增 `--lang` 参数。
8. 不把现有英文文案（`Ready`、`Loading data...`、`Network OK`、`Region/Latency/...` 列头）改成中文。
9. **英文模式不做 UI 自动化**（D2）：不新增英文 UI 用例，不改造现有中文用例使其双语化；英文侧由 `test_i18n` 表断言 + 人工截图验收。
10. 不做「保存后自动重启进程」（D1 只要求重启生效）：提示框为单按钮，自动重启列为可选增强（§17.3）。

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
        ├── src/ui/**          菜单/工具栏/面板/对话框/托盘/悬浮窗（构造期取文案）
        ├── src/**             日志、任务名、状态消息
        ├── src/config/**      ui.language 的解析/序列化
        └── tests/**           单测（中文 UI 自动化仅验证菜单接线）
```

关键取舍：

- **不用 `wxTranslations`**：wx 的 catalog 在 `wxApp::Init` 前加载，运行期切换仍要手工重译；且引入 `.po/.mo` 构建链与 `msgfmt` 依赖（MinGW 工具链未保证）。自建表在编译期即可校验完整性，且**纯 C++ 无 wx 依赖**，可被 `UITests`（不包含 wx 头）直接链接复用。
- **不按「中文原文」做运行时查表**：以 `StrId` 枚举为键，缺项是编译错误（`static_assert` 表长）+ 单测兜底。
- **语言只在启动时确定**（D1）：`i18n::setLanguage()` 仅在 `main_gui`/`main_cli` 启动路径与单测中调用一次；此后全进程只读。这消除了热切换的全部并发与控件副作用风险。

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

启动解析（`main_gui.cpp` / `main_cli.cpp` 共用 `resolveStartupLanguage(const AppConfig&)`）：

1. `config.ui.language`（缺失 → 默认，不告警；非法值 → 一条 WARN（含原值）+ 默认）；
2. `Language::ZhCN`。

**没有环境变量覆盖层**。首次启动固定 `zh-CN`（D4），英文用户的可发现性由顶层「语言 / Language」菜单保证。

调用时机：

- `main_gui`：`ConfigReader::load()` 成功后、构造 `MainFrame` 之前 `i18n::setLanguage(...)`。理由：`src/ConfigReader.cpp` 无中文字面量，load 之前的诊断日志本来就是英文，因此不存在「启动前几行中文」的问题。
- `main_cli`：各命令在 `ConfigReader::load()` 成功后、输出提示前调用。
- **不在任何运行期路径调用** `i18n::setLanguage()`（除单测）。

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
    { "id": "MainFrame.Menu.LangZh",    "zh": "中文 (Chinese)", "en": "中文 (Chinese)", "fixed": true },
    { "id": "MainFrame.Menu.LangEn",    "zh": "English (English)", "en": "English (English)", "fixed": true },
    { "id": "Subscription.Column.Name", "zh": "名称 ↕", "en": "Name ↕", "note": "↕ 是排序指示，两语言都保留" }
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

生成的 `StringTable.cpp`（节选，**窄表是 `const char*`**）：

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

> 直接生成两套编码字面量（`L"..."` 与 `u8"..."`），避免运行期宽窄转换；表为 `const` 静态存储，任意线程读取零锁。`u8"..."` 在 C++17 的类型是 `const char[N]`，因此窄表必须是 `const char* const[]`。
>
> `static_assert` 比较的是 `size_t` 与 `int`，必须显式 `static_cast<int>`，否则 `-Wsign-compare` 会新增警告（违反 AC-7）。

ID 命名规范见附录 B。

### 7.3 运行时 API 与编码规则

```cpp
// include/i18n/I18n.h  （不含任何 wx 头）
namespace i18n {
void setLanguage(Language lang);              // 仅启动路径与单测调用
Language language();
const wchar_t* w(StrId id);
const char*    u8(StrId id);
std::string    u8f(StrId id, std::initializer_list<std::string> args);
}
```

- 内部状态仅一个 `std::atomic<int> g_lang`（启动写入一次，之后只读）；即使是 worker 线程读取也不需要加锁。
- `w()`/`u8()` 返回静态存储指针，永不解引用悬垂。
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

**printf 风格格式化迁移（全仓 26 处含中文的 `wxString::Format`）**：

- 统一改为 `i18n::wxf(StrId, {...})`，参数先转 `wxString`：整数用 `wxString::Format("%lld", static_cast<long long>(v))` 或 `std::to_string` 后 `wxString::FromUTF8`，**禁止**把 `%zu/%ld/%d/%s` 留在译文里。
- 译文不得保留 U+3000（全角空格）对齐符（如 `剩余 %d　成功 %d`），改用普通空格或 ` · ` 分隔。
- `{}` 顺序由评审保证（生成器只能校验数量）；译文必须保持占位符相对顺序，避免参数错位。
- 迁移清单随 P1/P2/P3 各自批次执行；P3 收尾时 `grep` 带中文的 `Format(` 行数必须为 0。

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

**不做的事**：

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

### 7.5 菜单与切换流程

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

> 为什么放顶层而不是「设置」子菜单：默认语言是中文，英文用户首次启动面对「文件/代理/任务/设置/帮助」五个纯中文标题，**无法猜出哪个是 Settings**。把语言菜单做成顶层、且中文模式下标题为 `语言 / Language`，英文用户一眼可见。
>
> 该菜单是**新增项**，不触碰任何既有标题，因此不违反 FR-7。

切换流程（UI 线程，同步，只改配置）：

```
onMenuLanguage(event):
  1. target = (id == ID_MENU_LANGUAGE_ZH) ? ZhCN : EnUS
  2. 当前配置语言 = parseLanguage(config_.ui.language)
  3. if target == 当前配置语言: 同步 radio 选中态; return
  4. bool saved = controller_->setUiLanguage(target)   // 唯一写点：configMutex_ 下改 config_ 并落盘
  5. if (saved) { config_ = controller_->getConfig(); }
     else        { config_ = controller_->getConfig(); /* 仍是旧值 */ }
  6. syncLanguageMenu()          // radio 反映「配置中的语言」，保存失败时回到旧值
  7. if (saved) wxMessageBox(重启后生效)  else wxMessageBox(保存失败)
  8. Logger::write(i18n::u8f(StrId::MainFrame_Log_LanguageSaved, {code}), INFO)
  9. statusBar_->SetStatusText(提示文本, 0)
```

关键约束：

- **不调用 `i18n::setLanguage()`**：运行期语言必须与启动时一致，否则会出现「提示说重启生效，但部分界面已经变了」的自相矛盾状态。
- 第 7 步的两个提示框都是**单按钮（wxOK）**，文案入表；标题分别是 `Language` / `Language`。
- 第 9 步的状态栏文本只用于即时反馈，可能被后续刷新覆盖（因此第 7 步的模态框才是主反馈）。

**权威配置副本与唯一写点**：本工程有两份 `AppConfig`——`MainFrame::config_` 与 `AppController::config_`，后者由 `AppController::saveConfig` 在 `configMutex_` 下更新并落盘。语言切换必须走后者，否则用户随后打开「配置…」（以 `controller_->getConfig()` 初始化）点确定，会把旧 `ui.language` 整份写回，语言被静默回滚。

`AppController` 新增：

```cpp
// src/ui/AppController.h
bool setUiLanguage(i18n::Language lang);   // 更新 config_.ui.language 并落盘；返回保存是否成功
```

实现要点：取 `configMutex_` → 复制 `config_` → 改 `ui.language` → 赋值回 `config_` → `ConfigReader::save(config::activeConfigPath(), config_)`。语言不参与 worker 数据路径，**不需要** `isRunning()` 守卫。

**不重建菜单栏**：`MainFrame.cpp:845-846` 已记录 `initMenuBar()` 内直接 `SetMenuBar()` 在 wxMSW 3.2.5/MinGW 上会间歇性挂起。语言菜单在 `initMenuBar()` 中构造一次即可，运行期只改 radio 选中态（`wxMenu::Check`），不重建。

### 7.6 启动期文案来源（无重译）

语言在启动时确定，所有控件在**构造期**一次性取表值，运行期不再设置任何文本。因此不存在「重译矩阵」，只有「文案来源清单」：

| 组件 | 文案来源 | 备注 |
|---|---|---|
| `MainFrame` 菜单 | `initMenuBar()` 中 `i18n::wx(...)` | 6 个顶层 + 19 个叶子；加速键 `\tCtrl+X` 属于表值，不拼接 |
| 工具栏 `m_toolbar` | `initToolBar()` 中 11 个 `AddTool` 的 label/shortHelp | 含 `ID_TOOL_DETAIL_TOGGLE` |
| AUI 面板 | 仅 `detailPane` 有 Caption，值为英文 `Proxy Details` | **保持英文不变**（非目标 8） |
| 状态栏 | field 0 初始 `Ready`（zh 值也是 `Ready`）；field 4 路径 | 运行期状态文本走 §7.7 |
| 搜索区 | `wxChoice` 两项 + 占位提示 | 构造期取值 |
| 底部视图 | `wxRadioBox` 组标题 `监控 / Monitor` + 3 个选项 | 构造期取值；`UIIds.h` 的中文锚点仅在中文模式使用 |
| 网络监控条 | `Network OK` / `Disconnected` 写死在 paint lambda | **保持英文不变**（非目标 8） |
| `SubscriptionPanel` | 7 个列头：`#` / `启用 On` / `名称 Name ↕` / `有效 ValidProxies ↕` / `代理数 TotalProxies ↕` / `更新 UpdatedTime ↕` / `ID ↕` | D5 指定译名；` ↕` 后缀两语言都保留 |
| `ProxyListPanel` | 13 个列头已是英文（Region/Latency/…） | **不做任何改动** |
| `ProxyDetailPanel` | 字段标签、复制按钮 | 构造期取值 |
| `LogPanel` | 按钮（打开日志/清空日志窗口/日志统计）、`Level:` 标签 | `Level:` 现状即英文，保持 |
| `TaskMonitorPanel` | 「当前任务/排队/操作」标签、按钮、右键菜单 | 任务名渲染期查表（§7.7） |
| `BatchQueuePanel` | 状态文本、百分比、计数前缀 | 原 `剩余 %d　成功 %d　失败 %d` 按 §7.3 迁移 |
| `ComponentListPanel` | 列头、按钮 | 构造期取值 |
| `TrayIcon` | 右键菜单（仅「退出」一项，`TrayIcon.cpp:78`）；tooltip 为 `validproxy` | 构造期取值 |
| `StandaloneFloatingWidget` | 标题、列头、右键菜单、模式切换项、启动/停止池按钮 | 按钮文案是**状态派生**（`running ? 停止池 : 启动池`），两分支都走表 |
| 对话框（`ConfigDialog` 等 6 个） | 构造期取值 | 每次打开都新建，天然使用当前语言 |

> 因不再有运行期重译，§7.6 不再需要 `retranslate()` 接口、`Freeze()`/`Thaw()`、列宽/滚动/选中态保持、`SetItemLabel` 加速键重注册等设计（v1.1 的 R1–R3、R7 随之消失）。

### 7.7 日志与任务名

- 业务层日志：`Logger::write("中文", level)` → `Logger::write(i18n::u8(StrId::...), level)`；带参数用 `u8f`。
- 保持 `Logger` 接口不变（避免波及测试替身与统计逻辑），只在调用点替换文本来源。
- 日志行格式、级别、时间戳、`[Module]` 前缀**不翻译**（前缀是检索锚点）。
- `TaskType`：`const char* toString(TaskType)` 保留现有签名（测试与日志依赖），内部改为 `i18n::u8(StrId::Task_XXX)`；括号中的英文枚举名（如 `(TEST_ALL)`）保留。
- **自动任务子任务名**：`AppController.cpp:4215 autoStepDisplayName()` 把中文步骤名写进 `TaskHandle::targetId` 后缓存，`TaskMonitorPanel.cpp:159` 直接渲染该字段。改法：子任务行只保留 `AutoTaskStepType`，显示名在 `TaskMonitorPanel` 渲染时查表；`taskId` 展示文本（原 `"auto-3-<中文步骤名>"`）同样在渲染期拼接，不再把中文写入字段。
- 现有**英文**日志（如 `[MainFrame] Constructor begin`）保持英文，不做中文化（非目标 1）。
- `LogPanel` 只显示本次会话通过回调推来的行（不回读历史文件），因此面板内不存在跨会话混排。

#### 7.7.1 日志语言横幅与分隔线（D3）

**事实前提**：`LoggerInstance::init()` 每次启动都新建 `logDir/prefix_YYYYMMDD_HHMMSS.log` 并以 `std::ios::trunc` 打开（`src/LoggerInstance.cpp:44-46`），`LogPanel` 也只追加本次会话的日志事件。因此：

- **同一日志文件 / 同一面板内不会出现「新英文 + 旧中文」**，不存在可做动态分隔的位置；
- 真正会混排的场景是**把多个日志文件拼接查看**（排障、日志统计、用户报障打包）。

因此 D3 实现为「会话语言横幅 + 跨会话语言变更分隔线」，两条都写在每个日志文件的开头，语言中立（只有语言码与分隔符，无需翻译）：

```
[2026-11-18 21:03:11] [INFO] ===== session language: zh-CN =====
[2026-11-18 21:03:11] [INFO] ----- UI language changed since last session: zh-CN -> en-US -----
[2026-11-18 21:03:11] [INFO] ===== session language: en-US =====
```

实现：

- 新增 `include/i18n/LogBanner.h` + `src/i18n/LogBanner.cpp`：
  ```cpp
  namespace i18n {
  struct SessionBanner { const char* previous; const char* current; };  // previous 为空表示无历史
  SessionBanner readPreviousLanguage(const std::string& logDir, const std::string& currentLogPath);
  void writeSessionBanner(const SessionBanner& b);   // 调用 Logger::write，语言中立
  }
  ```
- `readPreviousLanguage`：在 `logDir` 中取**修改时间最新的、非当前文件的** `*.log`，读取末尾 32 KB，用正则 `session language: ([A-Za-z-]+)` 取最后一次匹配；找不到 → `previous` 为空。
- `writeSessionBanner`：若 `previous` 非空且与 `current` 不同，先写变更分隔线，再写会话横幅；两行均为 `INFO`，不受 `log_file_level` 影响（除非配置为 WARN 以上，此时按既有级别规则省略，属可接受退化）。
- 调用点：`main_gui` 在 `Logger::init()` 与 `i18n::setLanguage()` 之后、`MainFrame` 构造之前调用一次。
- `LogPanel` 无需改动：横幅经既有 `setLogCallback` 进入面板，天然成为本次会话的第一行，构成视觉边界。

### 7.8 对话框策略

- `ConfigDialog`、`SqlQueryManageDialog`、`AddPoolMemberDialog`、`BundleSelectorDialog`、`TestOnlineResultDialog`、`BundleChainEditor` 均为**按需创建**：创建时读取当前语言即可。
- `ConfigDialog` 通过 `ShowModal()` 打开，模态期间无法点击主菜单。
- `wxMessageBox` 的标题与正文全部走表；按钮为系统提供（`wxOK`），语言由 Windows 决定，不纳入范围。
- `wxPropertyGrid` 分类名、`wxNotebook` 页签在构造期一次性生成，随对话框创建取当前语言。

---

## 8. 文件级改动清单

### 8.1 新增

| 文件 | 内容 |
|---|---|
| `resources/i18n/strings.json` | 翻译资产（823 条） |
| `resources/i18n/README.md` | 一行说明：编译期资产，不随包，不是运行时可替换资源 |
| `include/i18n/Language.h` | 语言枚举与解析 |
| `include/i18n/StringId.h` | 生成：`StrId` 枚举 |
| `include/i18n/I18n.h` | 纯 C++ 查询/格式化 API |
| `include/i18n/I18nWx.h` | wx 便捷封装（仅 UI 层） |
| `include/i18n/LogBanner.h` + `src/i18n/LogBanner.cpp` | 会话语言横幅与变更分隔线 |
| `src/i18n/StringTable.cpp` | 生成：4 张表 + `static_assert` |
| `src/i18n/I18n.cpp` | 语言原子量、越界保护、`u8f` 实现 |
| `include/config/sections/UiConfigParser.h` | `ui.language` 解析（与其余 section 同目录） |
| `include/config/ActiveConfigPath.h` + `src/config/ActiveConfigPath.cpp` | 进程级生效配置路径 |
| `tests/test_i18n.cpp` | 表完整性、格式化、横幅解析单测 |
| `tests/test_config_ui_language.cpp` | 配置解析/序列化/生效路径单测 |
| `tests/ui/TestLanguageMenu.cpp` | 中文模式下的菜单接线与配置写入 UI 用例 |
| `scripts/gen-i18n.ps1` | 生成器（含 `-Check`） |
| `scripts/i18n-allowlist.txt` | 允许保留中文字面量的文件 + 理由 + 到期复核日期（逐阶段清空） |
| `docs/specs/2026-11-18-Spec-BilingualUI-v1.2.md` | 本文档 |

> 不新增 `check-i18n-coverage.ps1`：扩展现有 `scripts/check-ui-literals.ps1`（见 §11）。

### 8.2 修改（按字面量数量，合计 823 / 38 文件）

| 批次 | 文件（中文字面量数） | 小计 |
|---|---|---|
| P1 主窗口 | `MainFrame.cpp`(88)、`StandaloneFloatingWidget.cpp`(41)、`ProxyListPanel.cpp`(39)、`TaskMonitorPanel.cpp`(30)、`SubscriptionPanel.cpp`(24)、`LogPanel.cpp`(20)、`BatchQueuePanel.cpp`(10)、`ComponentListPanel.cpp`(7)、`RefreshCommands.cpp`(4)、`TrayIcon.cpp`(1)、`ProxyDetailPanel.cpp`(0，仅替换文案来源) | 264 |
| P2 对话框 | `ConfigDialog.cpp`(265)、`SqlQueryManageDialog.cpp`(73)、`AddPoolMemberDialog.cpp`(12)、`BundleSelectorDialog.cpp`(6)、`TestOnlineResultDialog.cpp`(5)、`BundleChainEditor.cpp`(5) | 366 |
| P3 日志/业务/入口 | `main_gui.cpp`(68)、`AppController.cpp`(36)、`CloudSyncService.cpp`(25)、`TaskType.cpp`(21)、`ConflictDetector.cpp`(8)、`ConfigGenerator.cpp`(5)、`ComponentResolver.cpp`(4)、`WebDavCredentials.cpp`(3)、`main_cli.cpp`(3)、`ProfileitemDAO.cpp`(3)、`StreamSettingsBuilder.cpp`(2)、`SchemaValidator.cpp`(2)、`SubitemDAO.cpp`(2)、`ProfileExItemDAO.cpp`(2)、`include/DatabaseHelper.h`(2)、`include/SubitemUpdaterV2.h`(1)、`WebDavClient.cpp`(1)、`SingBoxOutboundBuilderFactory.cpp`(1)、`OutboundBuilderFactory.cpp`(1)、`BundleChainResolver.cpp`(1)、`ProxyBatchTester.cpp`(1)、`AutoTaskManager.cpp`(1) | 193 |
| 配置与构建 | `ConfigReader.h`、`ConfigReader.cpp`、`ConfigJsonSerializer.cpp`、`ActiveConfigPath.cpp`、`main_gui.cpp`（设置生效路径 + 启动语言 + 横幅）、`main_cli.cpp`（启动语言）、`CMakeLists.txt`、`bin/config.json`、`bin/test_config.json` | — |
| 测试常量 | `tests/ui/UIIds.h`（新增语言菜单常量 + 英文常量块）、`tests/ui/TestMenuAction.cpp`（`leaves[]` 补云同步两项） | — |
| 文档 | `docs/INDEX.md`、`docs/plans/project-plans-tracker.md`、`README.md`/`README.en.md`（仅新增一节） | — |

> 三批小计 264 + 366 + 193 = **823**，与 §2.1 总数一致（P1 264 + P2 366 + P3 193）。

---

## 9. 分阶段实施计划

> 每个阶段结束必须：构建 EXIT 0 + 相关单测通过 + `check-ui-literals.ps1 -Mode cjk` 无**新增**违规（allowlist 只减不增），再进入下一阶段。

### P0 — 基础设施

| 任务 | 产出 | 验证 |
|---|---|---|
| P0-0 | `check-ui-literals.ps1`：注释/字面量识别改为单遍词法扫描（修既有缺陷）+ 增加 `-Mode cjk` + `-AllowList`（含每文件条数上限）；生成基线 allowlist（38 文件，含理由与上限） | 脚本在当前树退出码 0（全部命中 allowlist，823 条）；`-Mode cjk` 无 allowlist 时能报出全部 823 条；把任一行上限减 1 时退出码 1 |
| P0-1 | `Language.h` + `I18n.h/.cpp` + 空表骨架 + CMake 接入（`core` 库与 `test_i18n`） | 构建 EXIT 0 |
| P0-2 | `strings.json` 初始化（菜单/状态栏/语言项等种子 ~20 条）+ `gen-i18n.ps1` | 生成成功；`-Check` 通过 |
| P0-3 | `ui.language` 全链路（AppConfig/Parser/Serializer/`bin/*.json`）+ 启动解析 | `test_config_ui_language` 全绿 |
| P0-4 | `ActiveConfigPath` + `main_gui`/`main_cli` 发布 + `saveConfig`/`ConfigDialog` 改用 | 单测：`--config` 指向临时文件时保存落到该文件 |
| P0-5 | `AppController::setUiLanguage` + 顶层语言菜单 + radio 同步 + 重启提示 | 手动 + `TestLanguageMenu` |
| P0-6 | `LogBanner` + `main_gui` 调用 | 单测：解析上一会话语言；手动：切换语言后重启，日志出现分隔线 |

### P1 — 主窗口与长生命周期 UI（264 条）

| 任务 | 文件 | 验证 |
|---|---|---|
| P1-1 | `MainFrame.cpp`：菜单/工具栏/搜索区/底部 radio（含组标题）/状态栏 | `[menu]` + `[layout]` + `[bottomview]` |
| P1-2 | `ProxyListPanel` + `ProxyDetailPanel`（列头已是英文，只改按钮/右键菜单/空态） | `[search]`、`[clear]` |
| P1-3 | `SubscriptionPanel`（7 列头，D5 译名）+ `LogPanel` + `TaskMonitorPanel`（含子任务名）+ `BatchQueuePanel` | `[bottomview]`、`[taskledger]` |
| P1-4 | `StandaloneFloatingWidget` + `TrayIcon` | `[floatingwidget]` |
| P1-5 | `ComponentListPanel` + `RefreshCommands` | 手动巡检 |

### P2 — 对话框（366 条）

| 任务 | 文件 | 验证 |
|---|---|---|
| P2-1 | `ConfigDialog`（265 条；不含语言枚举） | 英文截图，无截断；配置往返单测 |
| P2-2 | `SqlQueryManageDialog`（73 条） | 英文截图 |
| P2-3 | `AddPoolMemberDialog` / `BundleSelectorDialog` / `TestOnlineResultDialog` / `BundleChainEditor`（28 条） | 英文截图 |

### P3 — 日志与业务层（193 条）

| 任务 | 文件 | 验证 |
|---|---|---|
| P3-1 | `main_gui`（启动/退出/错误框/单实例提示） | 中英各启动一次，日志按模板反查 |
| P3-2 | `AppController`（含 printf 风格迁移）+ `TaskType` + `CloudSyncService` | 实跑：更新订阅、批量测试、同步、云同步各一次 |
| P3-3 | 配置/DAO/网络层残余（`ConflictDetector`、`ConfigGenerator`、DAO、Validator、`WebDavClient` 等） | `test_config_reader*` 全绿 |
| P3-4 | `main_cli`（3 条） | CLI 两种语言各跑 `-h`、无参测试 |

### P4 — 门禁、测试与文档

| 任务 | 产出 |
|---|---|
| P4-1 | allowlist 清空到 0（逐文件移除），`check-ui-literals.ps1 -Mode cjk` 全仓零违规 |
| P4-2 | `test_i18n` 完整性用例 + `UIIds.h` 与表一致性断言 |
| P4-3 | `TestLanguageMenu`（菜单 + 配置写入 + 提示框 + 失败分支） |
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
| 菜单完整性 | 每个 `MainFrame.Menu.*` 表项同时存在 zh 与 en，且 en 值非空 |
| UIIds 一致性 | `ids::MenuFileCaption` 等既有中文常量 == `i18n::w(StrId::...)`（ZhCN 模式）— 防止测试常量与表漂移 |
| 配置 | 缺省 zh-CN；`en-US` 解析；非法值回退 + **恰好一条** WARN；`save()` 往返保留 `ui.language`；`ui` 为错误类型时容错 |
| 生效路径 | `setActiveConfigPath(tmp)` 后 `saveConfig` 写入 tmp；未设置时回落 `exeDir/config.json` |
| 保存失败 | 只读路径下选择英文：`setUiLanguage` 返回 false，`config_.ui.language` 保持旧值，日志仅一条 WARN，程序不崩溃 |
| 日志横幅 | 目录内存在上一会话 `zh-CN` 日志时，`readPreviousLanguage` 返回 `zh-CN`；无历史时返回空；当前文件被排除 |

### 10.2 UI 自动化（`tests/ui`，仅中文模式）

新增 `TestLanguageMenu.cpp`（`[language]` 标签），**使用独立沙箱目录** `test/ui-sandbox-lang/`（复制模板配置），避免污染其余 UI 用例共用的 `test/ui-sandbox/config.json`。

1. 沙箱配置预置 `"ui":{"language":"zh-CN"}` → 启动 → 断言顶层菜单数量为 6，第 5 项为 `语言 / Language`，`中文 (Chinese)` 为选中态。
2. 点击 `语言 / Language → English (English)` → 用 `dismissAllPopups(pid)` 关闭提示框。
3. 断言沙箱配置文件 `ui.language == "en-US"`，且 `bin/config.json` 内容哈希未变。
4. 断言**主窗口文案仍为中文**（不重启不切换）——这是 D1 的预期行为，防止实现者误加运行期重译。
5. 断言 `English (English)` radio 变为选中态。
6. Teardown（RAII）：无论用例成败，恢复沙箱配置文件原始字节。

**英文模式不做 UI 断言**（D2）。英文侧验收 = `test_i18n` 表断言（每条 en 非空且不含 CJK）+ §10.3 人工截图。

**工具栏不能通过 UIA 断言**：`tests/ui/UIIds.h` 已记录 `wxAuiToolBar` 是单个自绘 `wxWindowNR` 节点（"no measurable locator can exist for an individual tool"）。工具栏英文由 `test_i18n` 断言 `StrId::MainFrame_Toolbar_*` 的 en 值与 `UIIds.h` 新增英文常量一致 + 截图。

`UIIds.h` 新增：语言菜单的 `语言 / Language`、`中文 (Chinese)`、`English (English)`、云同步两项（`从云端同步订阅` / `上传配置与订阅到云端`）的中英常量。不修改既有中文常量（FR-7）。

### 10.3 人工巡检（记录到 PR 描述）

| 场景 | 中文 | 英文 |
|---|---|---|
| 启动 → 主窗口截图 | ✔ | ✔ |
| 工具栏 11 个按钮截图 | ✔ | ✔ |
| 配置对话框（全部 tab）截图 | ✔ | ✔ |
| 悬浮窗 + 托盘菜单 | ✔ | ✔ |
| 批量测试 / 更新 / 同步 / 云同步日志 | ✔ | ✔ |
| 100% / 125% / 150% DPI | ✔ | ✔ |
| 语言切换后重启，日志分隔线 | ✔ | ✔ |

---

## 11. 质量门禁

| 门禁 | 规则 | 失败处理 |
|---|---|---|
| `scripts/check-ui-literals.ps1 -Mode cjk -Root src -AllowList scripts/i18n-allowlist.txt` | `src/**`、`include/**`（自动排除 `src/i18n/**`）不得出现含 CJK 的字符串字面量；allowlist 每项需写理由、到期复核日期与**条数上限**，实测超过上限即失败（机制化「只减不增」） | 退出码 1 |
| `scripts/check-ui-literals.ps1`（既有默认模式） | `src/ui` 窄字节非 ASCII 零违规（保持不变） | 退出码 1 |
| `scripts/gen-i18n.ps1 -Check` | 生成文件与 `strings.json` 同步 | 退出码 1 |
| `UiStringEncodingTest`（`tests/test_ui_string_encoding.cpp`） | 保持既有扫描范围与断言 | 失败 |
| `test_i18n` | 表完整性/占位符/助记符/横幅解析 | 失败 |
| 全量 `ctest` | 与基线一致（当前 74/76；既有失败 `LogStatisticsEventTest`、flaky `UI_FLOATINGWIDGET`） | 新增失败即阻塞 |
| `scripts/build-and-test.bat` | UI 变更必跑 | 失败 |

**门禁能力边界（显式记录）**：`-Mode cjk` 只能发现「中文写进了源码」，发现不了「新增的纯英文文案没走表」。该方向由评审检查单兜底：任何新增 UI 文本必须同时新增 `StrId`。`UiStringEncodingTest` 已对 `L"..."` 前缀字面量免检，因此生成的 `StringTable.cpp` 不会误报。

**注释/字面量识别口径（P0-0 已修正）**：`check-ui-literals.ps1` 原实现在每行内用「先匹配字面量、再把第一个字面量外的 `//` 之后截断」的方式剥离行注释，但 span 数组构造写成 `,@($m.Index, $m.Index + $m.Length)`——PowerShell 里逗号优先于 `+`，实际得到三元数组 `@($idx, $idx, $len)`，`$sp[1]` 恒等于 `$idx`，导致**任何含 `//` 的行都被整行截断**（包括 `L"http://..."` 这类字符串内部的 `//`），既有 narrow 门禁因此存在漏报盲区。现改为单遍字符扫描（`Get-StrippedText`）：行注释 / 块注释替换为空格但保留换行（行号不漂移），字符串（含 `\"` 转义）内的 `//`、`/*` 不算注释，原始字符串 `R"delim(...)delim"` 整体抹空（其内部常有裸引号，若原样保留会让后续正则跨行误匹配）。

---

## 12. 验收标准

| ID | 标准 | 证据 |
|---|---|---|
| AC-1 | 源码零未登记中文字面量 | `check-ui-literals.ps1 -Mode cjk` 退出码 0（allowlist 为空） |
| AC-2 | 菜单选择写入配置并提示重启；不重启不切换 | `TestLanguageMenu` 通过 |
| AC-3 | `ui.language = "en-US"` 启动后界面为英文 | 人工截图（D2：不跑英文 UI 自动化） |
| AC-4 | 英文模式日志中**不再出现登记过的中文模板**（用户数据与第三方原文不含在内） | 对 `strings.json` 的 `zh` 值反查日志文件 |
| AC-5 | 中文模式与当前版本一致（除新增语言菜单） | 既有 UI 常量未改 + 全部 UI 用例通过 |
| AC-6 | `test_i18n` / `test_config_ui_language` 全绿 | ctest 输出 |
| AC-7 | 无 `auto`、无新增编译警告 | `rg -n "\bauto\b" src include` + 构建输出 |
| AC-8 | 英文模式下主窗口与对话框在 125%/150% DPI 无截断/重叠 | 截图 |
| AC-9 | 工具栏 11 个标签/tooltip 英文一致 | `test_i18n` 表断言 + 截图（UIA 不可用） |
| AC-10 | UI 测试不改写生产配置 | `bin/config.json` 前后哈希一致 |
| AC-11 | 每次启动日志含语言横幅；跨会话语言变更含分隔线 | 日志文件检查 |

---

## 13. 边界条件与错误处理

| 场景 | 行为 |
|---|---|
| 旧 `config.json` 无 `ui` 节 | 静默使用 `zh-CN`，保存后补写 `ui` 节 |
| `ui.language` 为未知值（如 `"ja-JP"`） | 归一化 `zh-CN`，**恰好一条** WARN（含原值），由 `UiConfigParser` 唯一产生 |
| 选择与当前配置相同的语言 | 不写盘、不弹框，仅同步 radio |
| 保存失败（只读/被占用） | `config_` 保持旧值，radio 回到旧值，弹一次失败提示 + 一条 WARN |
| 选择 English 后未重启 | 界面保持中文；radio 显示 English（配置值）；再次点击中文可改回，无需重启 |
| 选择 English 后立即点「配置…」并确定 | 对话框以 `controller_->getConfig()`（已含 `en-US`）为基准，不会回滚 |
| 语言切换时正在运行批量测试 | 不触发任何语言写入竞态（`saveConfig` 持 `configMutex_`）；重启生效意味着运行中任务不受影响 |
| 日志级别设为 WARN 以上 | 语言横幅（INFO）不落盘，属已知退化，不影响功能 |
| 上一会话日志不存在/不可读 | 只写会话横幅，不写变更分隔线 |
| 字符串表缺项（开发期） | 编译期 `static_assert` 失败；若绕过则 Debug `assert`，Release 返回 `(missing translation)` + 一条 WARN |
| 英文文案过长导致列头截断 | 订阅面板 5 个列头按语言设置最小宽度；DPI 巡检 |
| 主窗口标题 | 两语言均保持 `validproxy - Proxy Manager` |

---

## 14. 风险与缓解

| ID | 风险 | 影响 | 缓解 |
|---|---|---|---|
| R1 | 823 条替换引入编译错误或漏改 | 进度风险 | 按文件小批次提交，每批构建；allowlist 只减不增；生成器从修正后的扫描结果生成初始资产 |
| R2 | 英文文案更长导致对话框/工具栏拥挤 | 可用性 | 英文翻译遵循「短标签优先」（`Dedup` 而非 `Remove Duplicate Proxies`）；DPI 巡检；必要时调整最小宽度 |
| R3 | 配置写入与 worker 线程竞争 | 数据竞争 | 仅 `AppController::saveConfig` 在 `configMutex_` 下写；语言不参与 worker 读取 |
| R4 | 测试常量与翻译表漂移 | 测试假绿/假红 | `test_i18n` 断言 `UIIds.h` zh 常量 == 表值 |
| R5 | 生成文件被手改后与 JSON 不一致 | 维护混乱 | `gen-i18n.ps1 -Check` 入门禁；生成文件头部写「自动生成，勿手改」 |
| R6 | 新增纯英文文案绕过表 | 双语腐化 | 评审检查单（门禁只能查 CJK，见 §11 边界） |
| R7 | 生效配置路径改造影响既有「配置…」保存位置 | 配置写到意外文件 | 普通运行时三者本就同为 `exeDir/config.json`；新增单测覆盖 `--config` 分支 |
| R8 | 新增语言菜单改变菜单栏索引 | 依赖索引的旧断言失效 | 已核：`tests/ui` 通过 UIA Name / `UIIds.h` 定位，无索引依赖；保留 `TestMenuAction` 全量回归 |
| R9 | 语言测试污染共享沙箱配置 | 后续 UI 用例连锁假失败 | 独立沙箱目录 + RAII 还原（§10.2） |
| R10 | 「配置里是 English、界面仍是中文」被误认为 bug | 用户困惑 | 模态提示「重启后生效」+ 状态栏文本 + radio 反映配置值（§7.5） |
| R11 | 英文侧无自动化覆盖，漏译只能靠人工 | 质量风险（owner 已接受，D2） | `test_i18n` 保证每条 en 非空；PR 附中英截图对照；allowlist 清零 |
| R12 | 子任务名等「缓存到字段」的文案被漏改 | 英文界面残留中文 | P1-3 显式改造 `autoStepDisplayName` → 渲染期查表（§7.7） |

---

## 15. 发布与迁移

1. 版本号：作为 minor 特性随下一版本发布（`1.7.0`）。
2. 迁移：无数据库变更；`config.json` 首次保存自动补 `ui` 节。
3. README（中/英）新增一节「界面语言 / UI Language」：菜单位置、配置文件键、**重启生效**说明。
4. 发布说明注明：中文为默认；英文覆盖 UI 与日志模板；切换需重启；历史日志不重译。
5. 打包口径不变（见 `docs/2026-11-17-Reference-ReleasePackaging-v1.0.md`）：`strings.json` **不随包**，全部编译进 exe。

---

## 16. 工作量估算

| 阶段 | 内容 | 人日 |
|---|---|---|
| P0 | 门禁基线 + 基础设施 + 配置 + 生效路径 + 菜单/重启提示 + 日志横幅 | 1.5 |
| P1 | 主窗口与长生命周期 UI（264 条） | 2.0 |
| P2 | 对话框（366 条） | 2.5 |
| P3 | 日志/业务/入口（193 条）+ 翻译校对 | 1.5 |
| P4a | allowlist 清零 + 门禁接入 | 0.5 |
| P4b | `test_i18n` + `TestLanguageMenu` + 文档 | 0.5 |
| 缓冲 | R2/R7 触发的返工 | 0.5 |
| 合计 | | **9.0 人日** |

> 相比 v1.1 的 11.0：删除运行期重译（约 −1.5）、删除英文 UI 自动化与英文布局回归自动化（约 −0.5）、新增日志横幅（约 +0.3）。

---

## 17. 遗留问题（owner 可后续决定）

1. **自动重启**：当前提示框只有「确定」，用户需手动关闭并重新打开。若希望「一键重启」，需要额外处理单实例互斥、未保存状态与日志 flush，建议单独排期。
2. **英文侧自动化**（D2 的长期风险）：若将来希望英文界面也进 CI，可考虑「英文沙箱启动 + 只断言 6 个顶层菜单名」的最小用例，不改造现有中文用例。
3. **CLI 语言**：CLI 已随配置语言输出，但没有 `--lang` 覆盖；若需要单次覆盖，后续再加。
4. **企业/CI 场景的多语言并行**：当前一份 `config.json` 只有一个语言。若同一机器需要同时跑中文与英文（截图流水线），可用不同 `-c` 配置文件实现，无需新机制。

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
    { "id": "MainFrame.BottomView.Group", "zh": "监控", "en": "Monitor", "note": "wxRadioBox 组标题，同时是中文 UI 测试定位锚点" },
    { "id": "MainFrame.BottomView.Tasks", "zh": "任务", "en": "Tasks" },
    { "id": "MainFrame.BottomView.Progress", "zh": "任务进度", "en": "Progress" },
    { "id": "MainFrame.BottomView.Log",  "zh": "日志", "en": "Log" },
    { "id": "Subscription.Column.Name",  "zh": "名称 ↕", "en": "Name ↕" },
    { "id": "Subscription.Column.Enabled", "zh": "启用", "en": "On" },
    { "id": "Subscription.Column.Valid", "zh": "有效 ↕", "en": "ValidProxies ↕" },
    { "id": "Subscription.Column.Total", "zh": "代理数 ↕", "en": "TotalProxies ↕" },
    { "id": "Subscription.Column.Updated", "zh": "更新 ↕", "en": "UpdatedTime ↕" },
    { "id": "Subscription.Column.Id",    "zh": "ID ↕", "en": "ID ↕" },
    { "id": "MainFrame.Message.RestartToApply", "zh": "语言设置已保存，重启程序后生效。", "en": "Language saved. Restart the application to apply it." },
    { "id": "MainFrame.Message.LanguageSaveFailed", "zh": "语言设置未能保存到配置文件，本次选择不会在重启后生效。", "en": "Failed to save the language setting; it will not apply after restart." }
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

## 附录 D：修订记录

### D.1 v1.0 → v1.1（独立评审修订）

独立评审（5 位只读评审员：coherence / feasibility / scope-guardian / adversarial / design-lens）共 44 条发现，其中 **P1 9 条**，全部修复：

| # | 来源 | 问题 | 修订 |
|---|---|---|---|
| 1 | adversarial | 盘点脚本跨行吞串，真实 824 条（ConfigDialog 实际 265） | §2.1 重算；§8.2 重排 264/366/194（该 824 后被 D.3 更正为 823） |
| 2 | feasibility / adversarial / coherence | 菜单实为 17 个叶子（云端两项漏计） | §2.3、§7.6、§10.2 改 17 + 2 |
| 3 | feasibility / adversarial | 语言只写 `MainFrame::config_`，后续「配置…」保存会静默回滚 | 新增 `AppController::setUiLanguage`，唯一写点 |
| 4 | feasibility | 配置路径三套且都落到 `bin/config.json`，UI 沙箱断言必红 | 新增 `ActiveConfigPath`（§7.4.1） |
| 5 | feasibility | `wxAuiToolBar` 无 UIA 子元素，工具栏断言不可实现 | 改为表断言 + 截图（AC-9） |
| 6 | feasibility / adversarial | `ProxyListPanel` 13 列**已是英文**；`SubscriptionPanel` 才是中文列头 | §7.6 修正 |
| 7 | coherence | 「en 不含 CJK」会判死固定双语的语言项 | `"fixed": true` 豁免 |
| 8 | coherence / adversarial | `kZhU8` 声明为 `const wchar_t*` 却赋 `u8"..."`，编译不过 | 窄表改 `const char* const[]` |
| 9 | coherence | `static_assert` 比较 `size_t`/`int` 新增 `-Wsign-compare` | 显式 `static_cast<int>` |
| 10 | scope / design / adversarial | `VALIDPROXY_LANG` 是范围外第三层优先级 | 删除 |
| 11 | scope | 配置对话框语言枚举与菜单重复，且是回滚路径 | 删除 |
| 12 | scope / adversarial | AC-4「日志零 CJK」与「不翻译用户数据」矛盾 | 改为模板反查判据 |
| 13 | scope / design | 门禁脚本排在 P4，却要求 P0 起每阶段执行 | 提前为 P0-0 |
| 14 | scope | 新脚本与 `check-ui-literals.ps1` 职责重叠 | 合并为 `-Mode cjk` |
| 15 | scope / feasibility | `Ready` 的 zh 写成「就绪」，违反逐字一致 | zh 固定 `Ready` |
| 16 | scope / design | README 同时是「非目标」与「交付项」 | §5.2 改为「仅新增一节」 |
| 17 | design-lens | 中文模式下英文用户找不到语言入口 | 语言菜单改**顶层**且标题双语 |
| 18 | design-lens | 保存失败只靠状态栏（200px 截断且会被覆盖） | 改为模态提示 |
| 19 | design-lens / adversarial | 运行中任务名切换时机矛盾；子任务名缓存中文 | 渲染期查表（§7.7） |
| 20 | design-lens | 助记符唯一性无门禁 | 生成器新增校验 |
| 21 | coherence | `main_cli` 11 条未计入任何批次（实为 3 条） | 补入 P3 |
| 22 | coherence | 示例用裸 `L"界面语言"`，违反自定门禁 | 随语言枚举一并删除 |
| 23 | coherence | `StrId::App_Queue.Enqueued` 不是合法标识符 | 统一 `Module_Area_Name` |
| 24 | coherence | §5.6「不改控件层级」与 FR-2 冲突 | 限定为「不改既有项」 |
| 25 | coherence | AUI 标题清单与源码不符 | 改为「仅 detailPane，且保持英文」 |
| 26 | feasibility | 底部 `wxRadioBox` 组标题「监控」漏项 | 补组标题 |
| 27 | feasibility | `ConfigValidator` 无法写回归一化值 | 归一化只在 `UiConfigParser` |
| 28 | feasibility / adversarial | UI 沙箱配置被语言用例污染 | 独立沙箱 + RAII |
| 29 | adversarial | 26 处 printf 风格 `Format` 未纳入迁移 | 新增 §7.3 迁移小节 |
| 30 | adversarial | 门禁只认 CJK，纯英文新文案可静默通过 | §11 记录边界 + 检查单 |
| 31 | adversarial | 托盘「显示主界面」不存在；菜单指针有双重释放风险；悬浮窗按钮状态派生 | §7.6 修正 |
| 32 | adversarial | `main_cli` 的 `--lang` 使完成判定模糊 | 明确移出本期 |

### D.2 v1.1 → v1.2（owner 决策定稿）

| 决策 | 变更 |
|---|---|
| **D1 重启生效** | 删除整章运行期重译设计（`retranslate()`、`Freeze/Thaw`、列宽/滚动/选中态保持、`SetItemLabel` 加速键重注册、原子热切换语义）；§7.6 由「重译矩阵」改为「启动期文案来源」；删除风险 R1–R3、R7；FR-3 改为「只写配置 + 提示重启」；新增 R10「配置是英文但界面仍中文」的用户困惑缓解；工作量 11.0 → 9.0 |
| **D2 不做英文 UI 自动化** | §10.2 只保留 `TestLanguageMenu`（中文模式，验证接线/落盘/提示/不重启不切换）；英文侧验收改为 `test_i18n` 表断言 + 人工截图；新增 §5.9 非目标、R11 覆盖度风险 |
| **D3 日志视觉分隔** | 新增 FR-10、§7.7.1、`LogBanner` 组件、AC-11；并说明重启模型下同一面板内不存在混排，分隔线针对跨文件拼接场景 |
| **D4 默认 zh-CN** | 不变；可发现性由 §7.5 顶层双语标题保证 |
| **D5 订阅列名** | 固定为 `Name / ValidProxies / TotalProxies / UpdatedTime / ID`（保留 ` ↕`），写入 §7.6 与附录 A |

### D.3 v1.2 实施首日（P0-0）的口径更正

P0-0 落地门禁时发现 `check-ui-literals.ps1` 既有实现有缺陷（详见 §11「注释/字面量识别口径」），修正后重新盘点，基线由 824/39 更正为 **823/38**：

| 项 | 修正前 | 修正后 | 原因 |
|---|---|---|---|
| 总数 / 文件 | 824 / 39 | **823 / 38** | `src/FullConfigTemplateDAO.cpp` 的唯一命中是 `:669-671` 的普通注释；旧脚本不识别原始字符串 `R"JSON(...)JSON"` 内的裸引号，引号失步后把注释当成代码 |
| `src/**` | 822 / 38 | **820 / 36** | 同上（−1 条命中、−1 个文件）＋ `include` 分项修正（见下） |
| `include/**` | 2 / 1 | **3 / 2** | §8.2 本来就列了 `DatabaseHelper.h`(2) + `SubitemUpdaterV2.h`(1) = 3 条 / 2 文件，§2.1 的汇总行写错 |
| P3 批次 | 194 | **193** | 移除 `FullConfigTemplateDAO.cpp`(1) |
| allowlist | 39 行 | **38 行** | 同上；文件头写入更正记录 |

同时确认：`tests/test_ui_string_encoding.cpp` 的 C++ 扫描器**没有**同类缺陷（它本来就是单遍词法扫描，并显式跳过注释、字符字面量与原始字符串），无需改动。

### D.4 v1.2 → v1.3（2026-11-20 用户三项追加）

| 项 | 变更 | 影响的 FR/AC |
|---|---|---|
| **版本信息加二进制 ABI** | `include/version.h.in` 新增编译期 `APP_CRT_ABI`（`_UCRT`→`ucrt`，`__MSVCRT_VERSION__`→`msvcrt`）；`-v`、启动 REPORT 日志、关于对话框三处显示。**FR-7「中文输出逐字一致」对关于对话框开一个 owner 明确要求的例外**（新增一行「运行时 ABI: {}」，中英同步，占位符 9/9） | FR-7（例外）、AC-5 |
| **配置热更新** | `XrayManager::getInstance()` 检测 xray 路径 / worker 数变化：空闲池立即重建，有实例在跑则保留并 WARN；改配置后无需重启即生效（不在本 Spec 的文案范围内，登记以免与 D1「重启生效」混淆——D1 只约束**语言切换**） | — |
| **「任务链」乱码修复** | `ConfigDialog` 的 `stepDisplayName()` 返回临时 `wxString` 缓冲区（悬垂指针）导致乱码；抽出 `src/ui/TaskChainText.{h,cpp}` 按值返回。属缺陷修复，不改变文案 | — |

验证见 `docs/bugfix/2026-11-20-Bugfix-UiMojibake-AbiTag-ConfigHotReload-v1.0.md`。
