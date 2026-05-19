#include "TrayIcon.h"
#include "MainFrame.h"
#include <wx/artprov.h>

// -------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(TrayIcon, wxTaskBarIcon)
    EVT_TASKBAR_LEFT_DCLICK(TrayIcon::onLeftDClick)
    EVT_MENU(wxID_ANY, TrayIcon::onMenuShow)
    EVT_MENU(wxID_ANY, TrayIcon::onMenuHide)
    EVT_MENU(wxID_ANY, TrayIcon::onMenuExit)
wxEND_EVENT_TABLE()

enum {
    ID_TRAY_SHOW = wxID_HIGHEST + 1000,
    ID_TRAY_HIDE,
    ID_TRAY_EXIT,
};

// -------------------------------------------------------------------
TrayIcon::TrayIcon(MainFrame* frame)
    : frame_(frame)
{
    // Use a built-in wxWidgets icon
    wxIcon icon = wxArtProvider::GetIcon(wxART_INFORMATION, wxART_OTHER, wxSize(16, 16));
    SetIcon(icon, "validproxy");
}

TrayIcon::~TrayIcon() = default;

wxMenu* TrayIcon::CreatePopupMenu() {
    wxMenu* menu = new wxMenu();
    menu->Append(ID_TRAY_SHOW, "&Show Window");
    menu->Append(ID_TRAY_HIDE, "&Hide Window");
    menu->AppendSeparator();
    menu->Append(ID_TRAY_EXIT, "E&xit");
    return menu;
}

void TrayIcon::showBalloon(const wxString& title, const wxString& msg) {
    ShowBalloon(title, msg, 3000, wxICON_INFORMATION);
}

void TrayIcon::onLeftDClick(wxTaskBarIconEvent&) {
    if (frame_) {
        frame_->Show(true);
        frame_->Raise();
        frame_->Iconize(false);
    }
}

void TrayIcon::onMenuShow(wxCommandEvent&) {
    if (frame_) {
        frame_->Show(true);
        frame_->Raise();
    }
}

void TrayIcon::onMenuHide(wxCommandEvent&) {
    if (frame_) {
        frame_->Show(false);
    }
}

void TrayIcon::onMenuExit(wxCommandEvent&) {
    if (frame_) {
        frame_->Close(true);
    }
}
