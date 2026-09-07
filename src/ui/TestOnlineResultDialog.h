#ifndef UI_TEST_ONLINE_RESULT_DIALOG_H
#define UI_TEST_ONLINE_RESULT_DIALOG_H

#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/event.h>

#include <string>
#include <vector>

// ---------------------------------------------------------------
// TestOnlineResultDialog — summary popup shown after a「在线代理测试」
// (standalone proxy online test) completes. It lists every failed proxy
// (by IndexId). A single click on a failed row posts a LocateProxyEvent to
// the top-level frame (which locates the proxy in both the Subscription panel
// and the Proxy List panel) and closes the dialog.
// ---------------------------------------------------------------
class TestOnlineResultDialog : public wxDialog {
public:
    TestOnlineResultDialog(wxWindow* parent,
                           const std::vector<std::string>& failedIndexIds,
                           int total, int success, int failed);

private:
    void buildList();
    void onItemSelected(wxListEvent& event);
    void onClose(wxCommandEvent& event);

    wxListCtrl* list_;
    std::vector<std::string> rowIndexIds_;  // row -> IndexId
};

#endif // UI_TEST_ONLINE_RESULT_DIALOG_H