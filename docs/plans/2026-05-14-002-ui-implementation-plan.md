---
title: "feat: UI 图形界面实现 — 基于 wxWidgets 的桌面客户端"
type: feat
status: draft
date: 2026-05-14
origin: "设计文档 docs/design/ui-design-plan.md"
supersedes: []
---

# UI 图形界面实现计划

> **前置文档:** [UI 设计文档](../../docs/design/ui-design-plan.md) — 含完整架构分层、数据模型映射、组件选型矩阵

**目标:** 为 CLI 工具 `validproxy` 添加 wxWidgets 图形界面，实现订阅管理、代理列表、批量测试、配置编辑等核心功能

**技术栈:** wxWidgets 3.2 (vcpkg), C++17, CMake+Ninja, MinGW

**参考:** `E:\vcpkg\installed\x64-mingw-static\lib\` 中已包含完整 wxWidgets 3.2 静态库

---

## 范围边界

### 修改范围
- `CMakeLists.txt` — 添加 wxWidgets find_package + 链接
- `src/ui/` — 新增 8 组 UI 模块文件 (MainFrame, SubscriptionPanel, ProxyListPanel, TestPanel, LogPanel, ConfigDialog, TrayIcon, AppController)
- `docs/plans/project-plans-tracker.md` — 添加 UI 实现进度跟踪项

### NOT 修改范围
- 不修改任何现有后端模块 (src/*.cpp, include/*.h)
- 不影响 CLI 模式运行 (`-c/-F/-UA` 等参数仍然可用)
- 不引入 wxWidgets 之外的额外依赖
- 不修改数据库 schema

---

## 详细变更

### U1: 构建系统 — CMakeLists.txt wxWidgets 集成

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 添加 wxWidgets 查找和链接**
  ```cmake
  # 在 find_package(SQLite3 REQUIRED) 之后添加
  set(wxWidgets_CONFIG_OPTIONS "--static")
  find_package(wxWidgets REQUIRED COMPONENTS
      core base adv aui html propgrid
  )
  include(${wxWidgets_USE_FILE})
  ```
- [ ] **Step 2: 添加 UI 源文件到 target**
  ```cmake
  target_sources(validproxy PRIVATE
      src/ui/MainFrame.cpp
      src/ui/SubscriptionPanel.cpp
      src/ui/ProxyListPanel.cpp
      src/ui/TestPanel.cpp
      src/ui/LogPanel.cpp
      src/ui/ConfigDialog.cpp
      src/ui/TrayIcon.cpp
      src/ui/AppController.cpp
  )
  ```
- [ ] **Step 3: 链接 wxWidgets 库**
  ```cmake
  target_link_libraries(validproxy PRIVATE ${wxWidgets_LIBRARIES})
  ```
- [ ] **Step 4: 创建 `src/ui/` 目录**
- [ ] **Step 5: 构建验证** — `cmake -B build -G Ninja && cmake --build build`

### U2: 事件系统 — 自定义事件定义

**Files:**
- Create: `src/ui/Events.h`

- [ ] **Step 1: 定义自定义事件类型和事件类**
  ```cpp
  // 事件 ID 范围
  enum class UIEventId {
      SUBSCRIPTION_UPDATED = wxID_HIGHEST + 1,
      PROXY_TEST_PROGRESS,
      PROXY_TEST_COMPLETED,
      PROXY_FOUND,
      LOG_MESSAGE,
      CONFIG_CHANGED,
      STATUS_UPDATE
  };

  // 进度事件
  class ProxyTestProgressEvent : public wxEvent { ... };
  // 日志事件
  class LogMessageEvent : public wxEvent { ... };
  // 状态事件
  class StatusUpdateEvent : public wxEvent { ... };
  ```
- [ ] **Step 2: 实现事件类型注册 (`wxDECLARE_EVENT` / `wxDEFINE_EVENT`)**

### U3: 控制器层 — AppController

**Files:**
- Create: `src/ui/AppController.h`
- Create: `src/ui/AppController.cpp`

- [ ] **Step 1: 设计 AppController 类**
  ```cpp
  class AppController {
  public:
      AppController(sqlite3* db, const config::AppConfig& cfg);
      ~AppController();

      // 订阅操作
      std::vector<db::models::Subitem> loadSubscriptions();
      bool updateSubscription(const std::string& subId);       // -U
      bool updateAllSubscriptions();                            // -UA
      bool importSubscription(const std::string& url);         // -IS

      // 代理操作
      std::vector<db::models::Profileitem> loadProxies(const std::string& subId = "");
      std::vector<db::models::ProfileExItem> loadProxyResults();

      // 测试操作
      bool testSubscription(const std::string& subId);         // -T
      bool cancelTest();

      // 查找操作
      ProxyFinder::TestResult findFirstProxy();                 // -F
      ProxyFinder::TestResult findBestProxy();                  // -FMIN

      // 导出/工具
      bool exportShareLinks();                                  // -TU
      bool deduplicate();                                       // -D
      bool syncDatabases(const std::string& src, const std::string& dst);  // -S
      bool generateConfig(const std::string& indexId);          // -G

      // 配置
      config::AppConfig getConfig() const;
      bool saveConfig(const config::AppConfig& cfg);

  private:
      sqlite3* db_;
      config::AppConfig config_;
      std::atomic<bool> cancelRequested_{false};
      // 后台线程管理
      std::thread workerThread_;
      // wxEvtHandler 用于发送事件到 UI 线程
  };
  ```
- [ ] **Step 2: 实现线程安全的后台操作调度**
  - 所有耗时操作在 `std::thread` 中执行
  - 通过 `wxQueueEvent` 将结果/进度发回 UI 线程
- [ ] **Step 3: 实现测试取消机制 (`cancelRequested_` 原子标志)**

### U4: 主窗口 — MainFrame

**Files:**
- Create: `src/ui/MainFrame.h`
- Create: `src/ui/MainFrame.cpp`

- [ ] **Step 1: 创建 MainFrame 类继承 wxFrame**
  ```cpp
  class MainFrame : public wxFrame {
  public:
      MainFrame(const config::AppConfig& cfg);
      ~MainFrame();

      // 面板访问
      void showSubscriptionPanel();
      void showProxyListPanel();
      void showTestPanel();
      void showLogPanel();

  private:
      void initMenuBar();      // 菜单栏
      void initToolBar();      // 工具栏 (wxAuiToolBar)
      void initStatusBar();    // 状态栏 (3 字段)
      void initAuiManager();   // wxAuiManager 布局
      void initPanels();       // 创建各面板
      void initTrayIcon();     // 系统托盘
      void loadSettings();     // 窗口位置/布局恢复

      wxAuiManager auiManager_;
      AppController* controller_;
      SubscriptionPanel* subPanel_;
      ProxyListPanel* proxyPanel_;
      TestPanel* testPanel_;
      LogPanel* logPanel_;
      ConfigDialog* configDialog_;
      TrayIcon* trayIcon_;
      wxStatusBar* statusBar_;
  };
  ```
- [ ] **Step 2: 实现菜单栏**
  - 文件: 导入订阅, 同步数据库, 退出
  - 订阅: 全部更新, 添加订阅
  - 工具: 查找代理, 去重, 导出分享链接, 生成配置
  - 帮助: 关于
- [ ] **Step 3: 实现工具栏 (wxAuiToolBar)**
  - 全部更新 | 测试 | 查找 | 去重 | 导入 | 配置
- [ ] **Step 4: 实现状态栏 (3 字段)**
  - Xray 状态 / 代理统计 / 最新延迟
- [ ] **Step 5: 实现 wxAuiManager 布局**
  - 左侧: 订阅面板 (默认宽度 280px)
  - 中央: 代理列表 (默认占据大部分空间)
  - 底部: 日志面板 (默认高度 150px, 可折叠)
  - 右侧: 预留详情面板 (可选)
- [ ] **Step 6: 实现窗口状态持久化 (`SavePerspective`/`LoadPerspective`)**
- [ ] **Step 7: 修改 `src/main.cpp` — 添加 `-ui` 参数启动 GUI 模式**

### U5: 订阅面板 — SubscriptionPanel

**Files:**
- Create: `src/ui/SubscriptionPanel.h`
- Create: `src/ui/SubscriptionPanel.cpp`

- [ ] **Step 1: 创建 SubscriptionPanel 类**
  ```cpp
  class SubscriptionPanel : public wxPanel {
      void loadSubscriptions();
      void onAddSubscription(wxCommandEvent&);
      void onEditSubscription(wxCommandEvent&);
      void onDeleteSubscription(wxCommandEvent&);
      void onUpdateSubscription(wxCommandEvent&);
      void onSelectionChanged(wxDataViewEvent&);
  private:
      wxDataViewCtrl* listCtrl_;
      wxDataViewListStore* store_;
      SubscriptionDialog* dialog_;
      // 列: ✓ | 名称 | 代理数 | 更新间隔 | 最后更新 | Url缩写
  };
  ```
- [ ] **Step 2: 实现订阅数据加载 (`SubitemDAO.getAll()`)**
- [ ] **Step 3: 实现右键菜单 (更新/编辑/删除/测试)**
- [ ] **Step 4: 实现添加订阅对话框 (URL + 名称 + UserAgent + 启用状态)**
- [ ] **Step 5: 实现编辑订阅对话框 (全部 15 字段)**
- [ ] **Step 6: 实现删除确认与执行**
- [ ] **Step 7: 实现选中订阅时触发代理列表刷新 (事件通知)**

### U6: 代理列表面板 — ProxyListPanel

**Files:**
- Create: `src/ui/ProxyListPanel.h`
- Create: `src/ui/ProxyListPanel.cpp`

- [ ] **Step 1: 创建 ProxyListPanel 类**
  ```cpp
  class ProxyListPanel : public wxPanel {
      void loadProxies(const std::string& subId = "");
      void setFilter(const std::string& configType, const std::string& status);
      void onSelectionChanged(wxDataViewEvent&);
  private:
      wxDataViewCtrl* listCtrl_;
      wxDataViewVirtualListModel* model_;
      wxSearchCtrl* searchCtrl_;
      wxChoice* typeFilter_;
      wxChoice* statusFilter_;
      std::vector<db::models::Profileitem> proxies_;
      std::vector<db::models::ProfileExItem> exItems_;
      // 自定义渲染: 协议类型颜色标签, 延迟进度条
  };
  ```
- [ ] **Step 2: 实现虚拟列表模型 (wxDataViewVirtualListModel)**
  - 支持 5 万+ 代理数据
  - 按需渲染 (GetValueByRow)
- [ ] **Step 3: 实现列渲染器**
  - ConfigType: 彩色标签 (wxDataViewCustomRenderer)
  - Delay: 进度条 (绿<200ms / 黄<500ms / 红≥500ms)
  - 黑名单行: 红色背景
- [ ] **Step 4: 实现列头排序 (wxDataViewCtrl 原生排序)**
- [ ] **Step 5: 实现筛选工具栏 (协议类型 + 状态 + 搜索框)**
- [ ] **Step 6: 实现右键菜单 (测试/生成配置/查看详情)**
- [ ] **Step 7: 实现列选择器 (35 列可显隐)**

### U7: 测试面板 — TestPanel

**Files:**
- Create: `src/ui/TestPanel.h`
- Create: `src/ui/TestPanel.cpp`

- [ ] **Step 1: 创建 TestPanel 类**
  ```cpp
  class TestPanel : public wxPanel {
      void startTest(const std::string& subId);
      void cancelTest();
      void onProgress(ProxyTestProgressEvent& evt);
      void onCompleted(ProxyTestCompletedEvent& evt);
  private:
      wxGauge* progressBar_;
      wxListCtrl* resultList_;
      wxButton* cancelBtn_;
      wxStaticText* statusLabel_;
      std::atomic<bool> isRunning_;
  };
  ```
- [ ] **Step 2: 实现进度条 (wxGauge — 不确定模式 + 百分比)**
- [ ] **Step 3: 实现实时结果列表 (每完成一个代理追加一行)**
- [ ] **Step 4: 实现测试取消按钮**
- [ ] **Step 5: 实现事件处理 (从 Worker Thread 接收进度/完成事件)**

### U8: 日志面板 — LogPanel

**Files:**
- Create: `src/ui/LogPanel.h`
- Create: `src/ui/LogPanel.cpp`

- [ ] **Step 1: 创建 LogPanel 类**
  ```cpp
  class LogPanel : public wxPanel {
      void appendLog(const std::string& msg, LogLevel level);
      void clearLog();
      void setLevelFilter(LogLevel minLevel);
      void onLogMessage(LogMessageEvent& evt);
  private:
      wxTextCtrl* logCtrl_;
      wxChoice* levelFilter_;
      wxButton* clearBtn_;
  };
  ```
- [ ] **Step 2: 实现日志文本控件 (wxTextCtrl, 只读, 自动滚动到底部)**
- [ ] **Step 3: 实现级别颜色 (TRACE=灰, DEBUG=蓝, INFO=绿, REPORT=青, WARN=黄, ERR=红)**
- [ ] **Step 4: 实现级别过滤下拉框**
- [ ] **Step 5: 实现 Logger 回调集成**
  ```cpp
  // 在 Logger 中添加回调注册
  using LogCallback = std::function<void(const std::string&, LogLevel)>;
  static void setLogCallback(LogCallback cb);
  ```
- [ ] **Step 6: 实现日志清空按钮**

### U9: 配置对话框 — ConfigDialog

**Files:**
- Create: `src/ui/ConfigDialog.h`
- Create: `src/ui/ConfigDialog.cpp`

- [ ] **Step 1: 创建 ConfigDialog 类 (继承 wxDialog)**
  ```cpp
  class ConfigDialog : public wxDialog {
      void loadConfig(const config::AppConfig& cfg);
      config::AppConfig getConfig() const;
      bool saveConfig();
  private:
      wxPropertyGridManager* propGridManager_;
      wxPropertyGridPage* dbPage_;      // database 分组
      wxPropertyGridPage* xrayPage_;    // xray 分组
      wxPropertyGridPage* testPage_;    // test 分组
      wxPropertyGridPage* logPage_;     // log 分组
      wxPropertyGridPage* subPage_;     // subscription 分组
      wxPropertyGridPage* dedupPage_;   // dedup 分组
      wxPropertyGridPage* syncPage_;    // sync 分组
      wxPropertyGridPage* notifyPage_;  // notification 分组
  };
  ```
- [ ] **Step 2: 按功能分组创建属性网格页面**
- [ ] **Step 3: 实现配置加载 → 属性网格映射**
- [ ] **Step 4: 实现属性网格 → 配置保存**
- [ ] **Step 5: 实现验证 (端口范围/URL 格式/路径存在性)**

### U10: 系统托盘 — TrayIcon

**Files:**
- Create: `src/ui/TrayIcon.h`
- Create: `src/ui/TrayIcon.cpp`

- [ ] **Step 1: 创建 TrayIcon 类 (继承 wxTaskBarIcon)**
  ```cpp
  class TrayIcon : public wxTaskBarIcon {
      void onLeftDClick(wxTaskBarIconEvent&);  // 恢复窗口
      void onMenuShow(wxEvent&);                // 显示
      void onMenuHide(wxEvent&);                // 隐藏
      void onMenuExit(wxEvent&);                // 退出
  private:
      wxMenu* CreatePopupMenu() override;
  };
  ```
- [ ] **Step 2: 实现托盘图标 (使用 wxWidgets 内置图标或嵌入资源)**
- [ ] **Step 3: 实现托盘右键菜单 (显示/隐藏/退出)**

### U11: main.cpp 入口修改

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: 添加 `-ui` 参数处理**
  ```cpp
  if (vm.count("ui")) {
      // 启动 GUI 模式
      wxApp::SetInstance(new UIApp());
      return wxEntry(argc, argv);
  }
  ```
- [ ] **Step 2: 保留所有 CLI 参数不变，`-ui` 为新增可选参数**

---

## 验证步骤

| 步骤 | 命令 | 预期结果 |
|---|---|---|
| 1. 构建 | `cmake -B build -G Ninja && cmake --build build` | 编译 0 错误, 生成 `bin/validproxy.exe` |
| 2. CLI 不变 | `.\bin\validproxy.exe -show-sub` | 正常显示订阅列表 (原有 CLI 功能不受影响) |
| 3. GUI 启动 | `.\bin\validproxy.exe -ui` | 主窗口显示, 布局面板正确 |
| 4. 订阅加载 | GUI 启动后自动加载 | 左侧订阅列表显示所有订阅项 |
| 5. 代理加载 | 点击订阅项 | 中央代理列表显示对应代理数据 |
| 6. 配置编辑 | 菜单 → 设置 → 配置 | 属性网格显示正确, 保存后生效 |
| 7. 日志显示 | 启动后 | 底部日志面板显示启动日志 |
| 8. 托盘功能 | 最小化窗口 | 托盘图标显示, 右键菜单工作 |
| 9. 单元测试 | `ctest -V` | 现有测试全部通过 |

---

## 文件变更列表

### 新增文件 (16 个)
| 文件 | 归属单元 | 行数估算 |
|---|---|---|
| `src/ui/Events.h` | U2 | ~80 |
| `src/ui/AppController.h` | U3 | ~100 |
| `src/ui/AppController.cpp` | U3 | ~350 |
| `src/ui/MainFrame.h` | U4 | ~80 |
| `src/ui/MainFrame.cpp` | U4 | ~400 |
| `src/ui/SubscriptionPanel.h` | U5 | ~60 |
| `src/ui/SubscriptionPanel.cpp` | U5 | ~350 |
| `src/ui/ProxyListPanel.h` | U6 | ~80 |
| `src/ui/ProxyListPanel.cpp` | U6 | ~500 |
| `src/ui/TestPanel.h` | U7 | ~50 |
| `src/ui/TestPanel.cpp` | U7 | ~300 |
| `src/ui/LogPanel.h` | U8 | ~50 |
| `src/ui/LogPanel.cpp` | U8 | ~250 |
| `src/ui/ConfigDialog.h` | U9 | ~60 |
| `src/ui/ConfigDialog.cpp` | U9 | ~300 |
| `src/ui/TrayIcon.h` | U10 | ~40 |
| `src/ui/TrayIcon.cpp` | U10 | ~100 |

### 修改文件 (2 个)
| 文件 | 变更 |
|---|---|
| `CMakeLists.txt` | wxWidgets find_package + UI 源文件 + 链接 |
| `src/main.cpp` | 添加 `-ui` 参数处理 |
| `docs/plans/project-plans-tracker.md` | 添加 UI 实现跟踪项 |

---

## 风险

| 风险 | 影响 | 对策 |
|---|---|---|
| wxWidgets 头文件路径与 MinGW 不兼容 | 构建失败 | vcpkg 已提供编译好的库; 如有路径问题可手动设置 `wxWidgets_INCLUDE_DIRS` |
| wxDataViewVirtualListModel 处理 5 万行性能 | 列表卡顿 | 使用虚拟模式 (只渲染可见行); 设置 `wxDV_VARIABLE_LINE_HEIGHT` 禁用 |
| Logger 回调线程安全 | 日志丢失/竞态 | Logger 内部已有 `std::mutex`; 回调调用时保持互斥 |
| wxWidgets 事件泄漏 | 内存泄漏 | 所有 `wxQueueEvent` 使用堆分配, wxWidgets 自动释放 |
| main.cpp 同时支持 CLI 和 GUI 模式 | 入口冲突 | `wxApp` 和普通 `main()` 通过条件编译分离 |

---

## 依赖关系

```
U1 (CMake) ── 前置条件, 所有 UI 模块依赖
  ├── U2 (Events) ── 被 U3, U4, U5, U6, U7, U8 依赖
  ├── U3 (AppController) ── 被 U4, U5, U6, U7 依赖
  ├── U4 (MainFrame) ── 依赖 U2, U3, U5, U6, U7, U8, U9, U10
  │   ├── U5 (SubscriptionPanel) ── 依赖 U3
  │   ├── U6 (ProxyListPanel) ── 依赖 U3
  │   ├── U7 (TestPanel) ── 依赖 U3
  │   ├── U8 (LogPanel) ── 依赖 U2
  │   ├── U9 (ConfigDialog) ── 无后端依赖
  │   └── U10 (TrayIcon) ── 无后端依赖
  └── U11 (main.cpp) ── 依赖 U4
```

**执行顺序建议:**

```
Phase 1 (独立) : U1 → U2 → U3
Phase 2 (独立) : U4 + U8 + U9 + U10 (可并行)
Phase 3 (独立) : U5 + U6 + U7 (可并行, 依赖 Phase 1)
Phase 4 (集成) : U11 + 集成测试
```
