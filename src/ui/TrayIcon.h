#ifndef UI_TRAY_ICON_H
#define UI_TRAY_ICON_H

#include <wx/wx.h>
#include <wx/taskbar.h>

class MainFrame;

// ---------------------------------------------------------------
// TrayIcon — system tray icon with popup menu
// ---------------------------------------------------------------
class TrayIcon : public wxTaskBarIcon {
public:
    explicit TrayIcon(MainFrame* frame);
    ~TrayIcon() override;

    void showBalloon(const wxString& title, const wxString& msg);

private:
    wxMenu* CreatePopupMenu() override;

    void onLeftDClick(wxTaskBarIconEvent& event);
    void onMenuShow(wxCommandEvent& event);
    void onMenuHide(wxCommandEvent& event);
    void onMenuExit(wxCommandEvent& event);

    MainFrame* frame_;

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_TRAY_ICON_H
