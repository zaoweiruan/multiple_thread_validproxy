#ifndef UI_STANDALONE_POOL_DIALOG_H
#define UI_STANDALONE_POOL_DIALOG_H

#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <vector>
#include <string>

#include "StandaloneProxyPool.h"
#include "AddPoolMemberDialog.h"

class AppController;

// ---------------------------------------------------------------
// StandalonePoolDialog — management UI for the single-process proxy pool.
// Lists current members (tag px-<indexId>) with live health, allows graceful
// removal, and toggles the a/b/c policy switches (reportHealth / autoPruneDead
// / autoOptimize). Refreshed via MainFrame forwarding PoolMembersUpdatedEvent.
// ---------------------------------------------------------------
class StandalonePoolDialog : public wxDialog {
public:
    StandalonePoolDialog(wxWindow* parent, AppController* controller);

    // Replace the member list with a fresh snapshot (called from MainFrame on
    // PoolMembersUpdatedEvent, and on manual refresh).
    void setMembers(const std::vector<proxy::PoolMemberView>& members);

private:
    void onStartStop(wxCommandEvent& event);
    void onDelete(wxCommandEvent& event);
    void onRefresh(wxCommandEvent& event);
    void onAdd(wxCommandEvent& event);
    void onToggleReport(wxCommandEvent& event);
    void onTogglePrune(wxCommandEvent& event);
    void onToggleOptimize(wxCommandEvent& event);
    void updateStatusText();

    AppController* controller_;
    wxListCtrl* list_{nullptr};
    wxButton* startStopBtn_{nullptr};
    wxButton* addBtn_{nullptr};
    wxStaticText* statusText_{nullptr};
    wxCheckBox* reportChk_{nullptr};
    wxCheckBox* pruneChk_{nullptr};
    wxCheckBox* optimizeChk_{nullptr};
    std::vector<proxy::PoolMemberView> currentMembers_;
};

#endif // UI_STANDALONE_POOL_DIALOG_H
