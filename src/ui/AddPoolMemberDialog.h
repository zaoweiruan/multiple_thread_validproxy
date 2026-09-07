#ifndef UI_ADD_POOL_MEMBER_DIALOG_H
#define UI_ADD_POOL_MEMBER_DIALOG_H

#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <vector>
#include <string>

#include "Profileitem.h"

class AppController;

// ---------------------------------------------------------------
// AddPoolMemberDialog — searchable picker used by the standalone proxy pool to
// add one or more existing proxies (by IndexId) as pool members. The caller is
// responsible for injecting the selected indexIds via AppController::injectProxyToPool.
// ---------------------------------------------------------------
class AddPoolMemberDialog : public wxDialog {
public:
    AddPoolMemberDialog(wxWindow* parent, AppController* controller);

    const std::vector<std::string>& getSelectedIndexIds() const { return selectedIndexIds_; }

private:
    void buildList(const std::wstring& filter);
    void onSearch(wxCommandEvent& event);
    void onOK(wxCommandEvent& event);

    AppController* controller_;
    wxListCtrl* list_{nullptr};
    wxTextCtrl* search_{nullptr};
    std::vector<db::models::Profileitem> candidates_;
    std::vector<std::string> rowIndexIds_;       // row -> IndexId (visible rows only)
    std::vector<std::string> selectedIndexIds_;  // result
};

#endif // UI_ADD_POOL_MEMBER_DIALOG_H
