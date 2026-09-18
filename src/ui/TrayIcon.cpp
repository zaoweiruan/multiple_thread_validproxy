#include "TrayIcon.h"
#include "MainFrame.h"
#include <wx/artprov.h>
#include <algorithm>
#include <vector>

// ID_TRAY_EXIT 数值 = wxID_HIGHEST + 1002，已被 UI 测试路径研究
// (WM_COMMAND 模拟退出) 记录为稳定常量，不得改动。
enum {
    ID_TRAY_EXIT = wxID_HIGHEST + 1002,
};

// -------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(TrayIcon, wxTaskBarIcon)
    EVT_TASKBAR_LEFT_DCLICK(TrayIcon::onLeftDClick)
    EVT_MENU(ID_TRAY_EXIT, TrayIcon::onMenuExit)
wxEND_EVENT_TABLE()

// -------------------------------------------------------------------
TrayIcon::TrayIcon(MainFrame* frame)
    : frame_(frame)
{
    // 托盘图标使用主图标（icon.ico → 资源 "icon_ico"），与 MainFrame 一致；
    // 加载失败时回退内置 wxArtProvider 图标（保证托盘必须可用）。
#ifdef __WXMSW__
    wxIcon icon("icon_ico", wxBITMAP_TYPE_ICO_RESOURCE);
#else
    wxIcon icon;
#endif
    if (!icon.IsOk()) {
        icon = wxArtProvider::GetIcon(wxART_INFORMATION, wxART_OTHER, wxSize(16, 16));
    }

    // 差分捕获 wxTaskBarIcon 内部懒创建的辅助窗口 m_win（wxTaskBarIconWindow
    // 是无父、无标题的顶层 wxFrame，为 Shell_NotifyIcon 提供 HWND）。
    // m_win 为 wxTaskBarIcon 私有成员，无公开 API 可引用，故对全局
    // wxTopLevelWindows 列表做 SetIcon 前后快照差分，新增项即 m_win。
    // 根因：WM_CLOSE 广播（UI 测试 terminate / 任务管理器"结束任务" / 系统
    // 关机注销）命中 m_win 时走 wxFrame 默认 Destroy() 进入 wxPendingDelete，
    // 而 ~wxTaskBarIcon 仍会 raw delete m_win → 双删除 UAF。
    // 修复：拦截其 wxEVT_CLOSE_WINDOW（无捕获空 lambda，不 Skip）——吞掉
    // 外部关闭请求，m_win 只允许经 ~wxTaskBarIcon 释放。
    // 详见 docs/bugfix/2026-09-11-Bugfix-TrayIcon-HelperWindow-DoubleDelete-v1.0.md
    std::vector<wxWindow*> topLevelBefore;
    for (wxWindowList::compatibility_iterator node = wxTopLevelWindows.GetFirst();
         node; node = node->GetNext()) {
        topLevelBefore.push_back(node->GetData());
    }

    SetIcon(icon, "validproxy");

    for (wxWindowList::compatibility_iterator node = wxTopLevelWindows.GetFirst();
         node; node = node->GetNext()) {
        wxWindow* win = node->GetData();
        if (std::find(topLevelBefore.begin(), topLevelBefore.end(), win)
            == topLevelBefore.end()) {
            win->Bind(wxEVT_CLOSE_WINDOW,
                      [](wxCloseEvent&) {
                          // 有意吞掉：m_win 生命周期完全归属 ~wxTaskBarIcon
                          // 的 raw delete，外部关闭一律不进入默认 Destroy。
                          // 不调用 event.Skip()，保持窗口存活。
                      });
        }
    }
}

TrayIcon::~TrayIcon() {
    // RemoveIcon 通知 shell 移除任务栏通知区域图标，防止 MainFrame 销毁后
    // shell 仍向本对象持有的隐藏窗口投递通知事件 → 消息循环永久 pump，
    // 进程无法退出 (hang)。
    RemoveIcon();
}

wxMenu* TrayIcon::CreatePopupMenu() {
    // 菜单精简为仅「退出」——「显示/隐藏」被双击切换取代
    // (Spec: docs/design/2026-09-04-Design-TrayIcon-DoubleClickToggle-v1.0.md)
    wxMenu* menu = new wxMenu();
    menu->Append(ID_TRAY_EXIT, "退&出");
    return menu;
}

void TrayIcon::showBalloon(const wxString& title, const wxString& msg) {
    ShowBalloon(title, msg, 3000, wxICON_INFORMATION);
}

void TrayIcon::onLeftDClick(wxTaskBarIconEvent&) {
    // 与 StandaloneFloatingWidget::toggleMainFrameMaximize 语义一致：
    // 最大化 → Iconize(true) → MainFrame::onIconize 隐藏到托盘；
    // 否则（窗口化/最小化/托盘隐藏）→ Show + Maximize 恢复最大化。
    // 无需防抖：EVT_TASKBAR_LEFT_DCLICK 是单一原生事件源，
    // 不同于悬浮窗手动双击与系统消息并存的路径。
    if (frame_) {
        if (frame_->IsMaximized()) {
            frame_->Iconize(true);
        } else {
            if (!frame_->IsShown()) {
                frame_->Show(true);
                frame_->Raise();
            }
            frame_->Maximize(true);
        }
    }
}

void TrayIcon::onMenuExit(wxCommandEvent&) {
    if (frame_) {
        frame_->Close(true);
    }
}
