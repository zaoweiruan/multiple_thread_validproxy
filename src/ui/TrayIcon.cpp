#include "TrayIcon.h"
#include "MainFrame.h"
#include <wx/artprov.h>

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
    SetIcon(icon, "validproxy");
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
