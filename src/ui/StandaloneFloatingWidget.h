#ifndef UI_STANDALONE_FLOATING_WIDGET_H
#define UI_STANDALONE_FLOATING_WIDGET_H

#include <wx/frame.h>
#include <wx/listctrl.h>
#include <wx/timer.h>
// wxSlider replaced by custom-drawn CustomColorSlider (defined in .cpp)
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/region.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/stattext.h>

#include <string>
#include <vector>

#include "ConfigReader.h"   // config::AppConfig
#include "AppController.h"  // StandaloneMonitorRow
#include "Events.h"         // LocateProxyEvent
#include "FloatingWidgetPolicy.h"

class AppController;
class CustomColorSlider;   // 自绘滑块，替换 wxSlider

// ---------------------------------------------------------------
// StandaloneFloatingWidget — top-level (parent-less) circular hover
// floating widget that replaces the old standalone monitor dialog.
//
// v1.2 circular design:
//   * Collapsed = a small circular "orb" (30%-smaller than the original
//     radius) showing ONLY the running-proxy count in green bold text,
//     drawn over a PNG background compiled into the exe resource
//     (float_monitor_process_png = float_monitor_process_1.png, loaded via
//     ToolbarIcons). No other text is drawn on the orb.
//   * Hovering the orb expands it into a rounded detail panel
//     (IndexId/Host/起始时间/运行时长(分)/监听端口/PID) refreshed by
//     a wxTimer; leaving (after a delay) collapses it back to the orb.
//   * The orb is draggable; on release it snaps (docks) to the nearest
//     screen edge. A bottom slider controls the collapse delay.
// ---------------------------------------------------------------
class StandaloneFloatingWidget : public wxFrame {
public:
    StandaloneFloatingWidget(const config::AppConfig& cfg,
                             AppController* controller,
                             wxWindow* parent);

    // Start/stop the refresh timer together with visibility.
    bool Show(bool show = true) override;

    // Hot-apply a new config: store cfg, restart the timer with the new
    // interval; if monitoring was disabled, mark inactive and hide.
    void applySettings(const config::AppConfig& newCfg);

    // Flip active state. When active && enabled -> show; otherwise hide.
    void toggleActive();

    // Explicitly set the active state (used at startup so isActive() stays
    // consistent with the visible pill). When on && enabled -> show.
    void setActive(bool on);

    bool isActive() const { return active_; }

private:
    enum class Mode { Orb, Panel };

    void onTimer(wxTimerEvent& event);
    void onClose(wxCloseEvent& event);
    void onEnterWindow(wxMouseEvent& event);
    void onLeaveWindow(wxMouseEvent& event);
    void onLeftDown(wxMouseEvent& event);
    void onLeftUp(wxMouseEvent& event);
    void onLeftDClick(wxMouseEvent& event);
    void onListLeftDClick(wxMouseEvent& event);  // 列表内任意区域双击 → 切换主界面
    void onMouseMove(wxMouseEvent& event);
    void toggleMainFrameMaximize();
    void onPaint(wxPaintEvent& event);
    void onEraseBackground(wxEraseEvent& event);
    void onSlider(wxCommandEvent& event);
    void onContextMenu(wxContextMenuEvent& event);
    void onMenuExit(wxCommandEvent& event);
    void onMenuCloseProxy(wxCommandEvent& event);
    void onMenuTestOnline(wxCommandEvent& event);
    void onTestOnlineProxiesEvent(TestOnlineProxiesEvent& event);
    void onActivate(wxActivateEvent& event);

    // --- 代理池统一监控（v1.4）---
    void onStartStopPool(wxCommandEvent& event);
    void onAddPoolMember(wxCommandEvent& event);
    void onRefreshPool(wxCommandEvent& event);
    void onToggleReport(wxCommandEvent& event);
    void onTogglePrune(wxCommandEvent& event);
    void onToggleOptimize(wxCommandEvent& event);
    void onMenuLocateProxy(wxCommandEvent& event);
    void onPoolMembersUpdated(PoolMembersUpdatedEvent& event);
    void updatePoolStatusText();

#ifdef __WXMSW__
    // 分层窗口逐像素 alpha：整颗悬浮球均可命中测试，保证可拖动/悬停。
    WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;
    void updateLayeredWindow(wxImage& img);
#endif
    void onItemActivated(wxListEvent& event);
    void onItemSelected(wxListEvent& event);

    void refreshRows();
    void setMode(Mode m, bool force = false);
    void applyShape(const wxPoint& keepCenter);
    void positionForCurrentEdge();
    void loadBackground();
    bool pointerInside() const;
    wxRegion buildCircleRegion(int d);
    wxRegion buildRoundedRegion(int w, int h, int corner);

    config::AppConfig cfg_;
    AppController* controller_;
    wxEvtHandler* locateTarget_;  // 定位事件投递目标（MainFrame）
    wxWindow* parentWindow_{nullptr};  // 结果对话框父窗口（MainFrame），保证定位事件可达
    wxTimer timer_;
    wxTimer hideTimer_;
    wxTimer hoverTimer_;
    wxListCtrl* list_{nullptr};
    CustomColorSlider* slider_{nullptr};
    std::vector<UnifiedMonitorRow> rows_;
    // --- 代理池统一监控控件（v1.4）---
    wxStaticText* poolStatusText_{nullptr};
    wxButton* startStopBtn_{nullptr};
    wxButton* addBtn_{nullptr};
    wxButton* refreshBtn_{nullptr};
    wxCheckBox* reportChk_{nullptr};
    wxCheckBox* pruneChk_{nullptr};
    wxCheckBox* optimizeChk_{nullptr};
    long contextMenuSel_{-1};  // 右键菜单弹出时选中的行，供 onMenuCloseProxy 使用
    bool active_{false};
    bool hovering_{false};
    bool dragging_{false};
    bool placed_{false};  // 首次显示后置 true；之后 Show 不再重新定位，保留用户自由落点
    Mode mode_{Mode::Orb};
    int radius_{FloatingWidgetPolicy::CircleDefaults::kDefaultRadius};
    int hideDelayMs_{FloatingWidgetPolicy::HideDelayDefaults::kDefaultMs};
    int hoverExpandDelayMs_{FloatingWidgetPolicy::HoverExpandDefaults::kDefaultMs};
    FloatingWidgetPolicy::DockEdge dockEdge_{FloatingWidgetPolicy::DockEdge::Right};
    wxPoint dragOffset_;
    wxPoint dragStartPos_;  // 记录按下位置，用于区分点击 vs 拖拽
    wxLongLong lastToggleTime_{0}; // 最近一次切换主窗最大化的时刻(ms)，防抖
    wxImage bgImage_;
};

#endif // UI_STANDALONE_FLOATING_WIDGET_H
