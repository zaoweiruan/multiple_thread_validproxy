#include "StandaloneFloatingWidget.h"
#include "AddPoolMemberDialog.h"
#include "AppController.h"
#include "Events.h"
#include "Logger.h"
#include "TestOnlineResultDialog.h"
#include "ToolbarIcons.h"

#include <wx/sizer.h>
#include <wx/dcclient.h>
#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/region.h>
#include <wx/gdicmn.h>
#include <wx/colour.h>
#include <wx/utils.h>   // wxGetLocalTimeMillis / ::GetDoubleClickTime(MSW)

#ifdef __WXMSW__
#  include <wx/msw/wrapwin.h>
#endif

#include <string>
#include <vector>
#include <algorithm>  // std::clamp

namespace {

enum ColumnId {
    COL_TYPE = 0,          // 类型：独立 / 代理池
    COL_INDEX_ID,          // 标识：独立代理 indexId / 池成员 px-<indexId>
    COL_HOST,              // Host：profile address
    COL_SOCKS_PORT,        // 监听端口
    COL_STATE,             // 状态：运行中 / active / remove-requested / draining
    COL_DELAY_MS,          // 延迟(ms)
    COL_FAIL_STREAK,       // 失败次数
    COL_PID                // PID
};

// 悬浮窗右键菜单项唯一 ID。必须独占且互不相同，且不得使用 wxID_ANY：
// Bind(wxEVT_MENU, handler, this) 不带 id 时绑定到 wxID_ANY，会匹配菜单
// 上的所有菜单事件，导致点击「关闭代理」的同时也触发「测试在线代理」。
enum MenuId {
    ID_MENU_CLOSE_PROXY = wxID_HIGHEST + 500,
    ID_MENU_TEST_ONLINE = wxID_HIGHEST + 501,
    ID_MENU_LOCATE_PROXY = wxID_HIGHEST + 503
};

} // namespace

// ----------------------------------------------------------------
// CustomColorSlider — wxWindow 自绘滑块，完全接管绘制。
// 配色：背景 #F5F6F7，groove #D1D1D1，填充 #FFF9C4，thumb #0078D4。
// API 兼容 wxSlider 子集（GetValue / SetValue / SetToolTip / Hide / Show）。
// 注意：类定义在全局作用域（非匿名命名空间），与头文件前向声明一致。
// ----------------------------------------------------------------

class CustomColorSlider : public wxWindow {
public:
    CustomColorSlider(wxWindow* parent, wxWindowID id, int value,
                      int minVal, int maxVal,
                      const wxPoint& pos = wxDefaultPosition,
                      const wxSize& size = wxDefaultSize)
        : wxWindow(parent, id, pos, size),
          value_(value), min_(minVal), max_(maxVal), dragging_(false)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetMinSize(wxSize(120, 22));
        Bind(wxEVT_PAINT, &CustomColorSlider::onPaint, this);
        Bind(wxEVT_LEFT_DOWN, &CustomColorSlider::onLeftDown, this);
        Bind(wxEVT_LEFT_UP, &CustomColorSlider::onLeftUp, this);
        Bind(wxEVT_MOTION, &CustomColorSlider::onMotion, this);
        Bind(wxEVT_MOUSE_CAPTURE_LOST, &CustomColorSlider::onCaptureLost, this);
    }

    int GetValue() const { return value_; }

    void SetValue(int v) {
        v = std::clamp(v, min_, maxVal());
        if (v != value_) {
            value_ = v;
            Refresh();
        }
    }

    int GetMin() const { return min_; }
    int GetMax() const { return maxVal(); }

    void SetRange(int minVal, int maxVal) {
        min_ = minVal;
        max_ = maxVal;
        value_ = std::clamp(value_, min_, max_);
        Refresh();
    }

private:
    int value_;
    int min_;
    int max_;
    bool dragging_;

    int maxVal() const { return max_; }

    // ---- geometry helpers ----
    struct TrackGeom {
        int trackL, trackR, trackY, trackH;
        int thumbR;
        int clientW, clientH;
    };

    TrackGeom geom() const {
        TrackGeom g;
        const wxSize sz = GetClientSize();
        g.clientW = sz.x;
        g.clientH = sz.y;
        g.thumbR  = 8;
        g.trackH  = 4;
        g.trackY  = (g.clientH - g.trackH) / 2;
        g.trackL  = g.thumbR + 4;
        g.trackR  = g.clientW - g.thumbR - 4;
        return g;
    }

    double ratio() const {
        const int range = max_ - min_;
        return (range > 0)
            ? static_cast<double>(value_ - min_) / range
            : 0.0;
    }

    int valueFromX(int x) const {
        const TrackGeom g = geom();
        const int trackLen = g.trackR - g.trackL;
        if (trackLen <= 0) return min_;
        double r = static_cast<double>(x - g.trackL) / trackLen;
        r = std::clamp(r, 0.0, 1.0);
        return min_ + static_cast<int>(r * (max_ - min_) + 0.5);
    }

    void fireEvent() {
        wxCommandEvent evt(wxEVT_SLIDER, GetId());
        evt.SetInt(value_);
        evt.SetEventObject(this);
        ProcessWindowEvent(evt);
    }

    // ---- painting ----
    void onPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        const wxSize sz = GetClientSize();
        const TrackGeom g = geom();
        const double r = ratio();

        // 背景 #F5F6F7
        dc.SetBackground(wxBrush(wxColour(245, 246, 247)));
        dc.Clear();

        // groove #D1D1D1
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(wxColour(209, 209, 209)));
        dc.DrawRectangle(g.trackL, g.trackY, g.trackR - g.trackL, g.trackH);

        // fill #FFF9C4
        const int fillW = static_cast<int>(r * (g.trackR - g.trackL));
        if (fillW > 0) {
            dc.SetBrush(wxBrush(wxColour(255, 249, 196)));
            dc.DrawRectangle(g.trackL, g.trackY, fillW, g.trackH);
        }

        // thumb #0078D4
        const int thumbX = g.trackL + fillW;
        const int thumbY = g.clientH / 2;
        dc.SetPen(wxPen(wxColour(0, 80, 160)));
        dc.SetBrush(wxBrush(wxColour(0, 120, 212)));
        dc.DrawCircle(thumbX, thumbY, g.thumbR);
    }

    // ---- mouse ----
    void onLeftDown(wxMouseEvent& e) {
        CaptureMouse();
        dragging_ = true;
        int newVal = valueFromX(e.GetX());
        if (newVal != value_) {
            value_ = newVal;
            Refresh();
            fireEvent();
        }
    }

    void onMotion(wxMouseEvent& e) {
        if (!dragging_ || !e.Dragging() || !e.LeftIsDown()) return;
        int newVal = valueFromX(e.GetX());
        if (newVal != value_) {
            value_ = newVal;
            Refresh();
            fireEvent();
        }
    }

    void onLeftUp(wxMouseEvent&) {
        if (dragging_) {
            dragging_ = false;
            if (HasCapture()) ReleaseMouse();
        }
    }

    void onCaptureLost(wxMouseCaptureLostEvent&) {
        dragging_ = false;
    }
};

StandaloneFloatingWidget::StandaloneFloatingWidget(const config::AppConfig& cfg,
                                                   AppController* controller,
                                                   wxWindow* parent)
    : wxFrame(nullptr, wxID_ANY, L"",
              wxDefaultPosition, wxDefaultSize,
              wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP | wxBORDER_NONE | wxFRAME_SHAPED),
      cfg_(cfg),
      controller_(controller),
      locateTarget_(parent),
      parentWindow_(parent),
      timer_(this),
      hideTimer_(this),
      hoverTimer_(this),
      mode_(Mode::Orb),
      radius_(FloatingWidgetPolicy::CircleDefaults::kDefaultRadius),
      hideDelayMs_(FloatingWidgetPolicy::HideDelayDefaults::kDefaultMs),
      hoverExpandDelayMs_(FloatingWidgetPolicy::HoverExpandDefaults::kDefaultMs),
      dockEdge_(FloatingWidgetPolicy::DockEdge::Right) {
    SetName(L"StandaloneFloatingWidget");
    // Stable window title so the floating widget is discoverable by UI Automation
    // / accessibility tools and automated GUI tests (it has no caption, so this
    // text never renders as a title bar). Matches uitest ids::FloatingWidgetName.
    SetTitle(L"StandaloneFloatingWidget");

    // 透明窗口：自行绘制全部背景（避免系统灰色擦除），并以分层窗口 + 品红色键
    // 实现「未绘制/透明区域透出桌面」，从而彻底去除灰色填充。
    SetBackgroundStyle(wxBG_STYLE_PAINT);
#ifdef __WXMSW__
    HWND hwnd = reinterpret_cast<HWND>(GetHandle());
    if (hwnd != nullptr) {
        SetWindowLongPtr(hwnd, GWL_EXSTYLE,
                         GetWindowLongPtr(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
    }
#endif

    loadBackground();

    list_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxLC_REPORT | wxLC_SINGLE_SEL);
    list_->InsertColumn(COL_TYPE, L"类型", wxLIST_FORMAT_LEFT, 60);
    list_->InsertColumn(COL_INDEX_ID, L"标识", wxLIST_FORMAT_LEFT, 200);
    list_->InsertColumn(COL_HOST, L"Host", wxLIST_FORMAT_LEFT, 130);
    list_->InsertColumn(COL_SOCKS_PORT, L"监听端口", wxLIST_FORMAT_RIGHT, 80);
    list_->InsertColumn(COL_STATE, L"状态", wxLIST_FORMAT_LEFT, 90);
    list_->InsertColumn(COL_DELAY_MS, L"延迟(ms)", wxLIST_FORMAT_RIGHT, 80);
    list_->InsertColumn(COL_FAIL_STREAK, L"失败次数", wxLIST_FORMAT_RIGHT, 60);
    list_->InsertColumn(COL_PID, L"PID", wxLIST_FORMAT_RIGHT, 80);
    // Win10 Fluent 配色：白色内容区 + 深色文字。
    list_->SetBackgroundColour(wxColour(255, 255, 255));       // #FFFFFF
    list_->SetTextColour(wxColour(50, 49, 48));                // #323130
    list_->Hide();

    slider_ = new CustomColorSlider(this, wxID_ANY,
                                    FloatingWidgetPolicy::hideDelayToSlider(hideDelayMs_),
                                    0, 1000, wxDefaultPosition, wxSize(-1, 22));
    slider_->SetToolTip(L"鼠标离开后自动收起延迟");
    slider_->Hide();

    // ---- v1.4 代理池统一监控：状态文本 + 池控制按钮 + 评估开关 ----
    poolStatusText_ = new wxStaticText(this, wxID_ANY, L"代理池状态: 未运行");
    poolStatusText_->SetForegroundColour(wxColour(50, 49, 48));  // #323130
    poolStatusText_->SetBackgroundColour(wxColour(245, 246, 247)); // #F5F6F7，与 Panel 背景一致，去除深灰色填充

    startStopBtn_ = new wxButton(this, wxID_ANY, L"启动池");
    addBtn_ = new wxButton(this, wxID_ANY, L"添加代理");
    refreshBtn_ = new wxButton(this, wxID_ANY, L"刷新");

    reportChk_ = new wxCheckBox(this, wxID_ANY, L"上报健康");
    pruneChk_ = new wxCheckBox(this, wxID_ANY, L"自动剔除死亡");
    optimizeChk_ = new wxCheckBox(this, wxID_ANY, L"自动优化");
    reportChk_->SetBackgroundColour(wxColour(245, 246, 247));
    pruneChk_->SetBackgroundColour(wxColour(245, 246, 247));
    optimizeChk_->SetBackgroundColour(wxColour(245, 246, 247));
    reportChk_->SetValue(cfg_.standalone_pool.evaluate.reportHealth);
    pruneChk_->SetValue(cfg_.standalone_pool.evaluate.autoPruneDead);
    optimizeChk_->SetValue(cfg_.standalone_pool.evaluate.autoOptimize);

    wxBoxSizer* poolBtnRow = new wxBoxSizer(wxHORIZONTAL);
    poolBtnRow->Add(startStopBtn_, 0, wxRIGHT, 6);
    poolBtnRow->Add(addBtn_, 0, wxRIGHT, 6);
    poolBtnRow->Add(refreshBtn_, 0, 0, 0);

    wxBoxSizer* poolChkRow = new wxBoxSizer(wxHORIZONTAL);
    poolChkRow->Add(reportChk_, 0, wxRIGHT, 12);
    poolChkRow->Add(pruneChk_, 0, wxRIGHT, 12);
    poolChkRow->Add(optimizeChk_, 0, 0, 0);

    wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
    panelSizer->Add(list_, 1, wxEXPAND | wxALL, 6);
    panelSizer->Add(poolStatusText_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);
    panelSizer->Add(poolBtnRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);
    panelSizer->Add(poolChkRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);
    panelSizer->Add(slider_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    SetSizer(panelSizer);

    Bind(wxEVT_TIMER, &StandaloneFloatingWidget::onTimer, this);
    // 拦截外部 WM_CLOSE 广播（测试框架结束任务/系统关机广播会向所有顶层
    // 窗口发 WM_CLOSE）：本窗生命周期完全归属 MainFrame（~MainFrame 统一
    // raw delete），自身绝不自毁，否则 Destroy() 进入 wxPendingDelete 延迟
    // 删除链后会与 ~MainFrame 的 delete 形成双删除（UAF 崩溃，见
    // docs/bugfix/2026-09-11-Bugfix-FloatingWidget-CloseDoubleDelete-v1.0.md）。
    Bind(wxEVT_CLOSE_WINDOW, &StandaloneFloatingWidget::onClose, this);
    Bind(wxEVT_PAINT, &StandaloneFloatingWidget::onPaint, this);
    Bind(wxEVT_LEFT_DOWN, &StandaloneFloatingWidget::onLeftDown, this);
    Bind(wxEVT_LEFT_UP, &StandaloneFloatingWidget::onLeftUp, this);
    Bind(wxEVT_LEFT_DCLICK, &StandaloneFloatingWidget::onLeftDClick, this);
    Bind(wxEVT_MOTION, &StandaloneFloatingWidget::onMouseMove, this);
    Bind(wxEVT_ENTER_WINDOW, &StandaloneFloatingWidget::onEnterWindow, this);
    Bind(wxEVT_LEAVE_WINDOW, &StandaloneFloatingWidget::onLeaveWindow, this);
    Bind(wxEVT_ERASE_BACKGROUND, &StandaloneFloatingWidget::onEraseBackground, this);
    Bind(wxEVT_CONTEXT_MENU, &StandaloneFloatingWidget::onContextMenu, this);
    Bind(wxEVT_TEST_ONLINE_PROXIES, &StandaloneFloatingWidget::onTestOnlineProxiesEvent, this);
    Bind(wxEVT_ACTIVATE, &StandaloneFloatingWidget::onActivate, this);
    slider_->Bind(wxEVT_SLIDER, &StandaloneFloatingWidget::onSlider, this);
    startStopBtn_->Bind(wxEVT_BUTTON, &StandaloneFloatingWidget::onStartStopPool, this);
    addBtn_->Bind(wxEVT_BUTTON, &StandaloneFloatingWidget::onAddPoolMember, this);
    refreshBtn_->Bind(wxEVT_BUTTON, &StandaloneFloatingWidget::onRefreshPool, this);
    reportChk_->Bind(wxEVT_CHECKBOX, &StandaloneFloatingWidget::onToggleReport, this);
    pruneChk_->Bind(wxEVT_CHECKBOX, &StandaloneFloatingWidget::onTogglePrune, this);
    optimizeChk_->Bind(wxEVT_CHECKBOX, &StandaloneFloatingWidget::onToggleOptimize, this);
    // 池成员更新事件：MainFrame 不再转发，悬浮窗自消费（timer 兜底全量刷新）。
    Bind(wxEVT_POOL_MEMBERS_UPDATED, &StandaloneFloatingWidget::onPoolMembersUpdated, this);
    list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, &StandaloneFloatingWidget::onItemActivated, this);
    // Single-click on a row also locates the proxy, reusing the same
    // LocateProxyEvent -> MainFrame chain as the test-result dialog.
    list_->Bind(wxEVT_LIST_ITEM_SELECTED, &StandaloneFloatingWidget::onItemSelected, this);
    // 列表内任意区域（含行间/行下方空白）双击 → 切换主界面。wxListCtrl 为
    // 子窗口，其边界内（行与空白区）的双击都先到达 list_，故在此直接绑定。
    list_->Bind(wxEVT_LEFT_DCLICK, &StandaloneFloatingWidget::onListLeftDClick, this);

    setMode(Mode::Orb, true);
    positionForCurrentEdge();
}

bool StandaloneFloatingWidget::Show(bool show) {
    const bool ret = wxFrame::Show(show);
    if (show) {
        setMode(Mode::Orb, true);
        refreshRows();
        if (!placed_) {
            positionForCurrentEdge();
            placed_ = true;  // 仅首次显示定位；之后保留用户自由落点（拖动/关闭弹窗后不再回到起始位置）
        }
        if (!timer_.IsRunning()) {
            timer_.Start(
                FloatingWidgetPolicy::clampIntervalMs(
                    cfg_.proxy_process_monitor.checkIntervalMs),
                wxTIMER_CONTINUOUS);
        }
    } else {
        timer_.Stop();
        hideTimer_.Stop();
        hoverTimer_.Stop();
        // 若拖动中途隐藏，确保释放鼠标捕获，避免捕获残留。
        if (GetCapture() == this) {
            ReleaseMouse();
        }
    }
    return ret;
}

void StandaloneFloatingWidget::applySettings(const config::AppConfig& newCfg) {
    cfg_ = newCfg;

    // 代理池控件状态：独立于监控开关，依 standalone_pool.enabled 灰化。
    const bool poolEnabled = cfg_.standalone_pool.enabled;
    if (startStopBtn_) { startStopBtn_->Enable(poolEnabled); }
    if (addBtn_) { addBtn_->Enable(poolEnabled); }
    if (refreshBtn_) { refreshBtn_->Enable(poolEnabled); }
    if (reportChk_) { reportChk_->Enable(poolEnabled); }
    if (pruneChk_) { pruneChk_->Enable(poolEnabled); }
    if (optimizeChk_) { optimizeChk_->Enable(poolEnabled); }
    // 回填 evaluate 三开关（配置热应用后保持 UI 与配置一致）。
    if (reportChk_) { reportChk_->SetValue(cfg_.standalone_pool.evaluate.reportHealth); }
    if (pruneChk_) { pruneChk_->SetValue(cfg_.standalone_pool.evaluate.autoPruneDead); }
    if (optimizeChk_) { optimizeChk_->SetValue(cfg_.standalone_pool.evaluate.autoOptimize); }

    if (!cfg_.proxy_process_monitor.enabled) {
        active_ = false;
        Show(false);
        return;
    }
    // Restart the timer with the (possibly) new interval if it was running.
    if (timer_.IsRunning()) {
        timer_.Stop();
        if (active_) {
            timer_.Start(
                FloatingWidgetPolicy::clampIntervalMs(
                    cfg_.proxy_process_monitor.checkIntervalMs),
                wxTIMER_CONTINUOUS);
        }
    }
}

void StandaloneFloatingWidget::toggleActive() {
    active_ = !active_;
    if (active_ && cfg_.proxy_process_monitor.enabled) {
        Show(true);
    } else {
        Show(false);
    }
}

void StandaloneFloatingWidget::setActive(bool on) {
    active_ = on;
    Show(active_ && cfg_.proxy_process_monitor.enabled);
}

void StandaloneFloatingWidget::onClose(wxCloseEvent&) {
    // 仅隐藏，保留对象存活；整体退出由主窗口 / ~MainFrame 掌控。
    // 不调用 Destroy()/event.Skip()：避免进入 wxPendingDelete 延迟删除链
    // 与 ~MainFrame 的 raw delete 形成双删除。
    Show(false);
}

void StandaloneFloatingWidget::onTimer(wxTimerEvent& event) {
    if (&event.GetTimer() == &timer_) {
        refreshRows();
    } else if (&event.GetTimer() == &hideTimer_) {
        if (dragging_ || pointerInside()) {
            hovering_ = pointerInside();
            return;
        }
        hovering_ = false;
        // bugfix 2026-09-14: 收回悬浮球时保留当前位置（setMode→applyShape 内部
        // keepCenter 已保位）；不再 positionForCurrentEdge() 重新停靠到屏幕边缘
        // 中央，否则展开后自动收回会丢弃用户的自由落点。
        setMode(Mode::Orb);
    } else if (&event.GetTimer() == &hoverTimer_) {
        // 悬停 dwell 结束：若仍在悬浮球上且未拖动，展开面板。
        if (mode_ == Mode::Orb && hovering_ && !dragging_) {
            setMode(Mode::Panel);
        }
    }
}

void StandaloneFloatingWidget::onEnterWindow(wxMouseEvent& event) {
    if (dragging_) {
        event.Skip();
        return;
    }
    hovering_ = true;
    hideTimer_.Stop();
    if (active_ && cfg_.proxy_process_monitor.enabled && mode_ == Mode::Orb) {
        // 悬停一段时间后（hoverExpandDelayMs_）再展开面板，避免一碰就弹，
        // 方便先抓取并拖动悬浮球。
        hoverTimer_.Start(hoverExpandDelayMs_, wxTIMER_ONE_SHOT);
    }
    event.Skip();
}

void StandaloneFloatingWidget::onLeaveWindow(wxMouseEvent& event) {
    if (dragging_) {
        event.Skip();
        return;
    }
    hovering_ = false;
    if (mode_ == Mode::Panel) {
        hideTimer_.Start(hideDelayMs_, wxTIMER_ONE_SHOT);
    } else if (mode_ == Mode::Orb) {
        // 离开时取消待展开，保持悬浮球稳定，方便抓取拖动。
        hoverTimer_.Stop();
    }
    event.Skip();
}

void StandaloneFloatingWidget::onLeftDown(wxMouseEvent& event) {
    if (mode_ == Mode::Orb) {
        hoverTimer_.Stop();  // 拖动时取消待展开
        dragging_ = true;
        const wxPoint screen = wxGetMousePosition();
        dragOffset_ = screen - GetScreenPosition();
        dragStartPos_ = screen;
        CaptureMouse();
    } else if (mode_ == Mode::Panel) {
        // Panel 模式也捕获鼠标，以便检测单击关闭。
        dragging_ = true;
        const wxPoint screen = wxGetMousePosition();
        dragStartPos_ = screen;
        CaptureMouse();
    }
    event.Skip();
}

void StandaloneFloatingWidget::onMouseMove(wxMouseEvent& event) {
    if (dragging_) {
        const wxPoint screen = wxGetMousePosition();
        Move(screen - dragOffset_);
    }
    event.Skip();
}

void StandaloneFloatingWidget::onLeftUp(wxMouseEvent& event) {
    if (dragging_) {
        dragging_ = false;

        // 区分点击 vs 拖拽：移动距离 < 5px 视为单击。
        const wxPoint screen = wxGetMousePosition();
        const int dx = screen.x - dragStartPos_.x;
        const int dy = screen.y - dragStartPos_.y;
        const bool isClick = (std::abs(dx) < 5 && std::abs(dy) < 5);

        // 单击（悬浮球状态下）：立即展开为面板。
        // 双击切换主界面最大化/还原仅由 Panel 模式的 wxEVT_LEFT_DCLICK 触发
        //（onLeftDClick），悬浮球(Orb)不捕获双击。
        if (mode_ == Mode::Orb && isClick) {
            if (GetCapture() == this) {
                ReleaseMouse();
            }
            if (active_ && cfg_.proxy_process_monitor.enabled) {
                setMode(Mode::Panel);
            }
            event.Skip();
            return;
        }

        // 真正的拖拽结束（或 Panel 单击）：释放捕获并自由停靠。
        if (GetCapture() == this) {
            ReleaseMouse();
        }

        // 真正的拖拽结束：自由停靠，钳制在屏幕内。
        wxRect disp = wxGetClientDisplayRect();
        int x = GetScreenPosition().x - disp.x;
        int y = GetScreenPosition().y - disp.y;
        const int w = GetSize().x;
        const int h = GetSize().y;
        FloatingWidgetPolicy::ScreenAnchor anchor;
        anchor.screenW = disp.width;
        anchor.screenH = disp.height;
        anchor.margin = FromDIP(8);
        FloatingWidgetPolicy::clampToScreen(x, y, w, h, anchor);
        Move(disp.x + x, disp.y + y);
        // 记录参考边缘（仅用于下次 Show 的兜底定位，不影响当前自由位置）。
        const wxPoint center = GetScreenPosition() + GetSize() / 2;
        dockEdge_ = FloatingWidgetPolicy::nearestEdge(
            center.x - disp.x, center.y - disp.y, disp.width, disp.height);
    }
    event.Skip();
}

// 悬浮窗双击（或 Panel 模式下 OS 派发的 wxEVT_LEFT_DCLICK）→ 切换主界面
// 最大化 ⇄ 最小化。用 lastToggleTime_ 防抖：同一瞬间两条路径不会重复触发。
// 语义（v1.6 用户需求）：窗口化/最小化 → 最大化；最大化 → 最小化。
void StandaloneFloatingWidget::toggleMainFrameMaximize() {
    const wxLongLong now = wxGetLocalTimeMillis();
    if (now - lastToggleTime_ < 200) {
        // 当前按下既被手动检测(WM_LBUTTONDOWN)又派发 DBLCLK(Panel) 时只切一次。
        lastToggleTime_ = now;
        return;
    }
    lastToggleTime_ = now;

    wxFrame* frame = wxDynamicCast(parentWindow_, wxFrame);
    if (frame == nullptr) {
        return;
    }
    if (frame->IsMaximized()) {
        frame->Iconize(true);  // 最大化 → 最小化
    } else {
        // 窗口化 / 最小化 / 托盘隐藏 → 最大化。wxMSW 下对已最小化窗口直接
        // Maximize(true) 是空操作（ShowWindow(SW_MAXIMIZE) 被忽略），必须先
        // Restore 取消最小化；对已 Hide 到托盘（最小化后 onIconize 隐藏）的
        // 窗口还需先 Show+Raise 才能可见并最大化（与托盘双击恢复语义一致）。
        if (!frame->IsShown()) {
            frame->Show(true);
            frame->Raise();
        }
        if (frame->IsIconized()) {
            frame->Restore();
        }
        frame->Maximize(true);
    }
}

// 双击悬浮窗背景切换主界面 最大化 ⇄ 最小化。
// parentWindow_ 即主窗口(MainFrame, 继承自 wxFrame)。本回调仅接收落在悬浮窗
// 自身背景（非子列表）的双击；子列表 wxListCtrl 内的双击（行与空白区）由
// onListLeftDClick 接管。二者共用 lastToggleTime_ 防抖。
void StandaloneFloatingWidget::onLeftDClick(wxMouseEvent& WXUNUSED(event)) {
    // Panel 模式双击不会出现 Orb 展开的几何扰动，Windows 仍派发
    // WM_LBUTTONDBLCLK → wxEVT_LEFT_DCLICK，直接走受防抖保护的切换路径。
    toggleMainFrameMaximize();
}

// 双击列表内任意区域（含行/空白）切换主界面 最大化 ⇄ 最小化。
// wxListCtrl 为子窗口：其边界内（数据行及行下方/行间空白区）的任何双击都
// 先到达 list_，故在此捕获「任意区域双击」；行双击同时仍触发 onItemActivated。
// 单击定位保持由 onItemSelected 负责。二者共用 lastToggleTime_ 防抖，
// 行双击时本回调与 onItemActivated 均触发也只会切换一次。
void StandaloneFloatingWidget::onListLeftDClick(wxMouseEvent& event) {
    toggleMainFrameMaximize();
    event.Skip();  // 交由原生 wxListCtrl 继续处理（行选择/激活）
}

void StandaloneFloatingWidget::onSlider(wxCommandEvent& event) {
    hideDelayMs_ = FloatingWidgetPolicy::sliderToHideDelay(slider_->GetValue());
    event.Skip();
}

void StandaloneFloatingWidget::onContextMenu(wxContextMenuEvent& WXUNUSED(event)) {
    wxMenu menu;
    contextMenuSel_ = -1;
    if (mode_ == Mode::Panel) {
        long sel = list_->GetNextItem(-1, wxLIST_STATE_SELECTED);
        if (sel < 0) {
            // 右键时若未选中任何行，尝试根据鼠标位置选中点击的行。
            const wxPoint screen = wxGetMousePosition();
            const wxPoint client = list_->ScreenToClient(screen);
            int flags = 0;
            sel = list_->HitTest(wxPoint(client.x, client.y), flags);
            if (sel >= 0 && sel < static_cast<long>(rows_.size())) {
                list_->SetItemState(sel, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
            }
        }
        contextMenuSel_ = sel;
        if (sel >= 0 && sel < static_cast<long>(rows_.size())) {
            // 统一右键（bugfix #82）：池行与独立行菜单一致——「关闭代理」+
            // 「定位到代理列表」；池行的「关闭代理」在 onMenuCloseProxy 内
            // 按行类型分支为从池中优雅移除成员。
            menu.Append(ID_MENU_CLOSE_PROXY, L"关闭代理");
            menu.Bind(wxEVT_MENU, &StandaloneFloatingWidget::onMenuCloseProxy, this,
                      ID_MENU_CLOSE_PROXY);
            // 任意选中行：定位到代理列表。
            menu.Append(ID_MENU_LOCATE_PROXY, L"定位到代理列表");
            menu.Bind(wxEVT_MENU, &StandaloneFloatingWidget::onMenuLocateProxy, this,
                      ID_MENU_LOCATE_PROXY);
        }
    }
    menu.Append(ID_MENU_TEST_ONLINE, L"测试在线代理");
    menu.Bind(wxEVT_MENU, &StandaloneFloatingWidget::onMenuTestOnline, this,
              ID_MENU_TEST_ONLINE);
    menu.Append(wxID_EXIT, L"退出");
    menu.Bind(wxEVT_MENU, &StandaloneFloatingWidget::onMenuExit, this, wxID_EXIT);
    PopupMenu(&menu);
}

void StandaloneFloatingWidget::onMenuExit(wxCommandEvent&) {
    wxExit();
}

void StandaloneFloatingWidget::onMenuCloseProxy(wxCommandEvent&) {
    const long sel = contextMenuSel_;
    if (sel < 0 || sel >= static_cast<long>(rows_.size()) || !controller_) {
        return;
    }
    const std::string indexId = rows_[static_cast<std::size_t>(sel)].indexId;
    const int64_t pid = rows_[static_cast<std::size_t>(sel)].pid;

    // 统一菜单（bugfix #82）：池行「关闭代理」= 从池中优雅移除成员
    // （graceful=true 交由 evaluator 两阶段移除 outbound）；独立行走
    // 原有 stopStandaloneProxy + pid 兜底终止逻辑。
    if (rows_[static_cast<std::size_t>(sel)].type == MonitorType::Pool) {
        if (indexId.empty()) {
            return;
        }
        controller_->removePoolMember(std::stoll(indexId), true);
        refreshRows();
        return;
    }

    if (indexId.empty() && pid < 0) {
        return;
    }

    bool ok = false;
    if (!indexId.empty()) {
        ok = controller_->stopStandaloneProxy(indexId);
    }

    // 若按 indexId 未终止成功，且存在有效 pid，则直接按 pid 终止进程。
    if (!ok && pid > 0) {
        const DWORD dwPid = static_cast<DWORD>(pid);
        const HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, dwPid);
        if (hProcess) {
            if (TerminateProcess(hProcess, 1)) {
                WaitForSingleObject(hProcess, 3000);
                ok = true;
            } else {
                const DWORD err = GetLastError();
                Logger::write("[StandaloneProxy] TerminateProcess by PID FAILED (err="
                              + std::to_string(err) + ") for pid=" + std::to_string(dwPid),
                              LogLevel::ERR);
            }
            CloseHandle(hProcess);
        } else {
            const DWORD err = GetLastError();
            Logger::write("[StandaloneProxy] OpenProcess FAILED (err="
                          + std::to_string(err) + ") for pid=" + std::to_string(dwPid),
                          LogLevel::ERR);
        }
    }

    if (!ok) {
        wxMessageBox(L"关闭代理失败：未找到运行中的代理进程。",
                     L"提示", wxOK | wxICON_WARNING, this);
    }
    refreshRows();
}

// 代理池统一监控（v1.4）：启动/停止池按钮。
void StandaloneFloatingWidget::onStartStopPool(wxCommandEvent&) {
    if (!controller_) {
        return;
    }
    if (controller_->isProxyPoolRunning()) {
        controller_->stopProxyPool();
    } else {
        if (!controller_->startProxyPool()) {
            wxMessageBox(L"代理池启动失败", L"提示",
                         wxOK | wxICON_WARNING, this);
        }
    }
    refreshRows();
}

// 代理池统一监控（v1.4）：添加代理按钮。池未运行时先启动池，
// 再弹出 AddPoolMemberDialog 选择候选代理，逐个注入并汇总成功数。
void StandaloneFloatingWidget::onAddPoolMember(wxCommandEvent&) {
    if (!controller_) {
        return;
    }
    if (!controller_->isProxyPoolRunning()) {
        if (!controller_->startProxyPool()) {
            wxMessageBox(L"代理池启动失败，无法添加代理…", L"提示",
                         wxOK | wxICON_WARNING, this);
            return;
        }
    }
    AddPoolMemberDialog dlg(this, controller_);
    if (dlg.ShowModal() != wxID_OK) {
        return;
    }
    const std::vector<std::string>& selected = dlg.getSelectedIndexIds();
    int added = 0;
    for (std::size_t i = 0; i < selected.size(); ++i) {
        if (controller_->injectProxyToPool(selected[i])) {
            ++added;
        }
    }
    if (added > 0) {
        wxMessageBox(wxString::Format(L"已添加 %d 个代理到代理池。", added),
                     L"提示", wxOK | wxICON_INFORMATION, this);
    }
    refreshRows();
}

// 代理池统一监控（v1.4）：刷新按钮。池运行时触发一次健康探测，
// 随后全量刷新列表（timer 也会周期性刷新，此处提供手动触发）。
void StandaloneFloatingWidget::onRefreshPool(wxCommandEvent&) {
    if (!controller_) {
        return;
    }
    if (controller_->isProxyPoolRunning()) {
        controller_->probePoolNow();
    }
    refreshRows();
}

// 代理池统一监控（v1.4）：上报健康 checkbox。
void StandaloneFloatingWidget::onToggleReport(wxCommandEvent& event) {
    if (controller_) {
        controller_->setPoolReportHealth(reportChk_->GetValue());
    }
    event.Skip();
}

// 代理池统一监控（v1.4）：自动剔除死亡 checkbox。
void StandaloneFloatingWidget::onTogglePrune(wxCommandEvent& event) {
    if (controller_) {
        controller_->setPoolAutoPruneDead(pruneChk_->GetValue());
    }
    event.Skip();
}

// 代理池统一监控（v1.4）：自动优化 checkbox。
void StandaloneFloatingWidget::onToggleOptimize(wxCommandEvent& event) {
    if (controller_) {
        controller_->setPoolAutoOptimize(optimizeChk_->GetValue());
    }
    event.Skip();
}

// 右键菜单「定位到代理列表」：任意选中行可用，复用 LocateProxyEvent。
void StandaloneFloatingWidget::onMenuLocateProxy(wxCommandEvent&) {
    if (!controller_ || !locateTarget_) {
        return;
    }
    const long sel = contextMenuSel_;
    if (sel < 0) {
        return;
    }
    const wxString indexId = list_->GetItemText(sel, COL_INDEX_ID);
    if (indexId.empty()) {
        return;
    }
    wxQueueEvent(locateTarget_, new LocateProxyEvent(indexId.ToStdString()));
}

// 池成员更新事件：MainFrame 不再转发，悬浮窗自消费。事件可能先于
// 悬浮窗创建到达（此时未 Bind），timer 周期性全量刷新兜底。
void StandaloneFloatingWidget::onPoolMembersUpdated(PoolMembersUpdatedEvent& event) {
    event.Skip();
    if (!controller_ || !list_ || mode_ != Mode::Panel) {
        return;
    }
    refreshRows();
}

// 更新池状态文本：运行状态 + 成员数 + 在线代理数。
void StandaloneFloatingWidget::updatePoolStatusText() {
    if (!poolStatusText_) {
        return;
    }
    const bool running = controller_ && controller_->isProxyPoolRunning();
    int memberCount = 0;
    int aliveCount = 0;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].type == MonitorType::Pool) {
            ++memberCount;
            if (rows_[i].lastAlive) {
                ++aliveCount;
            }
        }
    }
    poolStatusText_->SetLabel(wxString::Format(
        L"代理池状态: %s 成员数: %d 在线代理: %d",
        running ? L"运行中" : L"未运行", memberCount, aliveCount));
}

// 右键菜单「测试在线代理」：复用 AppController::testOnlineProxiesAsync 的
// 多线程在线代理测试，结果事件投递回本悬浮窗，由 onTestOnlineProxiesEvent
// 展示 TestOnlineResultDialog（父窗口为 MainFrame，保证定位事件可达）。
void StandaloneFloatingWidget::onMenuTestOnline(wxCommandEvent&) {
    if (!controller_) {
        return;
    }
    if (controller_->isRunning()) {
        wxMessageBox(L"操作进行中，请等待完成后再试", L"操作进行中",
                     wxOK | wxICON_WARNING, this);
        return;
    }
    controller_->testOnlineProxiesAsync(this);
}

void StandaloneFloatingWidget::onTestOnlineProxiesEvent(TestOnlineProxiesEvent& event) {
    std::vector<std::string> failedIndexIds = event.takeFailedIndexIds();
    const int total = event.getTotal();
    const int success = event.getSuccess();
    const int failed = event.getFailed();

    wxString msg;
    if (total <= 0) {
        msg = L"当前没有正在运行的独立代理进程。";
        wxMessageBox(msg, L"测试在线代理", wxOK | wxICON_INFORMATION, this);
        return;
    }

    msg = wxString::Format(L"在线代理测试完成：共 %d，成功 %d，失败 %d。", total, success, failed);
    if (failed > 0) {
        // 失败代理在可滚动对话框中展示；单击失败行定位到订阅 + 代理列表面板。
        TestOnlineResultDialog dlg(parentWindow_, failedIndexIds, total, success, failed);
        dlg.ShowModal();
    } else {
        wxMessageBox(msg, L"测试在线代理", wxOK | wxICON_INFORMATION, this);
    }
}

void StandaloneFloatingWidget::onActivate(wxActivateEvent& event) {
    if (!event.GetActive() && mode_ == Mode::Panel && !dragging_) {
        // 窗口失去激活状态（用户点击了外部），关闭面板回到 Orb。
        // bugfix 2026-09-14: 保留当前位置收回（keepCenter），不重新停靠边缘
        // 中央，与 hideTimer 自动收回路径行为一致。
        setMode(Mode::Orb);
    }
    event.Skip();
}

// 双击列表行：切换主界面 最大化 ⇄ 还原。
// 单击定位由 onItemSelected(wxEVT_LIST_ITEM_SELECTED) 负责；双击在此切换主界面，
// 与 Panel 背景双击(onLeftDClick)语义一致，共用 lastToggleTime_ 防抖，避免双重触发。
void StandaloneFloatingWidget::onItemActivated(wxListEvent& WXUNUSED(event)) {
    toggleMainFrameMaximize();
}

// Single-click locate: reuses the same LocateProxyEvent -> MainFrame chain as
// the test-result dialog. On select, post the owning indexId so MainFrame's
// wxEVT_LOCATE_PROXY handler locates the proxy + owning subscription.
void StandaloneFloatingWidget::onItemSelected(wxListEvent& event) {
    const long row = event.GetIndex();
    if (row < 0 || !controller_ || !locateTarget_) {
        return;
    }
    const wxString indexId = list_->GetItemText(row, COL_INDEX_ID);
    if (indexId.IsEmpty()) {
        return;
    }
    wxQueueEvent(locateTarget_, new LocateProxyEvent(indexId.ToStdString()));
}

void StandaloneFloatingWidget::refreshRows() {
    if (!controller_ || !list_) {
        return;
    }
    list_->DeleteAllItems();

    rows_ = controller_->getUnifiedMonitorRows();
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const long index = list_->InsertItem(
            static_cast<long>(i), wxString(L""));

        // 类型列：独立 / 代理池
        list_->SetItem(index, COL_TYPE,
                      rows_[i].type == MonitorType::Pool
                          ? wxString(L"代理池")
                          : wxString(L"独立"));

        // 标识列：统一显示真实 indexId（bugfix #82：池行旧实现显示 px-tag 内部
        // 标识，且 tag 由 int 截断值拼接；现显示与独立行一致的完整 indexId）
        list_->SetItem(index, COL_INDEX_ID,
                      rows_[i].indexId.empty()
                          ? wxString(L"-")
                          : wxString(rows_[i].indexId));

        // Host 列：profile address
        list_->SetItem(index, COL_HOST,
                      rows_[i].host.empty()
                          ? wxString(L"-")
                          : wxString(rows_[i].host));

        // 监听端口列
        list_->SetItem(index, COL_SOCKS_PORT,
                      rows_[i].socksPort > 0
                          ? wxString(std::to_string(rows_[i].socksPort))
                          : wxString(L"-"));

        // 状态列：独立行原样；池行映射为可读文本
        wxString stateText;
        if (rows_[i].type == MonitorType::Pool) {
            if (rows_[i].state == "active") {
                stateText = L"运行中";
            } else if (rows_[i].state == "remove-requested") {
                stateText = L"待移除";
            } else if (rows_[i].state == "draining") {
                stateText = L"排空中";
            } else {
                stateText = wxString(rows_[i].state);
            }
        } else {
            stateText = rows_[i].state.empty()
                            ? wxString(L"-")
                            : wxString(rows_[i].state);
        }
        list_->SetItem(index, COL_STATE, stateText);

        // 延迟列：仅池行有值
        list_->SetItem(index, COL_DELAY_MS,
                      rows_[i].lastDelayMs > 0
                          ? wxString(std::to_string(rows_[i].lastDelayMs))
                          : wxString(L"-"));

        // 失败次数列：独立行 "-"；池行显示连续失败次数
        list_->SetItem(index, COL_FAIL_STREAK,
                      rows_[i].type == MonitorType::Pool
                          ? wxString(std::to_string(rows_[i].failStreak))
                          : wxString(L"-"));

        // PID 列：仅独立行有值
        list_->SetItem(index, COL_PID,
                      rows_[i].pid >= 0
                          ? wxString(std::to_string(rows_[i].pid))
                          : wxString(L"-"));
    }

    updatePoolStatusText();

    // 同步启动/停止池按钮文字
    if (startStopBtn_ && controller_) {
        const bool running = controller_->isProxyPoolRunning();
        startStopBtn_->SetLabel(running ? L"停止池" : L"启动池");
    }

    if (mode_ == Mode::Orb) {
        Refresh();
    }
}

void StandaloneFloatingWidget::setMode(Mode m, bool force) {
    if (!force && m == mode_) {
        return;
    }
    const wxPoint oldCenter = GetScreenPosition() + GetSize() / 2;
    // bugfix 2026-09-14 (#85): Orb→Panel 展开时保存球中心快照；Panel→Orb 收回时
    // 用快照替代当前（可能被 clampToScreen 钳制后）面板中心，避免边缘球展开
    // 收回后漂移至屏幕中部而非原始位置。
    if (mode_ == Mode::Orb && m == Mode::Panel) {
        savedOrbCenter_ = oldCenter;
    }
    mode_ = m;
    wxPoint keep = oldCenter;
    if (mode_ == Mode::Orb && savedOrbCenter_) {
        keep = *savedOrbCenter_;
    }
    applyShape(keep);
    if (mode_ == Mode::Orb) {
        savedOrbCenter_.reset();
    }
}

void StandaloneFloatingWidget::applyShape(const wxPoint& keepCenter) {
    int w = 0;
    int h = 0;
#ifdef __WXMSW__
    HWND hwnd = reinterpret_cast<HWND>(GetHandle());
#endif

    if (mode_ == Mode::Orb) {
        w = h = FromDIP(radius_ * 2);
        SetSize(w, h);
        // Orb 模式：隐藏全部子控件（列表、滑杆 + 代理池控制区），
        // 避免 0 尺寸 HWND 残留在 UIA 树中（UIA 会裁剪宽度/高度为 0 的元素，
        // 造成自动化测试在 Orb 模式下误以为 "启动池" 已可达）。
        list_->Hide();
        slider_->Hide();
        if (startStopBtn_) startStopBtn_->Hide();
        if (addBtn_) addBtn_->Hide();
        if (refreshBtn_) refreshBtn_->Hide();
        if (reportChk_) reportChk_->Hide();
        if (pruneChk_) pruneChk_->Hide();
        if (optimizeChk_) optimizeChk_->Hide();
        // bugfix 2026-09-14 (OrbHitZone): poolStatusText_ 是统一变更新增的第 8 个
        // 池控件，此前唯一遗漏 Hide——Orb 模式其 HWND 仍横亘球上部截获鼠标命中，
        // 造成悬停/单击仅下半球有效的死区。
        if (poolStatusText_) poolStatusText_->Hide();
#ifdef __WXMSW__
        // Orb 模式：开启 WS_EX_LAYERED，由 UpdateLayeredWindow 逐像素 alpha 渲染。
        SetWindowLongPtr(hwnd, GWL_EXSTYLE,
                         GetWindowLongPtr(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
#endif
    } else {
        // Panel 模式：统一监控面板（独立代理 + 代理池），600×480 容纳
        // 顶部池状态/按钮/checkbox 区与 8 列成员表。
        w = FromDIP(600);
        h = FromDIP(480);
        SetSize(w, h);
        list_->Show();
        slider_->Show();
        if (startStopBtn_) startStopBtn_->Show();
        if (addBtn_) addBtn_->Show();
        if (refreshBtn_) refreshBtn_->Show();
        if (reportChk_) reportChk_->Show();
        if (pruneChk_) pruneChk_->Show();
        if (optimizeChk_) optimizeChk_->Show();
        // bugfix 2026-09-14 (OrbHitZone): 与 Orb 分支对称，池状态文本恢复显示。
        if (poolStatusText_) poolStatusText_->Show();
        Layout();
#ifdef __WXMSW__
        // Panel 模式：关闭 WS_EX_LAYERED，恢复 wxWidgets 正常绘制，
        // 使子控件（wxListCtrl / wxSlider）能正常显示。
        SetWindowLongPtr(hwnd, GWL_EXSTYLE,
                         GetWindowLongPtr(hwnd, GWL_EXSTYLE) & ~WS_EX_LAYERED);
#endif
    }

    wxRect disp = wxGetClientDisplayRect();
    int rx = keepCenter.x - disp.x - w / 2;
    int ry = keepCenter.y - disp.y - h / 2;
    FloatingWidgetPolicy::ScreenAnchor anchor;
    anchor.screenW = disp.width;
    anchor.screenH = disp.height;
    anchor.margin = FromDIP(8);
    FloatingWidgetPolicy::clampToScreen(rx, ry, w, h, anchor);
    Move(disp.x + rx, disp.y + ry);
    Refresh();
}

void StandaloneFloatingWidget::positionForCurrentEdge() {
    wxRect disp = wxGetClientDisplayRect();
    FloatingWidgetPolicy::ScreenAnchor anchor;
    anchor.screenW = disp.width;
    anchor.screenH = disp.height;
    anchor.margin = FromDIP(8);
    const int w = GetSize().x;
    const int h = GetSize().y;
    int x = 0;
    int y = 0;
    FloatingWidgetPolicy::dockPosition(dockEdge_, w, h, anchor, x, y);
    Move(disp.x + x, disp.y + y);
}

void StandaloneFloatingWidget::loadBackground() {
    // 直接保存 wxImage，保留 PNG 原始 alpha 通道。
    // 不经过 wxBitmap 中转，避免 Windows 上 alpha 通道丢失。
    if (!ToolbarIcons::loadPngFromResource(L"float_monitor_process", bgImage_)) {
        bgImage_ = wxImage();  // 清空
    }
}

bool StandaloneFloatingWidget::pointerInside() const {
    return GetScreenRect().Contains(wxGetMousePosition());
}

wxRegion StandaloneFloatingWidget::buildCircleRegion(int d) {
    wxBitmap bmp(d, d);
    {
        wxMemoryDC mdc(bmp);
        mdc.SetBackground(wxBrush(wxColour(255, 0, 255)));
        mdc.Clear();
        mdc.SetBrush(*wxWHITE_BRUSH);
        mdc.SetPen(*wxWHITE_PEN);
        mdc.DrawCircle(d / 2, d / 2, d / 2);
    }
    return wxRegion(bmp, wxColour(255, 0, 255));
}

wxRegion StandaloneFloatingWidget::buildRoundedRegion(int w, int h, int corner) {
    wxBitmap bmp(w, h);
    {
        wxMemoryDC mdc(bmp);
        mdc.SetBackground(wxBrush(wxColour(255, 0, 255)));
        mdc.Clear();
        mdc.SetBrush(*wxWHITE_BRUSH);
        mdc.SetPen(*wxWHITE_PEN);
        mdc.DrawRoundedRectangle(0, 0, w, h, static_cast<double>(corner));
    }
    return wxRegion(bmp, wxColour(255, 0, 255));
}

void StandaloneFloatingWidget::onPaint(wxPaintEvent&) {
    const wxSize sz = GetClientSize();
    if (sz.x <= 0 || sz.y <= 0) return;

    if (mode_ == Mode::Orb) {
        // === Orb 模式：渲染到 wxImage → UpdateLayeredWindow ===
        wxImage canvas(sz.x, sz.y);
        canvas.InitAlpha();

        // 直接使用 bgImage_（保留 PNG 原始 alpha），作为画布基础。
        if (bgImage_.IsOk()) {
            canvas = bgImage_.Copy();
            if (canvas.GetWidth() != sz.x || canvas.GetHeight() != sz.y) {
                canvas.Rescale(sz.x, sz.y, wxIMAGE_QUALITY_HIGH);
            }
            if (!canvas.HasAlpha()) {
                canvas.InitAlpha();
                unsigned char* const a = canvas.GetAlpha();
                if (a) {
                    memset(a, 255, static_cast<std::size_t>(sz.x) * sz.y);
                }
            }
        } else {
            canvas.InitAlpha();
        }

        // 绘制数字：先渲染到临时 24-bit 位图，再提取非黑像素混合到画布。
        const int n = static_cast<int>(rows_.size());
        wxFont f = GetFont();
        f.SetPointSize(wxMax(10, FromDIP(radius_) / 2));
        f.SetWeight(wxFONTWEIGHT_BOLD);

        wxBitmap txtBmp(sz.x, sz.y, 24);
        {
            wxMemoryDC mdc(txtBmp);
            mdc.SetBackground(*wxBLACK_BRUSH);
            mdc.Clear();
            mdc.SetFont(f);
            mdc.SetTextForeground(wxColour(0, 200, 0));
            mdc.DrawLabel(wxString::Format(L"%d", n),
                          wxRect(0, 0, sz.x, sz.y),
                          wxALIGN_CENTER_HORIZONTAL | wxALIGN_CENTER_VERTICAL);
            mdc.SelectObject(wxNullBitmap);
        }

        wxImage txtImg = txtBmp.ConvertToImage();
        const unsigned char* const rgb = txtImg.GetData();
        const int stride = sz.x * 3;
        for (int y = 0; y < sz.y; ++y) {
            for (int x = 0; x < sz.x; ++x) {
                const int idx = y * stride + x * 3;
                const unsigned char r = rgb[idx];
                const unsigned char g = rgb[idx + 1];
                const unsigned char b = rgb[idx + 2];
                if (r != 0 || g != 0 || b != 0) {
                    canvas.SetRGB(x, y, r, g, b);
                    canvas.SetAlpha(x, y, 255);
                }
            }
        }

        updateLayeredWindow(canvas);
    } else {
        // === Panel 模式：WS_EX_LAYERED 已关闭，正常 DC 绘制 ===
        wxPaintDC dc(this);
        dc.SetBackground(wxBrush(wxColour(245, 246, 247)));  // #F5F6F7
        dc.Clear();
        // 1px 低对比度边框：#D1D1D1
        const wxSize sz = GetClientSize();
        dc.SetPen(wxPen(wxColour(209, 209, 209)));  // #D1D1D1
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(0, 0, sz.x, sz.y);
        // 子控件（list/slider）由 Windows 自动绘制在 DC 之上。
    }
}

void StandaloneFloatingWidget::onEraseBackground(wxEraseEvent&) {
    // 不擦除背景：由 UpdateLayeredWindow 逐像素 alpha 控制透明，未绘制处自然透明。
}

#ifdef __WXMSW__
void StandaloneFloatingWidget::updateLayeredWindow(wxImage& img) {
    if (!img.IsOk()) return;

    const int w = img.GetWidth();
    const int h = img.GetHeight();
    if (w <= 0 || h <= 0) return;

    if (!img.HasAlpha()) {
        img.InitAlpha();
        unsigned char* const alpha = img.GetAlpha();
        if (alpha) {
            memset(alpha, 255, static_cast<std::size_t>(w) * h);
        }
    }

    unsigned char* const data = img.GetData();
    unsigned char* const alpha = img.GetAlpha();

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -static_cast<LONG>(h);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    unsigned char* pvBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS,
                                    reinterpret_cast<void**>(&pvBits), nullptr, 0);
    if (!hBmp || !pvBits) return;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int srcIdx = (y * w + x) * 3;
            const int dstIdx = (y * w + x) * 4;
            const unsigned char a = alpha[y * w + x];
            pvBits[dstIdx]     = static_cast<unsigned char>((data[srcIdx + 2] * a) / 255);     // B
            pvBits[dstIdx + 1] = static_cast<unsigned char>((data[srcIdx + 1] * a) / 255);     // G
            pvBits[dstIdx + 2] = static_cast<unsigned char>((data[srcIdx] * a) / 255);         // R
            pvBits[dstIdx + 3] = a;                                                             // A
        }
    }

    SIZE sz = {w, h};
    POINT ptSrc = {0, 0};

    BLENDFUNCTION bf = {0};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hOldBmp = static_cast<HBITMAP>(SelectObject(hdcMem, hBmp));

    UpdateLayeredWindow(GetHandle(), hdcScreen, nullptr, &sz, hdcMem, &ptSrc, 0, &bf, ULW_ALPHA);

    SelectObject(hdcMem, hOldBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
    DeleteObject(hBmp);
}
#endif
WXLRESULT StandaloneFloatingWidget::MSWWindowProc(WXUINT message,
                                                  WXWPARAM wParam,
                                                  WXLPARAM lParam) {
    if (message == WM_NCHITTEST) {
        // 让整颗悬浮球均可命中测试，保证可拖动/悬停。
        return HTCLIENT;
    }
    return wxFrame::MSWWindowProc(message, wParam, lParam);
}
