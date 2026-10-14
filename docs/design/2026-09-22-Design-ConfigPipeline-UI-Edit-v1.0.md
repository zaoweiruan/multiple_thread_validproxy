---
title: "ConfigPipeline UI 编辑功能 — 方案设计"
type: design
status: draft
date: 2026-09-22
updated: 2026-09-22
version: v1.2
---

# ConfigPipeline UI 编辑功能 — 方案设计

> 关联设计文档：`docs/design/2026-09-22-Design-ConfigPipeline-v1.0.md`（Pipeline 核心实现）
> 关联组件：`ConfigDialog` (`src/ui/ConfigDialog.h/cpp`)、`ConfigPipeline` (`include/config/ConfigPipeline.h`)

---

## 1. 背景与目标

### 1.1 背景

ConfigPipeline 管线机制已实现全部 6 阶段（基础设施 → 核心步骤 → 扩展步骤 → ConfigBuilder 集成 → 单元测试 → 文档），但管线配置（`config.json` 的 `config_pipeline` 段）目前**仅能通过手动编辑 JSON 文件**配置，缺乏 UI 可视化编辑能力。

### 1.2 目标

在 `ConfigDialog` 的**「模板配置」Tab** 中集成 `config_pipeline` 编辑功能，实现：

1. **管线列表浏览** — 展示已定义的管线（test/standalone/pool × xray/singbox）
2. **步骤可视化编排** — 以列表形式展示每个管线的步骤序列
3. **步骤参数编辑** — 支持编辑步骤类型、参数 JSON、错误策略
4. **管线保存/加载** — 读写 `config.json` 的 `config_pipeline` 段
5. **重启生效** — 保存后提示用户重启应用使管线配置生效（避免线程安全问题）
6. **模板预览** — 实时显示当前管线配置生成的最终 Xray/SingBox JSON 配置

---

## 2. 现状分析

### 2.1 ConfigDialog 现有结构

```
ConfigDialog (wxDialog)
├── Notebook (wxNotebook)
│   ├── Tab 1: "应用配置"
│   │   └── PropertyGrid (wxPropertyGrid) — 通用应用设置
│   └── Tab 2: "模板配置"
│       ├── wxListCtrl — 模板列表（ID/类型/父模板/备注/启用）
│       ├── wxTextCtrl — JSON 编辑器
│       └── 按钮：保存选中 / 刷新
└── 底部：确定 / 取消
```

### 2.2 Pipeline 配置数据结构

**`config.json` 的 `config_pipeline` 段**：
```json
{
  "config_pipeline": {
    "test": {
      "templateId": "xray_test_001",
      "steps": [
        {"type": "replace_variables", "params": {}},
        {"type": "add_outbound", "params": {"tag": "proxy", "source": "profile"}, "onError": "fail_fast"},
        {"type": "merge_routing", "params": {"inboundTag": "socks", "outboundTag": "proxy"}, "onError": "continue"}
      ]
    },
    "test_singbox": {
      "templateId": "singbox_test_001",
      "steps": [...]
    }
    // standalone, standalone_singbox, pool, pool_singbox 类似
  }
}
```

**`PipelineConfig.h` 数据结构**：
```cpp
struct StepConfig {
    std::string type;                    // 步骤类型（如 "replace_variables"）
    boost::json::object params;          // 步骤参数
    std::string onError = "continue";    // 错误策略：continue/fail_fast/fallback
};

struct PipelineDefinition {
    std::string templateId;              // 关联模板 ID
    std::vector<StepConfig> steps;       // 步骤列表
};
```

### 2.3 可用步骤类型（10 个）

| 步骤类型 | 说明 | 主要参数 |
|----------|------|----------|
| `replace_variables` | 变量替换 | — |
| `add_outbound` | 添加出站 | tag, source, protocol, settings |
| `merge_routing` | 合并路由规则 | inboundTag, outboundTag, balancerTag |
| `add_inbound` | 添加入站 | tag, protocol, port, settings |
| `add_observatory` | 添加观测器 | subjectSelector, probeURL, probeInterval |
| `add_balancer` | 添加负载均衡器 | tag, selector, strategy, observation |
| `add_stats` | 启用统计 | — |
| `add_policy` | 添加策略 | levels, system |
| `add_api` | 添加 API | tag, listen, services |
| `modify_field` | 修改字段 | path, value |

### 2.4 管线键名规则

```
modeKey = [mode] + [_singbox]
mode: test | standalone | pool
clientName: xray (默认) | singbox

示例:
- test → "test"
- test_singbox → "test_singbox"
- standalone → "standalone"
- standalone_singbox → "standalone_singbox"
- pool → "pool"
- pool_singbox → "pool_singbox"
```

---

## 3. UI 设计方案

### 3.1 整体布局 — 三栏布局

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  模板配置 Tab                                                                │
├─────────────────────────────────────────────────────────────────────────────┤
│  ┌───────────────────────────────────────────────────────────────────────┐ │
│  │  [● 模板配置]  [○ 配置管线]  ← 子 Tab (wxNotebook)                   │ │
│  ├───────────────────────────────────────────────────────────────────────┤ │
│  │                                                                       │ │
│  │  ┌────────────┐  ┌─────────────────────┐  ┌────────────────────────┐ │ │
│  │  │ 管线列表   │  │ 步骤编辑器           │  │ 模板预览               │ │ │
│  │  │            │  │                     │  │                        │ │ │
│  │  │ test       │  │ 模板: [xray_test ▼] │  │ [生成预览] [清空]       │ │ │
│  │  │ test_sb    │  │                     │  │                        │ │ │
│  │  │ standalone │  │ ┌──┬──────────┬───┐ │  │ ┌────────────────────┐ │ │ │
│  │  │ standalone │  │ │# │步骤类型  │错 │ │  │ │ {                  │ │ │ │
│  │  │   _sb      │  │ │  │replace..│策 │ │  │ │   "log": {...},    │ │ │ │
│  │  │ pool       │  │ │  │add_out. │略 │ │  │ │   "dns": {...},    │ │ │ │
│  │  │ pool_sb    │  │ │  │merge_ro │   │ │  │ │   "inbounds": [...],│ │ │ │
│  │  │            │  │ └──┴──────────┴───┘ │  │ │   "outbounds": [...],│ │ │ │
│  │  │ [删除]     │  │                     │  │ │   "routing": {...}  │ │ │ │
│  │  │ [刷新]     │  │ 参数 JSON:          │  │ │ }                  │ │ │ │
│  │  │            │  │ ┌─────────────────┐ │  │ │                    │ │ │ │
│  │  │            │  │ │ {"tag":"proxy"} │ │  │ └────────────────────┘ │ │ │
│  │  │            │  │ └─────────────────┘ │  │                        │ │ │
│  │  │            │  │                     │  │ 状态: ✅ 生成成功      │ │ │
│  │  │            │  │ [上移][下移][添加]  │  │                        │ │ │
│  │  │            │  │ [删除][保存]        │  │                        │ │ │
│  │  └────────────┘  └─────────────────────┘  └────────────────────────┘ │ │
│  │                                                                       │ │
│  └───────────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 子 Tab 结构

在现有「模板配置」Tab 内部嵌入 **wxNotebook**，分为两个子页：

| 子页 | 内容 |
|------|------|
| 模板配置 | 现有模板列表 + JSON 编辑器（不变） |
| 配置管线 | 管线列表 + 步骤编辑器（新增） |

### 3.3 配置管线子页详细设计

#### 3.3.1 左侧：管线列表面板

**控件**: `wxListCtrl` (wxLC_REPORT)

| 列 | 标题 | 宽度 | 说明 |
|----|------|------|------|
| 0 | 管线名 | 120 | 如 "test", "standalone_singbox" |
| 1 | 模板 | 120 | 关联模板 ID |
| 2 | 步骤数 | 60 | steps.size() |

**按钮**:
- `[删除]` — 删除选中管线
- `[刷新]` — 从 config.json 重新加载

**注**: 管线名从固定列表选择（test/standalone/pool × xray/singbox），不支持自由输入新建管线。如需新增管线类型，需修改代码添加新的 modeKey。

#### 3.3.2 右侧：步骤编辑面板

**顶部**: 模板选择器
- `wxChoice` 下拉框，显示所有可用模板 ID（从 `FullConfigTemplateDAO::getAll()` 获取）
- 选中管线后自动填充其 `templateId`

**中间**: 步骤列表
- `wxListCtrl` (wxLC_REPORT)

| 列 | 标题 | 宽度 | 说明 |
|----|------|------|------|
| 0 | # | 40 | 步骤序号（从 1 开始） |
| 1 | 步骤类型 | 140 | 如 "replace_variables" |
| 2 | 错误策略 | 80 | continue / fail_fast / fallback |
| 3 | 参数摘要 | 200 | params JSON 的前 50 字符 |

**底部**: 参数 JSON 编辑器
- `wxTextCtrl` (wxTE_MULTILINE)
- 显示选中步骤的 `params` JSON
- 编辑后点击「保存」或切换步骤时自动保存

**底部按钮**:
- `[上移]` — 步骤上移
- `[下移]` — 步骤下移
- `[添加]` — 添加新步骤（弹出步骤类型选择框）
- `[删除]` — 删除选中步骤
- `[保存管线]` — 保存当前管线到 config.json

#### 3.3.3 右侧：模板预览面板

**功能**: 实时显示当前管线配置生成的最终 Xray/SingBox JSON 配置

**控件**:
- `wxTextCtrl` (wxTE_MULTILINE | wxTE_READONLY) — 只读 JSON 预览
- `wxButton` — `[生成预览]` 触发生成
- `wxButton` — `[清空]` 清空预览内容
- `wxStaticText` — 状态提示（成功/失败/错误信息）

**交互流程**:
```
用户点击「生成预览」
    │
    ▼
收集当前编辑的管线定义（templateId + steps）
    │
    ▼
从 DB 加载模板配置（FullConfigTemplateDAO::getById）
    │
    ▼
执行管线步骤（ConfigPipeline::execute 或等效逻辑）
    │
    ▼
格式化 JSON → 填充预览编辑器
    │
    ▼
显示「生成成功」或错误信息
```

**预览内容示例**:
```json
{
  "log": { "loglevel": "warning" },
  "dns": { "servers": ["1.1.1.1"] },
  "inbounds": [
    { "port": 10808, "protocol": "mixed", "tag": "socks" }
  ],
  "outbounds": [
    { "tag": "proxy", "type": "vmess", "settings": { ... } },
    { "tag": "direct", "type": "direct" },
    { "tag": "block", "type": "blackhole" }
  ],
  "routing": {
    "rules": [
      { "inboundTag": "socks", "outboundTag": "proxy" },
      { "inboundTag": "socks", "outboundTag": "direct" }
    ]
  },
  "api": { "tag": "api", "listen": "127.0.0.1:10085" }
}
```

**注意事项**:
- 预览使用**当前编辑状态**，不依赖保存后的配置
- 如果模板加载失败或管线执行出错，显示错误信息而非崩溃
- 预览内容为**只读**，防止用户误修改
- 每次点击「生成预览」重新生成，不缓存结果

### 3.4 交互流程

```
用户选择管线
    │
    ▼
加载管线定义（templateId + steps）
    │
    ▼
填充模板下拉框 + 步骤列表
    │
    ▼
用户选择步骤
    │
    ▼
填充参数 JSON 编辑器
    │
    ▼
用户编辑参数
    │
    ▼
用户点击「保存管线」
    │
    ▼
更新 PipelineDefinition → 写入 config.json
    │
    ▼
提示「保存成功，重启应用后生效」
```

---

## 4. 数据流与持久化

### 4.1 读取流程

```
ConfigDialog 初始化
    │
    ▼
createTemplateTab() → 创建子 Notebook
    │
    ▼
createPipelineTab() → 创建管线编辑界面
    │
    ▼
loadPipelineConfig()
    │
    ├── 读取 config.json（通过 AppConfig 或文件 IO）
    ├── 解析 config_pipeline 段
    ├── 填充管线列表 wxListCtrl
    └── 填充模板下拉框（从 DB 获取所有模板 ID）
```

### 4.2 预览生成流程

```
用户点击「生成预览」
    │
    ▼
onGeneratePreview()
    │
    ├── 收集当前编辑的 PipelineDefinition（未保存状态）
    ├── 从 DB 加载模板配置（FullConfigTemplateDAO::getById）
    ├── 执行管线步骤生成最终配置
    │   ├── 方式 A: 创建临时 ConfigPipeline 实例执行
    │   └── 方式 B: 直接调用各 StepHandler 执行
    ├── 格式化 JSON（boost::json::dump + 缩进）
    ├── 填充预览编辑器（wxTextCtrl::ChangeValue）
    └── 显示状态（成功/失败）
```

### 4.3 保存流程

```
用户点击「保存管线」
    │
    ▼
onPipelineSave()
    │
    ├── 收集当前编辑的 PipelineDefinition
    ├── 更新 pipelines_ map（内存）
    └── 写入 config.json（合并 config_pipeline 段）
    
    // 注意：不调用 loadConfig() 热加载，避免线程安全问题
    // 用户需要重启应用使新配置生效
```

### 4.3 config.json 写入策略

**关键约束**: 不能破坏 config.json 的其他配置段。

**方案**: 
1. 读取整个 config.json → `boost::json::value`
2. 更新 `root["config_pipeline"]` 段
3. 写回整个 JSON

**实现位置**: `ConfigDialog::savePipelineConfig()` 私有方法

---

## 5. 类设计

### 5.1 ConfigDialog 新增成员

```cpp
// 子 Notebook
wxNotebook* templateSubNotebook_{nullptr};

// 管线列表
wxListCtrl* pipelineList_{nullptr};
std::unordered_map<std::string, config::PipelineDefinition> pipelines_;

// 步骤列表
wxListCtrl* stepList_{nullptr};
wxChoice* templateChoice_{nullptr};
wxTextCtrl* stepParamEditor_{nullptr};

// 模板预览
wxTextCtrl* previewEditor_{nullptr};
wxStaticText* previewStatus_{nullptr};

// 管线名
std::string currentPipelineName_;

// 步骤索引
int currentStepIndex_{-1};

// config.json 路径（新增，解决 R1）
std::string configPath_;
```

### 5.1.1 ConfigDialog 构造函数变更（R1 修复）

```cpp
// 原构造函数
ConfigDialog(wxWindow* parent, const config::AppConfig& cfg, sqlite3* db);

// 新构造函数（增加 configPath 参数）
ConfigDialog(wxWindow* parent, const config::AppConfig& cfg, sqlite3* db, 
             const std::string& configPath);
```

**调用方更新**: `MainFrame::onMenuConfig()` 需要传入 config.json 路径。

**路径来源**: 从 `AppConfig::database_path` 所在目录推断，或通过 `wxStandardPaths::Get().GetLocalDataDir()` + `config.json` 拼接。

### 5.2 ConfigDialog 新增方法

```cpp
// 管线 Tab 创建
void createPipelineTab();

// 管线数据加载/保存
void loadPipelineConfig();
void savePipelineConfig();
bool readConfigJson(boost::json::value& root) const;
bool writeConfigJson(const boost::json::value& root) const;

// 模板预览
void onGeneratePreview(wxCommandEvent& event);
void onClearPreview(wxCommandEvent& event);
bool generatePreviewConfig(boost::json::value& config) const;

// 管线事件处理
void onPipelineSelection(wxListEvent& event);
void onPipelineAdd(wxCommandEvent& event);
void onPipelineDelete(wxCommandEvent& event);
void onPipelineSave(wxCommandEvent& event);

// 步骤事件处理
void onStepSelection(wxListEvent& event);
void onStepAdd(wxCommandEvent& event);
void onStepDelete(wxCommandEvent& event);
void onStepMoveUp(wxCommandEvent& event);
void onStepMoveDown(wxCommandEvent& event);
void onTemplateChange(wxCommandEvent& event);

// 辅助方法
void refreshStepList();
void refreshStepParamEditor();
void refreshPipelineList();
std::vector<std::string> getAvailableTemplateIds() const;
std::vector<std::string> getAvailableStepTypes() const;
std::string formatJson(const boost::json::value& value) const;
```

---

## 6. 步骤类型注册表

### 6.1 可用步骤类型（从 StepRegistry 动态获取）

```cpp
std::vector<std::string> ConfigDialog::getAvailableStepTypes() const {
    const auto& registry = config::StepRegistry::instance();
    return registry.getRegisteredTypes();  // 返回所有已注册步骤类型
}
```

### 6.2 步骤类型显示名映射（R5 修复）

**方案 A（推荐）**: 在 `StepHandler` 接口中增加 `displayName()` 虚方法

```cpp
// StepHandler.h 新增
class StepHandler {
public:
    virtual ~StepHandler() = default;
    virtual std::string type() const = 0;
    virtual std::string displayName() const { return type(); }  // 默认返回内部类型名
    virtual void execute(...) const = 0;
};
```

**各 Handler 实现 displayName()**:
| 内部类型 | displayName() |
|----------|---------------|
| replace_variables | "变量替换" |
| add_outbound | "添加出站" |
| merge_routing | "合并路由" |
| add_inbound | "添加入站" |
| add_observatory | "添加观测器" |
| add_balancer | "添加负载均衡" |
| add_stats | "启用统计" |
| add_policy | "添加策略" |
| add_api | "添加 API" |
| modify_field | "修改字段" |

**UI 获取显示名**:
```cpp
std::string ConfigDialog::getStepDisplayName(const std::string& type) const {
    const auto& registry = config::StepRegistry::instance();
    if (auto handler = registry.getHandler(type)) {
        return handler->displayName();
    }
    return type;  // fallback to internal type name
}
```

**方案 B（备选）**: 在 UI 中维护映射表（当前设计）

如果不想修改 StepHandler 接口，可在 ConfigDialog 中维护映射表，但需在新增步骤类型时同步更新两处。

---

## 7. 模板预览生成逻辑

### 7.1 预览生成策略

**方案**: 直接调用各 StepHandler 执行，避免创建临时 ConfigPipeline 实例

```cpp
bool ConfigDialog::generatePreviewConfig(boost::json::value& config) const {
    // 1. 从 DB 加载模板配置
    std::string templateId = templateChoice_->GetStringSelection().ToStdString();
    auto templateOpt = db_->getTemplateById(templateId);
    if (!templateOpt.has_value()) {
        previewStatus_->SetLabel(L"错误: 模板加载失败");
        return false;
    }
    
    // 2. 解析模板 JSON
    boost::json::value configVal;
    try {
        configVal = boost::json::parse(templateOpt->config);
    } catch (const std::exception& e) {
        previewStatus_->SetLabel(L"错误: 模板 JSON 解析失败");
        return false;
    }
    
    // 3. 构建 PipelineContext
    config::PipelineContext ctx;
    ctx.proxyType = isSingBoxPipeline() ? config::ProxyType::SingBox 
                                         : config::ProxyType::Xray;
    ctx.db = db_.get();
    
    // 4. 执行各步骤
    const auto& pipeline = pipelines_[currentPipelineName_];
    for (const auto& step : pipeline.steps) {
        auto handler = config::StepRegistry::instance().getHandler(step.type);
        if (!handler) continue;  // 跳过未注册的步骤
        
        try {
            handler->execute(configVal.as_object(), step.params, ctx);
        } catch (const std::exception& e) {
            previewStatus_->SetLabel(
                wxString::Format(L"步骤执行错误 [%s]: %s", 
                                 wxString(step.type), wxString(e.what())));
            return false;
        }
    }
    
    config = configVal;
    return true;
}
```

### 7.2 JSON 格式化

```cpp
std::string ConfigDialog::formatJson(const boost::json::value& value) const {
    boost::json::serialization_options opts;
    opts.indent = 2;  // 2 空格缩进
    return boost::json::serialize(value, opts);
}
```

### 7.3 预览事件处理

```cpp
void ConfigDialog::onGeneratePreview(wxCommandEvent&) {
    boost::json::value preview;
    if (generatePreviewConfig(preview)) {
        std::string formatted = formatJson(preview);
        previewEditor_->ChangeValue(wxString(formatted.c_str(), wxConvUTF8));
        previewStatus_->SetLabel(L"✅ 生成成功");
    }
}

void ConfigDialog::onClearPreview(wxCommandEvent&) {
    previewEditor_->ChangeValue(L"");
    previewStatus_->SetLabel(L"");
}
```

### 7.4 预览面板创建

```cpp
void ConfigDialog::createPreviewPanel(wxPanel* parent) {
    wxBoxSizer* previewSizer = new wxBoxSizer(wxVERTICAL);
    
    // 按钮栏
    wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    wxButton* btnGenerate = new wxButton(parent, wxID_ANY, L"生成预览");
    btnGenerate->Bind(wxEVT_BUTTON, &ConfigDialog::onGeneratePreview, this);
    wxButton* btnClear = new wxButton(parent, wxID_ANY, L"清空");
    btnClear->Bind(wxEVT_BUTTON, &ConfigDialog::onClearPreview, this);
    btnSizer->Add(btnGenerate, 0, wxRIGHT, 5);
    btnSizer->Add(btnClear, 0);
    previewSizer->Add(btnSizer, 0, wxALL, 5);
    
    // 预览编辑器
    previewEditor_ = new wxTextCtrl(parent, wxID_ANY, L"",
        wxDefaultPosition, wxDefaultSize,
        wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    previewEditor_->SetFont(wxFont(10, wxFONTFAMILY_MODERN, 
                                 wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    previewSizer->Add(previewEditor_, 1, wxEXPAND | wxALL, 5);
    
    // 状态栏
    previewStatus_ = new wxStaticText(parent, wxID_ANY, L"");
    previewSizer->Add(previewStatus_, 0, wxALL, 5);
    
    parent->SetSizer(previewSizer);
}
```

---

## 8. 错误处理

### 7.1 JSON 解析错误

```cpp
bool ConfigDialog::readConfigJson(boost::json::value& root) const {
    try {
        // 读取 config.json 文件
        std::ifstream file(configPath);
        if (!file.is_open()) return false;
        root = boost::json::parse(file);
        return true;
    } catch (const std::exception& e) {
        wxLogError("config.json 解析失败: %s", e.what());
        return false;
    }
}
```

### 7.2 步骤参数验证（R3 修复）

**完整参数验证表**（从 `StepHandlers.cpp` 提取）：

| 步骤类型 | 必需参数 | 可选参数 | 默认值 |
|----------|----------|----------|--------|
| `replace_variables` | — | — | — |
| `add_outbound` | `tag` | `source`, `protocol`, `settings` | source="static" |
| `merge_routing` | `inboundTag` | `outboundTag`, `balancerTag` | outboundTag="proxy" |
| `add_inbound` | `tag` | `protocol`, `port`, `settings` | protocol="socks", port=10808 |
| `add_observatory` | `subjectSelector` | `probeURL`, `probeInterval` | probeInterval=10 |
| `add_balancer` | `tag` | `selector`, `strategy`, `observation` | strategy="random", observation="latency" |
| `add_stats` | — | — | — |
| `add_policy` | — | `levels`, `system` | levels={}, system={} |
| `add_api` | `tag` | `listen`, `services` | listen="127.0.0.1:10085" |
| `modify_field` | `path`, `value` | — | — |

**验证函数**：
```cpp
bool ConfigDialog::validateStepParams(
    const std::string& type, 
    const boost::json::object& params) const {
    
    if (type == "add_outbound") {
        if (!params.contains("tag")) return false;
    } else if (type == "merge_routing") {
        if (!params.contains("inboundTag")) return false;
    } else if (type == "add_inbound") {
        if (!params.contains("tag")) return false;
    } else if (type == "add_observatory") {
        if (!params.contains("subjectSelector")) return false;
    } else if (type == "add_balancer") {
        if (!params.contains("tag")) return false;
    } else if (type == "add_api") {
        if (!params.contains("tag")) return false;
    } else if (type == "modify_field") {
        if (!params.contains("path") || !params.contains("value")) return false;
    }
    // replace_variables, add_stats, add_policy 无必需参数
    
    return true;
}
```

**验证时机**：
1. 用户点击「保存管线」前 — 验证所有步骤参数
2. 用户切换步骤时 — 验证当前步骤参数（可选）
3. 用户添加新步骤时 — 验证参数格式（JSON 语法）

### 7.3 保存前验证（R4 修复）

```cpp
void ConfigDialog::onPipelineSave(wxCommandEvent&) {
    // 1. 验证管线名是否在固定列表中
    static const std::set<std::string> validPipelineNames = {
        "test", "test_singbox",
        "standalone", "standalone_singbox",
        "pool", "pool_singbox"
    };
    if (validPipelineNames.find(currentPipelineName_) == validPipelineNames.end()) {
        wxMessageBox("管线名无效：仅支持 test/standalone/pool × xray/singbox", 
                     "验证错误", wxICON_ERROR);
        return;
    }
    
    // 2. 验证所有步骤参数
    for (const auto& step : pipeline.steps) {
        if (!validateStepParams(step.type, step.params)) {
            wxMessageBox("步骤参数无效: " + wxString(step.type), 
                         "验证错误", wxICON_ERROR);
            return;
        }
    }
    
    // 3. 保存
    savePipelineConfig();
}
```

---

## 8. 测试计划

### 8.1 单元测试

| 测试项 | 说明 |
|--------|------|
| `PipelineTab_LoadEmptyConfig` | 无 config_pipeline 段时正常加载 |
| `PipelineTab_LoadExistingPipeline` | 正确加载已有管线 |
| `PipelineTab_SaveNewPipeline` | 新建管线并保存 |
| `PipelineTab_EditStepParams` | 编辑步骤参数并保存 |
| `PipelineTab_DeletePipeline` | 删除管线 |
| `PipelineTab_StepMoveUp_Down` | 步骤排序 |
| `PipelineTab_InvalidStepType` | 无效步骤类型提示错误 |
| `PipelineTab_JsonParseError` | config.json 损坏时优雅降级 |

### 8.2 UI 集成测试

| 测试项 | 说明 |
|--------|------|
| `UI_PIPELINE_TAB_VISIBLE` | 管线 Tab 可见且可交互 |
| `UI_PIPELINE_SAVE_RELOAD` | 保存后重新加载数据一致 |

---

## 9. 实施计划

| 阶段 | 任务 | 工时 | 依赖 |
|------|------|------|------|
| 1 | 子 Notebook 结构 + 管线列表 | 2h | — |
| 2 | 步骤列表 + 参数编辑器 | 3h | 1 |
| 3 | 步骤 CRUD（添加/删除/排序） | 2h | 2 |
| 4 | 模板预览面板 + 预览生成逻辑 | 2.5h | 3 |
| 5 | config.json 读写 + 重启生效 | 2h | 4 |
| 6 | 验证 + 错误处理 | 1.5h | 5 |
| 7 | 单元测试 + UI 测试 | 2h | 6 |
| **合计** | | **15h** | |

---

## 10. 风险与约束

### 10.1 风险

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| config.json 写入损坏 | 配置丢失 | 写入前备份 + 事务性写入（先写临时文件再 rename） |
| 管线名冲突 | 覆盖已有管线 | 保存前检查 + 确认对话框 |
| 步骤参数格式错误 | 管线执行失败 | 保存前验证 + JSON Schema 校验 |

### 10.2 约束

- **不能破坏现有「模板配置」功能** — 通过子 Notebook 隔离
- **不能引入新的全局状态** — 管线数据在 ConfigDialog 生命周期内管理
- **不支持热加载** — 仅写入 config.json，需重启应用生效（避免 UI 线程与工作线程的数据竞争）

---

## 11. 变更记录

| 版本 | 日期 | 变更内容 | 作者 |
|------|------|----------|------|
| v1.2 | 2026-09-22 | 新增模板预览功能：①三栏布局（管线列表 + 步骤编辑器 + 模板预览） ②预览生成逻辑（调用 StepHandler 执行） ③预览面板控件（wxTextCtrl 只读 + 生成/清空按钮） ④实施计划更新为 15h | AI Assistant |
| v1.1 | 2026-09-22 | 修复评审问题 R1-R5: ①构造函数增加 configPath 参数 ②移除热加载改为重启生效 ③完善参数验证表 ④管线名改为固定列表 ⑤建议 StepHandler 增加 displayName() | AI Assistant |
| v1.0 | 2026-09-22 | 初始版本 — ConfigPipeline UI 编辑功能方案设计 | AI Assistant |
