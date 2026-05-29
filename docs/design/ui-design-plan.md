# UI 界面建设方案设计文档

> 版本: v1.0
> 状态: draft
> 日期: 2026-05-14

## 1. 项目架构基准评估

### 1.1 现状总览

| 维度 | 现状 |
|---|---|
| 语言 | C++17 |
| 构建系统 | CMake 3.20+ / Ninja |
| 编译器 | MinGW (GCC) |
| 目标平台 | **仅 Windows** |
| 源码文件 | 15 `.cpp` + 19 `.h` |
| 数据库 | SQLite3 (raw `sqlite3*` API) |
| 网络 | libcurl |
| JSON | Boost.JSON |
| 日志 | 自定义 Logger 类 |
| 并发 | std::thread + std::mutex + std::atomic |
| Xray集成 | 子进程管理 (grpc 风格 CLI) |
| 输出目录 | `bin/validproxy.exe` |

### 1.2 现有模块清单

所有后端模块均会保留不变，UI 层在其上封装调用：

| 模块 | 文件 | 核心职责 |
|---|---|---|
| ConfigReader | `src/ConfigReader.cpp` | 读取 `config.json` → `AppConfig` |
| DatabaseHelper | `include/DatabaseHelper.h` | SQLite3 数据库打开/关闭/执行 |
| Profileitem | `include/Profileitem.h` | 代理配置数据模型 + DAO |
| ProfileExItem | `include/ProfileExItem.h` | 代理测试结果数据模型 + DAO |
| Subitem | `include/Subitem.h` | 订阅数据模型 + DAO |
| XrayManager | `src/XrayManager.cpp` | xray 实例单例管理 (启动/停止/端口分配) |
| XrayInstance | `src/XrayInstance.cpp` | 单个 xray 进程生命周期 |
| XrayApi | `src/XrayApi.cpp` | xray gRPC API 通信 (addOutbound/removeOutbound) |
| ProxyBatchTester | `src/ProxyBatchTester.cpp` | 多线程并发测试代理 |
| ProxyFinder | `src/ProxyFinder.cpp` | 查找第一个可用/最小延迟代理 |
| ProxyTester | `src/ProxyTester.cpp` | 单个代理测试 (CURL + Xray 注入) |
| ConfigGenerator | `src/ConfigGenerator.cpp` | 生成 outbound JSON 配置 |
| SubitemUpdaterV2 | `src/SubitemUpdaterV2.cpp` | 订阅更新 + 去重 + 同步 + 导入 |
| PortManager | `src/PortManager.cpp` | SOCKS5/API 端口分配与回收 |
| ShareLink | `src/ShareLink.cpp` | 分享链接导出 |
| UrlFetcher | `src/UrlFetcher.cpp` | URL 内容获取 (CURL 封装) |
| CurlEasyHandle | `src/CurlEasyHandle.cpp` | CURL RAII 封装 |
| Logger | `src/Logger.cpp` | 统一日志 (文件+控制台) |
| Utils | `src/Utils.cpp` | 通用工具函数 |

## 2. 技术选型

### 2.1 UI 框架选择: wxWidgets

**决策理由:**

项目当前 vcpkg 环境 (`E:\vcpkg\installed\x64-mingw-static`) 已完整安装了 wxWidgets 3.2 系列全部静态库，**零额外依赖安装成本**。

| 已安装的 wxWidgets 库 | 用途 |
|---|---|
| `libwxmsw32u_core.a` | 核心控件 (窗口、按钮、列表、对话框) |
| `libwxbase32u.a` | 基础 (事件、线程、文件、配置读写) |
| `libwxmsw32u_adv.a` | 高级控件 (BitmapComboBox, 超链接) |
| `libwxmsw32u_aui.a` | 可停靠面板、多文档界面布局 |
| `libwxmsw32u_html.a` | HTML 内容渲染 (可用于日志高亮) |
| `libwxmsw32u_propgrid.a` | **属性网格 — 编辑 config.json 的理想控件** |
| `libwxmsw32u_stc.a` | 带语法高亮的文本控件 |
| `libwxmsw32u_ribbon.a` | Ribbon 工具栏 |
| `libwxmsw32u_xrc.a` | XML 资源 (UI 布局与代码分离) |
| `libwxbase32u_net.a` | 网络支持 |
| `libwxbase32u_xml.a` | XML 解析 |

**技术优势对比:**

| 候选方案 | 与 MinGW 兼容 | 需新增依赖 | 原生体验 | C++ 集成度 |
|---|---|---|---|---|
| **wxWidgets** ✅ | ✅ 已验证 | ❌ 零新增 | ✅ 原生 | ✅ 最高 |
| Qt | ✅ | ❌ 需安装 Qt SDK | ✅ 原生 | ✅ 高 (但 boost 集成冲突) |
| Embedded Web (React/etc) | ✅ | ❌ 需 node + WebSocket | ❌ 非原生 | ⚠️ 需 IPC |
| FLTK | ✅ | ❌ 需安装 | ✅ 原生 | ✅ 高 |
| imgui | ✅ | ❌ 零新增 | ⚠️ 即时模式 | ✅ 高 (但缺标准控件) |

### 2.2 布局框架选择: wxAUI

选择 `wxAUI` (Advanced User Interface) 作为主窗口布局管理器，原因:
- 面板可拖拽、可停靠、可关闭
- 布局状态可持久化 (SavePerspective/RestorePerspective)
- 适合同时展示订阅列表、代理列表、日志等多个面板
- 类似 Visual Studio 的 MDI 体验

## 3. 数据模型映射

### 3.1 ProfileItem — 代理配置 (35 字段)

数据库表 `ProfileItem`，对应 C++ 结构体 `db::models::Profileitem`:

| # | 字段 | 类型 | 长度/格式 | UI 展示控件 | 可编辑 |
|---|---|---|---|---|---|
| 0 | indexid | TEXT | 定长 | wxStaticText (只读) | ❌ |
| 1 | configtype | TEXT | 2 | wxChoice (下拉) | ✅ |
| 2 | configversion | TEXT | - | wxStaticText | ❌ |
| 3 | address | TEXT | - | wxTextCtrl | ✅ |
| 4 | port | TEXT | ≤5 | wxTextCtrl (数字校验) | ✅ |
| 5 | id | TEXT | UUID | wxTextCtrl | ✅ |
| 6 | alterid | TEXT | - | wxTextCtrl | ✅ |
| 7 | security | TEXT | - | wxChoice | ✅ |
| 8 | network | TEXT | - | wxChoice | ✅ |
| 9 | remarks | TEXT | - | wxTextCtrl | ✅ |
| 10 | headertype | TEXT | - | wxChoice | ✅ |
| 11 | requesthost | TEXT | - | wxTextCtrl | ✅ |
| 12 | path | TEXT | - | wxTextCtrl | ✅ |
| 13 | streamsecurity | TEXT | - | wxChoice | ✅ |
| 14-34 | (allowinsecure..echforcequery) | TEXT | - | 只读/高级编辑 | 视情况 |
| - | subid | TEXT | 19 | 关联订阅标签 | ❌ |
| - | issub | TEXT | 1 | 只读标识 | ❌ |

**协议类型映射 (configtype):**
| 值 | 协议 | 颜色标识 |
|---|---|---|
| 1 | VMess | 蓝 |
| 3 | SS (Shadowsocks) | 绿 |
| 4 | SOCKS5 | 灰 |
| 5 | VLESS | 紫 |
| 6 | Trojan | 红 |
| 7 | Hysteria2 | 橙 |
| 8 | TUIC | 青 |
| 9 | Juicity | 粉 |
| 10 | ShadowTLS | 黄 |

### 3.2 ProfileExItem — 代理测试结果 (6 字段)

数据库表 `ProfileExItem`，与 ProfileItem 通过 `indexid` 关联:

| 字段 | 类型 | UI 展示 | 说明 |
|---|---|---|---|
| indexid | TEXT | 隐藏 (JOIN 关联) | 外键 → ProfileItem.indexid |
| delay | TEXT (整数) | **wxGauge** + wxStaticText | 延迟毫秒数，可转为颜色 (绿<200, 黄<500, 红≥500) |
| speed | TEXT (整数) | **wxGauge** + wxStaticText | 速度 KB/s |
| sort | TEXT (整数) | 排序索引 (隐藏) | 排序用 |
| message | TEXT | wxStaticText | 测试结果消息 |
| consecutive_failures | INTEGER | 条件格式 (≥阈值红色高亮) | 连续失败次数，用于黑名单判定 |

### 3.3 Subitem — 订阅源 (15 字段)

数据库表 `Subitem`，对应 C++ 结构体 `db::models::Subitem`:

| # | 字段 | 类型 | UI 展示 | 可编辑 |
|---|---|---|---|---|
| 0 | id | TEXT (19位数字) | 只读标签 | ❌ |
| 1 | remarks | TEXT | wxTextCtrl | ✅ |
| 2 | url | TEXT | wxTextCtrl | ✅ |
| 3 | moreurl | TEXT | wxTextCtrl | ✅ |
| 4 | enabled | TEXT ("True"/"False") | wxCheckBox | ✅ |
| 5 | useragent | TEXT | wxTextCtrl | ✅ |
| 6 | sort | TEXT (整数) | wxSpinCtrl | ✅ |
| 7 | filter | TEXT | wxTextCtrl | ✅ |
| 8 | autoupdateinterval | TEXT (整数, 分钟) | wxSpinCtrl | ✅ |
| 9 | updatetime | TEXT (时间戳) | 只读标签 | ❌ |
| 10 | converttarget | TEXT | wxChoice | ✅ |
| 11 | prevprofile | TEXT | 隐藏 | ❌ |
| 12 | nextprofile | TEXT | 隐藏 | ❌ |
| 13 | presocksport | TEXT | wxSpinCtrl (端口) | ✅ |
| 14 | memo | TEXT | wxTextCtrl (多行) | ✅ |

### 3.4 AppConfig — 应用配置结构

读取自 `config.json`，对应 C++ 结构体 `config::AppConfig`:

```json
{
  "database": {
    "path": "/path/to/guindb.db",
    "sql": "SELECT ... WHERE ...",
    "sql_by_subid": "SELECT ... WHERE subid = {subid} ..."
  },
  "xray": {
    "executable": "/path/to/xray.exe",
    "workers": 4,
    "start_port": 1080,
    "api_port": 10080
  },
  "test": {
    "url": "https://www.google.com/generate_204",
    "timeout_ms": 5000
  },
  "log": {
    "enabled": true,
    "network_failures": false,
    "console_level": "INFO",
    "file_level": "ERROR"
  },
  "subscription": {
    "priority_mode": "proxy_first",
    "check_auto_update_interval": true
  },
  "dedup": {
    "enabled": true,
    "dedup_after_update": false,
    "blacklist_threshold": 5,
    "subids": ["...", "..."]
  },
  "sync": {
    "source_db": "/path/to/source.db",
    "target_db": "/path/to/target.db",
    "sync_skip_subids": true
  },
  "notification": {
    "enabled": false,
    "on_update": false,
    "on_test": false
  }
}
```

UI 编辑推荐使用 **wxPropertyGridManager**，将每个配置项映射为属性网格条目，按功能分组 (`database / xray / test / log / subscription / dedup / sync`)。

## 4. 架构分层设计

### 4.1 三层架构

```
┌──────────────────────────────────────────────────────────────┐
│                        UI LAYER                              │
│  wxWidgets (MainFrame, Dialogs, wxAUI Panels, Controls)     │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐        │
│  │ 订阅列表  │ │ 代理列表  │ │ 测试面板  │ │ 日志面板  │       │
│  └────┬─────┘ └────┬─────┘ └────┬─────┘ └────┬─────┘        │
├───────┼────────────┼────────────┼────────────┼───────────────┤
│       ▼            ▼            ▼            ▼               │
│                    CONTROLLER LAYER                          │
│  UIActions — CLI 命令对应的 UI 操作封装                       │
│  ┌─────────────────────────────────────────────────┐         │
│  │ SubscriptionController                           │         │
│  │ ProxyTestController                              │         │
│  │ ProxyFinderController                            │         │
│  │ ConfigController                                 │         │
│  │ ExportController                                 │         │
│  └──────────┬──────────────────────────┬────────────┘         │
├─────────────┼──────────────────────────┼──────────────────────┤
│             ▼                          ▼                      │
│                    BACKEND LAYER (现有代码)                    │
│  SubitemUpdaterV2  ProxyBatchTester  ProxyFinder             │
│  ConfigGenerator   ShareLink         ConfigReader            │
│  XrayManager       XrayInstance      XrayApi                │
│  DatabaseHelper    PortManager       Logger                  │
│  ProfileItem/ExItem DAO              UrlFetcher              │
└──────────────────────────────────────────────────────────────┘
```

### 4.2 模块解耦原则

1. **UI Layer** — 只依赖 Controller 层接口，不直接调用后端
2. **Controller Layer** — 封装后端调用，在**非 UI 线程**执行耗时操作，通过 wxEvtHandler 派发结果到 UI 线程
3. **Backend Layer** — 完全不感知 UI 存在，保持原样

### 4.3 线程模型

```
┌─────────────────────────────────────────────┐
│ Main Thread (wxWidgets UI Thread)           │
│  - 事件循环 (wxEvtLoop)                     │
│  - 界面更新                                 │
│  - 通过 wxPostEvent / wxQueueEvent 接收     │
│    后台操作完成通知                          │
├─────────────────────────────────────────────┤
│ Worker Thread(s) (std::thread)              │
│  - 执行 SubitemUpdaterV2::run()            │
│  - 执行 ProxyBatchTester::run()            │
│  - 执行 ProxyFinder::findWorkingProxy()    │
│  - 执行 ShareLink::exportAll()             │
│  - 执行同步/去重/导入等耗时操作              │
│  - 通过 wxEvtHandler 发送进度/完成事件      │
├─────────────────────────────────────────────┤
│ Xray Process(es) (子进程)                   │
│  - 由 XrayManager 管理                      │
│  - 通过 gRPC CLI + stdout 通信             │
└─────────────────────────────────────────────┘
```

## 5. UI 组件选型矩阵

| 功能区域 | 推荐 wxWidgets 控件 | 备选 | 说明 |
|---|---|---|---|
| **主窗口布局** | `wxAuiManager` | `wxSplitterWindow` | 可拖拽停靠面板 |
| **订阅列表** | `wxDataViewCtrl` (虚拟模式) | `wxListCtrl` | 支持排序、自定义渲染 |
| **代理列表** | `wxDataViewCtrl` + `wxDataViewListStore` | `wxGrid` | 35 列需横向滚动，列可选择显隐 |
| **代理详情** | `wxPropertyGridManager` | `wxDialog` + 表单 | 按协议分组显示属性 |
| **测试进度** | `wxGauge` (不确定模式) + `wxListCtrl` | - | 实时显示测试进度和结果 |
| **查找代理** | `wxDialog` + `wxChoice` + `wxStaticText` | - | 选择查找模式 (-F / -FMIN) |
| **日志** | `wxTextCtrl` (wxTE_MULTILINE + wxTE_READONLY) | `wxStyledTextCtrl` | 可追加内容，自动滚动 |
| **配置编辑** | `wxPropertyGridManager` | `wxDialog` + 表单 | 属性网格按功能分组 |
| **工具栏** | `wxAuiToolBar` | `wxToolBar` | 放置常用操作按钮 |
| **菜单栏** | `wxMenuBar` + `wxMenu` | - | 文件、订阅、工具、帮助 |
| **状态栏** | `wxStatusBar` (多字段) | - | Xray 状态、代理计数、延迟 |
| **系统托盘** | `wxTaskBarIcon` | - | 最小化托盘，右键菜单 |
| **对话框** | `wxDialog` + `wxStdDialogButtonSizer` | - | 添加/编辑/导入/确认 |
| **文件选择** | `wxFileDialog` / `wxDirDialog` | - | 选择数据库/配置文件路径 |
| **订阅添加** | `wxDialog` + `wxTextCtrl` (URL) + `wxChoice` | - | URL/文件导入 |

## 6. CLI 命令 → UI 操作映射

| CLI 命令 | UI 入口 | Controller 调用 | 数据流 |
|---|---|---|---|
| `-show-sub` | 左侧面板自动加载 | `SubitemDAO.getAll()` | DB → UI Table |
| `-U <id>` | 右键菜单"更新" / 工具栏按钮 | `SubitemUpdaterV2.runSingle(id)` | URL → fetch → parse → DB → UI 刷新 |
| `-UA` | 工具栏"全部更新" | `SubitemUpdaterV2.run()` | 循环所有 Subitem, 同上 |
| `-F` | 工具栏"查找可用" | `ProxyFinder.findFirstWorkingProxy()` | DB → Xray 注入 → 测试 → 结果弹窗 |
| `-FMIN` | 工具栏"查找最优" | `ProxyFinder.findWorkingProxy()` | DB → 批量测试 → 排序 → 结果面板 |
| `-T <id>` | 点击"测试" / 右键 | `ProxyBatchTester.runWithSubId(id)` | DB → Xray 批量 → 进度条 → DB 更新 → UI 刷新 |
| `-G <id>` | 右键"生成配置" | `ConfigGenerator.generateJson(id)` | DB → JSON → 保存文件 |
| `-TU` | 菜单"导出分享链接" | `ShareLink::exportAll()` | DB → 写文件 |
| `-D` | 菜单"去重" | `SubitemUpdaterV2.deduplicate()` | DB → 去重 → DB 更新 |
| `-S` | 菜单"同步数据库" | `SubitemUpdaterV2.syncDatabases()` | 源DB → 筛选 → 目标DB |
| `-IS` | 菜单"导入订阅" | `SubitemUpdaterV2.importSubitemsFromFile()` | 文件/URL → parse → DB |
| `-c` | 菜单"设置"→"配置" | `ConfigReader::load()` + 属性网格编辑 | 文件读写 |

## 7. 构建系统集成

### 7.1 CMakeLists.txt 修改

```cmake
# 在现有内容基础上添加:

# wxWidgets
set(wxWidgets_CONFIG_OPTIONS "--static")
find_package(wxWidgets REQUIRED COMPONENTS
    core base adv aui html propgrid stc net xml
)
include(${wxWidgets_USE_FILE})

# UI 源文件
set(UI_SOURCES
    src/ui/MainFrame.cpp
    src/ui/MainFrame.h
    src/ui/SubscriptionPanel.cpp
    src/ui/SubscriptionPanel.h
    src/ui/ProxyListPanel.cpp
    src/ui/ProxyListPanel.h
    src/ui/TestPanel.cpp
    src/ui/TestPanel.h
    src/ui/LogPanel.cpp
    src/ui/LogPanel.h
    src/ui/ConfigDialog.cpp
    src/ui/ConfigDialog.h
    src/ui/TrayIcon.cpp
    src/ui/TrayIcon.h
    src/ui/AppController.cpp
    src/ui/AppController.h
)

# 添加到现有 target
target_sources(validproxy PRIVATE ${UI_SOURCES})
target_link_libraries(validproxy PRIVATE ${wxWidgets_LIBRARIES})
```

### 7.2 新增目录结构

```
src/ui/
├── MainFrame.h/cpp          # 主窗口 (wxFrame + wxAuiManager)
├── SubscriptionPanel.h/cpp  # 订阅列表面板 (wxDataViewCtrl)
├── ProxyListPanel.h/cpp     # 代理列表面板 (wxDataViewCtrl)
├── TestPanel.h/cpp          # 测试面板 (wxGauge + wxListCtrl)
├── LogPanel.h/cpp           # 日志面板 (wxTextCtrl)
├── ConfigDialog.h/cpp       # 配置编辑对话框 (wxPropertyGrid)
├── TrayIcon.h/cpp           # 系统托盘 (wxTaskBarIcon)
├── AppController.h/cpp      # 控制器 (封装后端调用 + 线程管理)
└── Events.h                 # 自定义事件定义 (wxEvtHandler 派生)
```

## 8. 主窗口布局设计

```
┌──────────────────────────────────────────────────────────────────┐
│ [菜单栏] 文件 | 订阅 | 工具 | 导出 | 帮助                       │
├──────────────────────────────────────────────────────────────────┤
│ [工具栏] 全部更新 | 测试订阅 | 查找代理 | 去重 | 导入 | 配置    │
├──────────┬──────────────────────────────┬────────────────────────┤
│          │                              │                        │
│ 左侧面板 │     中央面板(可切换标签)     │    右侧面板(可选)      │
│ (订阅列表│ ┌──────────────────────────┐ │  (代理详情/属性)      │
│  wxAui) │ │ 代理列表 (wxDataViewCtrl)│ │  wxPropertyGrid       │
│          │ │ 35 列, 支持排序/筛选     │ │                        │
│ 每个     │ │ ConfigType 彩色标签       │ │  选中代理后显示       │
│ 订阅项   │ │ Delay/Speed 进度条        │ │  全部 35 个属性       │
│ 显示:    │ │ 行颜色 (黑名单红色)       │ │                        │
│ ✓ 名称   │ └──────────────────────────┘ │                        │
│ ✓ 启用   │ ┌──────────────────────────┐ │                        │
│ ✓ 代理数 │ │ 测试面板 (下方)          │ │                        │
│ ✓ 更新   │ │ wxGauge 进度条           │ │                        │
│   时间   │ │ 实时结果列表             │ │                        │
│          │ └──────────────────────────┘ │                        │
├──────────┴──────────────────────────────┴────────────────────────┤
│ [日志面板] (底部, 可折叠/拖拽)                                  │
│ 实时滚动日志, 级别颜色区分 (TRACE/DEBUG/INFO/WARN/ERR)          │
├──────────────────────────────────────────────────────────────────┤
│ [状态栏] Xray: 运行中 | 代理: 53,837 | 有效: 12,450 | 延迟: 238ms│
└──────────────────────────────────────────────────────────────────┘
```

## 9. 实施路线图

### Phase 1: 主窗口骨架 (估算: 3-5天)

- [ ] CMakeLists.txt wxWidgets 集成
- [ ] `src/ui/` 目录结构创建
- [ ] MainFrame 主窗口 + wxAuiManager 布局
- [ ] 菜单栏 + 工具栏 (wxAuiToolBar)
- [ ] 状态栏 (wxStatusBar)
- [ ] 配置加载 (AppConfig → 主窗口初始化)
- [ ] 系统托盘 (wxTaskBarIcon)
- [ ] 构建验证: `cmake --build build`

### Phase 2: 订阅管理 (估算: 3-5天)

- [ ] SubscriptionPanel (wxDataViewCtrl 虚拟模式)
- [ ] 订阅列表加载 (`SubitemDAO.getAll()`)
- [ ] 添加订阅对话框 (URL + 名称 + User-Agent)
- [ ] 编辑订阅对话框
- [ ] 删除订阅 (带确认)
- [ ] 右键菜单 (更新/测试/编辑/删除)
- [ ] 订阅更新执行 (`SubitemUpdaterV2.runSingle/run`)
- [ ] 导入订阅 (文件和 URL)

### Phase 3: 代理列表 (估算: 3-5天)

- [ ] ProxyListPanel (wxDataViewCtrl)
- [ ] 代理数据加载 (ProfileItem + ProfileExItem JOIN)
- [ ] 列选择器 (35 列可显隐)
- [ ] 排序支持 (点击列头)
- [ ] 筛选栏 (按 ConfigType / Subid / 延迟范围)
- [ ] 行颜色渲染 (黑名单红色, 协议类型颜色)
- [ ] 延迟/速度进度条渲染
- [ ] 代理详情属性网格 (选中代理时更新)

### Phase 4: 测试与查找 (估算: 3-5天)

- [ ] TestPanel (wxGauge + 结果列表)
- [ ] `ProxyBatchTester::runWithSubId` 调用集成
- [ ] 进度更新事件传递 (Worker Thread → UI)
- [ ] 实时结果行追加
- [ ] 测试取消支持 (原子标志位)
- [ ] 查找代理对话框 (-F / -FMIN)
- [ ] 配置生成对话框 (-G)
- [ ] 导出分享链接 (-TU)

### Phase 5: 配置与日志 (估算: 2-3天)

- [ ] ConfigDialog (wxPropertyGridManager)
- [ ] 配置项分组展示
- [ ] 编辑 + 保存 + 重载
- [ ] LogPanel (wxTextCtrl)
- [ ] Logger 重定向到 UI 面板 (Logger::setOutputCallback)
- [ ] 日志级别过滤
- [ ] 日志清空/复制

### Phase 6: 辅助功能与收尾 (估算: 2-3天)

- [ ] 去重功能 (-D)
- [ ] 同步数据库 (-S) 对话框
- [ ] 数据库切换/路径选择
- [ ] 窗口布局持久化 (wxAuiManager::SavePerspective)
- [ ] 托盘最小化 + 托盘菜单
- [ ] 异常处理与错误提示
- [ ] 帮助/关于对话框

**总估算: 16-26 天 (单人)**

## 10. 风险与对策

| 风险 | 影响 | 对策 |
|---|---|---|
| wxWidgets 3.2 与 MinGW 编译兼容性问题 | 构建失败 | vcpkg 已有预编译库；必要时降级到 3.0 |
| 35 列 DataView 性能 (数据量 5万+) | 列表卡顿 | 使用虚拟模式 (wxDataViewVirtualListModel)，按需加载 |
| Logger 当前写入文件，UI 面板需实时日志 | 日志延迟 | Logger 添加回调接口 `setOutputCallback`，面板订阅 |
| XrayManager 为单例，UI 可能多操作冲突 | 竞态条件 | Controller 层加操作队列/互斥，一次只允许一个后端操作 |
| 大规模测试 (5万+代理) 会长时间阻塞 | UI 假死 | 始终在 Worker Thread 执行后端操作；UI 显示进度条 |
| wxPropertyGrid 对复杂 JSON 嵌套支持有限 | 配置编辑困难 | 分组平面化；嵌套 JSON 以字符串编辑框展示 |
| MinGW 静态链接 wxWidgets 导致体积膨胀 | exe 变大 | Release 构建 strip；图标/资源压缩 |

## 11. 附录

### 11.1 数据库表关系

```
Subitem (订阅源)
  └─ id ───────────────────────────┐
                                   │
ProfileItem (代理配置)              │
  ├─ indexid (PK)                  │
  └─ subid (FK) ───────────────────┘
       │
       │ 1:1
       │
ProfileExItem (测试结果)
  ├─ indexid (PK/FK → ProfileItem.indexid)
  ├─ delay
  ├─ speed
  ├─ message
  └─ consecutive_failures (黑名单阈值)
```

### 11.2 ConfigType 与协议对照

| ConfigType | 协议 | outbound 协议标记 |
|---|---|---|
| 1 | VMess | "vmess" |
| 3 | Shadowsocks | "shadowsocks" |
| 4 | SOCKS5 | "socks" |
| 5 | VLESS | "vless" |
| 6 | Trojan | "trojan" |
| 7 | Hysteria2 | "hysteria2" |
| 8 | TUIC | "tuic" |
| 9 | Juicity | "juicity" |
| 10 | ShadowTLS | "shadowtls" |

### 11.3 搜索结果: wxWidgets 3.2 vcpkg 包验证

```
E:\vcpkg\installed\x64-mingw-static\lib\ 中包含:
  libwxbase32u.a
  libwxbase32u_net.a
  libwxbase32u_xml.a
  libwxmsw32u_adv.a
  libwxmsw32u_aui.a
  libwxmsw32u_core.a
  libwxmsw32u_gl.a
  libwxmsw32u_html.a
  libwxmsw32u_propgrid.a
  libwxmsw32u_qa.a
  libwxmsw32u_ribbon.a
  libwxmsw32u_richtext.a
  libwxmsw32u_stc.a
  libwxmsw32u_xrc.a
  + 对应头文件在 E:\vcpkg\installed\x64-mingw-static\include\wx\
```

确认 wxWidgets 3.2 (对应 `32u` 版本号) 已完整安装且为 MinGW 静态编译。
