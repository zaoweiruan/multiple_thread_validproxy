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
#include "PoolCandidate.h"

class AppController;

// ---------------------------------------------------------------
// AddPoolMemberDialog — searchable picker used by the standalone proxy pool to
// add one or more existing proxies (by IndexId) as pool members. The caller is
// responsible for injecting the selected indexIds via AppController::injectProxyToPool.
// Only valid proxies (delay > 0) are listed; columns are sortable by clicking
// the column header (ascending/descending toggle). Multi-select (Shift+click
// range, Ctrl+click toggle) is the wxListCtrl default (no wxLC_SINGLE_SEL).
// ---------------------------------------------------------------
class AddPoolMemberDialog : public wxDialog {
public:
    AddPoolMemberDialog(wxWindow* parent, AppController* controller);

    const std::vector<std::string>& getSelectedIndexIds() const { return selectedIndexIds_; }

private:
    void buildList(const std::wstring& filter);
    void onSearch(wxCommandEvent& event);
    void onColumnClick(wxListEvent& event);
    void onOK(wxCommandEvent& event);

    AppController* controller_;
    wxListCtrl* list_{nullptr};
    wxTextCtrl* search_{nullptr};
    std::vector<pool_candidate::PoolCandidateItem> candidates_;
    std::vector<std::string> rowIndexIds_;       // row -> IndexId (visible rows only)
    std::vector<std::string> selectedIndexIds_;  // result
    pool_candidate::CandidateSortKey sortColumn_{pool_candidate::CandidateSortKey::IndexId};
    bool ascending_{true};
};

#endif // UI_ADD_POOL_MEMBER_DIALOG_H
