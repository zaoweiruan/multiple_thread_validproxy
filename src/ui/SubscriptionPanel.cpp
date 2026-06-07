#include "SubscriptionPanel.h"
#include "AppController.h"
#include "Events.h"

#include <wx/sizer.h>
#include <wx/dataview.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/textdlg.h>
#include <wx/checkbox.h>
#include <ctime>
#include <iomanip>
#include <sstream>

enum {
    ID_SUB_EDIT = wxID_HIGHEST + 400,
    ID_SUB_DELETE,
    ID_SUB_UPDATE,
    ID_SUB_TEST,
    ID_SUB_IMPORT,
    ID_SUB_REFRESH,
};

// -------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(SubscriptionPanel, wxPanel)
    EVT_DATAVIEW_SELECTION_CHANGED(wxID_ANY, SubscriptionPanel::onSelectionChanged)
    EVT_DATAVIEW_ITEM_CONTEXT_MENU(wxID_ANY, SubscriptionPanel::onContextMenu)
    EVT_MENU(ID_SUB_EDIT, SubscriptionPanel::onEditSubscription)
    EVT_MENU(ID_SUB_DELETE, SubscriptionPanel::onDeleteSubscription)
    EVT_MENU(ID_SUB_UPDATE, SubscriptionPanel::onUpdateSubscription)
    EVT_MENU(ID_SUB_TEST, SubscriptionPanel::onTestSubscription)
    EVT_MENU(ID_SUB_IMPORT, SubscriptionPanel::onImportSubscription)
    EVT_MENU(ID_SUB_REFRESH, SubscriptionPanel::onRefreshSubscription)
wxEND_EVENT_TABLE()

// -------------------------------------------------------------------
SubscriptionPanel::SubscriptionPanel(wxWindow* parent, AppController* controller)
    : wxPanel(parent, wxID_ANY),
      controller_(controller)
{
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    // Data view control
    listCtrl_ = new wxDataViewCtrl(this, wxID_ANY,
                                     wxDefaultPosition, wxDefaultSize,
                                     wxDV_ROW_LINES | wxDV_SINGLE);

    // Model
    model_ = new SubscriptionListModel();
    listCtrl_->AssociateModel(model_);
    model_->DecRef(); // AssociateModel took ownership

    // Columns — Append[Toggle|Text]Column(label, model_column, mode, width, ...)
    listCtrl_->AppendTextColumn("#", 0, wxDATAVIEW_CELL_INERT, 30);
    listCtrl_->AppendToggleColumn("启用", 1, wxDATAVIEW_CELL_ACTIVATABLE, 30);
    listCtrl_->AppendTextColumn("Name ↕", 2, wxDATAVIEW_CELL_INERT, 200);  // Changed to INERT to prevent editing
    listCtrl_->AppendTextColumn("Proxies ↕", 3, wxDATAVIEW_CELL_INERT, 70, wxALIGN_RIGHT);
    listCtrl_->AppendTextColumn("Update ↕", 4, wxDATAVIEW_CELL_INERT, 130);

    sizer->Add(listCtrl_, 1, wxEXPAND | wxALL, 2);

    // (no button bar — Add and Refresh are now in context menu)

    SetSizer(sizer);

    // Connect toggle event - persist enabled state to database
    listCtrl_->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, [this](wxDataViewEvent& event) {
        int row = wxPtrToUInt(event.GetItem().GetID()) - 1;
        if (row >= 0 && row < (int)subs_.size()) {
            // Model already updated by SetValueByRow, just persist to database
            if (controller_) {
                wxVariant val;
                event.GetModel()->GetValue(val, event.GetItem(), 1);
                bool enabled = val.GetBool();
                controller_->updateSubscriptionEnabled(subs_[row].id, enabled);
            }
        }
    });

    // Bind column header click for sorting
    listCtrl_->Bind(wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, &SubscriptionPanel::onColumnHeaderClick, this);
}

void SubscriptionPanel::updateSubscriptionList(const std::vector<db::models::Subitem>& subs,
                                                 const std::unordered_map<std::string, int>& proxyCounts) {
    subs_ = subs;
    proxyCounts_ = proxyCounts;
    model_->setData(&subs_, &proxyCounts_);
    model_->Reset(0);
    model_->Reset(static_cast<unsigned int>(subs_.size()));
    model_->detectIdOffset();
}

void SubscriptionPanel::loadSubscriptions() {
    auto subs = controller_->loadSubscriptions();

    // Single efficient GROUP BY query for all proxy counts instead of N+1 full-table scans
    auto proxyCounts = controller_->countProxiesBySubId();

    updateSubscriptionList(subs, proxyCounts);
 }

void SubscriptionPanel::loadSubscriptions(const std::vector<db::models::Subitem>& subs,
                                           const std::unordered_map<std::string, int>& proxyCounts) {
    updateSubscriptionList(subs, proxyCounts);
 }

 std::string SubscriptionPanel::formatUpdateTime(const std::string& updatetime) {
     if (updatetime.empty()) {
         return "Never";
     }
     try {
         long timestamp = std::stol(updatetime);
         if (timestamp <= 0) {
             return "Never";
         }
         time_t t = static_cast<time_t>(timestamp);
         struct tm tm_time;
         localtime_s(&tm_time, &t);
         char buf[32];
         strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm_time);
         return std::string(buf);
     } catch (...) {
         return updatetime;  // Return as-is if parsing fails
     }
 }

std::string SubscriptionPanel::getSelectedSubId() const {
    wxDataViewItem sel = listCtrl_->GetSelection();
    if (!sel.IsOk()) return "";
    int row = wxPtrToUInt(sel.GetID()) - 1;
    if (row >= 0 && row < (int)subs_.size()) {
        return subs_[row].id;
    }
    return "";
}

void SubscriptionPanel::onSelectionChanged(wxDataViewEvent& event) {
    std::string subId = getSelectedSubId();
    if (!subId.empty()) {
        // Notify parent frame to load proxies for this subscription
        // Use wxGetTopLevelParent instead of GetParent() because this panel
        // is now parented to centerPanel (wxAui wrapper), not MainFrame directly
        wxWindow* topLevel = wxGetTopLevelParent(this);
        if (topLevel) {
            SubscriptionSelectedEvent evt(subId);
            wxPostEvent(topLevel, evt);
        }
    }
    (void)event; // Suppress unused parameter warning
}

void SubscriptionPanel::onContextMenu(wxDataViewEvent&) {
    std::string subId = getSelectedSubId();
    wxMenu menu;
    menu.Append(ID_SUB_UPDATE, "更新订阅");
    menu.Append(ID_SUB_TEST, "测试订阅");
    menu.AppendSeparator();
    menu.Append(ID_SUB_EDIT, "编辑订阅");
    menu.Append(ID_SUB_DELETE, "删除订阅");
    menu.AppendSeparator();
    menu.Append(ID_SUB_REFRESH, "刷新");
    menu.Append(ID_SUB_IMPORT, "添加订阅");

    // Disable context-dependent items if nothing selected
    if (subId.empty()) {
        menu.Enable(ID_SUB_UPDATE, false);
        menu.Enable(ID_SUB_TEST, false);
        menu.Enable(ID_SUB_EDIT, false);
        menu.Enable(ID_SUB_DELETE, false);
    }

    PopupMenu(&menu);
}

void SubscriptionPanel::onRefreshSubscription(wxCommandEvent&) {
    loadSubscriptions();
}

void SubscriptionPanel::onEditSubscription(wxCommandEvent&) {
    if (controller_ && controller_->isRunning()) {
        wxMessageBox(L"操作进行中，请等待完成后再试", L"操作进行中", wxOK | wxICON_WARNING);
        return;
    }
    std::string subId = getSelectedSubId();
    if (subId.empty()) return;
    for (const auto& sub : subs_) {
        if (sub.id == subId) {
            showEditDialog(sub);
            break;
        }
    }
}

void SubscriptionPanel::onDeleteSubscription(wxCommandEvent&) {
    if (controller_ && controller_->isRunning()) {
        wxMessageBox(L"操作进行中，请等待完成后再试", L"操作进行中", wxOK | wxICON_WARNING);
        return;
    }
    std::string subId = getSelectedSubId();
    if (subId.empty()) return;
    std::string remarks;
    for (const auto& sub : subs_) {
        if (sub.id == subId) { remarks = sub.remarks; break; }
    }
    if (confirmDelete(subId, remarks)) {
        if (controller_) {
            controller_->deleteSubscription(subId);
        }
        loadSubscriptions();
    }
}

void SubscriptionPanel::onUpdateSubscription(wxCommandEvent&) {
    if (controller_ && controller_->isRunning()) {
        wxMessageBox(L"操作进行中，请等待完成后再试", L"操作进行中", wxOK | wxICON_WARNING);
        return;
    }
    std::string subId = getSelectedSubId();
    if (!subId.empty()) {
        // Pass top-level frame as event sink (parent is now centerPanel, not MainFrame)
        controller_->updateSubscriptionAsync(subId, wxGetTopLevelParent(this));
    }
}

void SubscriptionPanel::onTestSubscription(wxCommandEvent&) {
    if (controller_ && controller_->isRunning()) {
        wxMessageBox(L"操作进行中，请等待完成后再试", L"操作进行中", wxOK | wxICON_WARNING);
        return;
    }
    std::string subId = getSelectedSubId();
    if (!subId.empty()) {
        wxWindow* topLevel = wxGetTopLevelParent(this);
        if (topLevel) {
            wxQueueEvent(topLevel, new SubscriptionTestEvent(subId));
        }
    }
}

void SubscriptionPanel::onImportSubscription(wxCommandEvent&) {
    wxTextEntryDialog dlg(this, "Enter subscription URL:", "Import Subscription", "");
    if (dlg.ShowModal() == wxID_OK) {
        wxString url = dlg.GetValue();
        if (!url.empty()) {
            controller_->importSubscription(url.ToStdString());
            loadSubscriptions();
        }
    }
}

// -------------------------------------------------------------------
// Column header click handler for Name/Proxies/Update sorting
// -------------------------------------------------------------------
void SubscriptionPanel::onColumnHeaderClick(wxDataViewEvent& event) {
    int col = event.GetColumn();
    Logger::write("[SubscriptionPanel] Column header click: column=" + std::to_string(col), LogLevel::DEBUG);

    // Only Name (SUB_COL_NAME=2), Proxies (SUB_COL_PROXIES=3), and Update (SUB_COL_UPDATE=4) are sortable
    if (col == SUB_COL_NAME || col == SUB_COL_PROXIES || col == SUB_COL_UPDATE) {
        // Cycle direction: None -> Asc -> Desc -> None
        if (sortState_.column == col) {
            switch (sortState_.direction) {
                case SortDirection::Asc:
                    sortState_.direction = SortDirection::Desc;
                    break;
                case SortDirection::Desc:
                    sortState_.direction = SortDirection::None;
                    sortState_.column = -1;
                    break;
                default:
                    sortState_.direction = SortDirection::Asc;
                    break;
            }
        } else {
            sortState_.column = col;
            sortState_.direction = SortDirection::Asc;
        }

        if (sortState_.direction != SortDirection::None) {
            // Set the sort indicator on the column and trigger re-sort.
            wxDataViewColumn* dvCol = listCtrl_->GetColumn(col);
            if (dvCol) {
                dvCol->SetSortOrder(sortState_.direction == SortDirection::Asc);
            }
            listCtrl_->GetModel()->Resort();
        } else {
            // Clear sort — remove indicator and restore identity order
            wxDataViewColumn* currentSort = listCtrl_->GetSortingColumn();
            if (currentSort) {
                currentSort->UnsetAsSortKey();
            }
            model_->Reset(0);
            model_->Reset(static_cast<unsigned int>(subs_.size()));
            model_->detectIdOffset();  // Required to restore correct ID mapping after Reset
        }
    }
    event.Skip();
}

void SubscriptionPanel::showEditDialog(const db::models::Subitem& sub) {
    wxDialog dlg(this, wxID_ANY, "Edit Subscription", wxDefaultPosition, wxSize(1000, 270));
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
    wxFlexGridSizer* grid = new wxFlexGridSizer(2, 8, 4);
    grid->AddGrowableCol(1);

    wxTextCtrl* nameCtrl;
    wxTextCtrl* urlCtrl;
    wxTextCtrl* intervalCtrl;
    wxCheckBox* enabledCtrl;

    grid->Add(new wxStaticText(&dlg, wxID_ANY, "ID:"));
    grid->Add(new wxStaticText(&dlg, wxID_ANY, sub.id));

    grid->Add(new wxStaticText(&dlg, wxID_ANY, "Name:"));
    nameCtrl = new wxTextCtrl(&dlg, wxID_ANY, sub.remarks);
    grid->Add(nameCtrl, 1, wxEXPAND);

    grid->Add(new wxStaticText(&dlg, wxID_ANY, "URL:"));
    urlCtrl = new wxTextCtrl(&dlg, wxID_ANY, sub.url);
    grid->Add(urlCtrl, 1, wxEXPAND);

    grid->Add(new wxStaticText(&dlg, wxID_ANY, "Enabled:"));
    enabledCtrl = new wxCheckBox(&dlg, wxID_ANY, "");
    enabledCtrl->SetValue(sub.enabled == "1");
    grid->Add(enabledCtrl);

    // Current interval value (may be empty)
    int curMinutes = 0;
    if (!sub.autoupdateinterval.empty()) {
        curMinutes = std::stoi(sub.autoupdateinterval);
    }
    grid->Add(new wxStaticText(&dlg, wxID_ANY, "Update Interval (min):"));
    intervalCtrl = new wxTextCtrl(&dlg, wxID_ANY,
                                  curMinutes > 0 ? wxString::Format("%d", curMinutes) : "");
    grid->Add(intervalCtrl, 1, wxEXPAND);

    topSizer->Add(grid, 1, wxEXPAND | wxALL, 6);
    wxSizer* btnSizer = dlg.CreateSeparatedButtonSizer(wxOK | wxCANCEL);
    if (btnSizer) topSizer->Add(btnSizer, 0, wxEXPAND | wxALL, 6);

    dlg.SetSizer(topSizer);
    dlg.Layout();
    dlg.SetMinSize(wxSize(900, 250));

    if (dlg.ShowModal() == wxID_OK) {
        db::models::Subitem updated = sub;
        updated.remarks = nameCtrl->GetValue().ToStdString();
        updated.url = urlCtrl->GetValue().ToStdString();
        updated.enabled = enabledCtrl->GetValue() ? "1" : "0";
        updated.useragent = sub.useragent;  // preserve existing
        updated.autoupdateinterval = intervalCtrl->GetValue().ToStdString();

        controller_->updateSubitem(updated);
        loadSubscriptions();
    }
}

bool SubscriptionPanel::confirmDelete(const std::string& id, const std::string& remarks) {
    wxString msg = wxString::Format("Delete subscription \"%s\" (ID: %s)?\nThis action cannot be undone.",
                                     remarks, id);
    wxMessageDialog dlg(this, msg, "Confirm Delete",
                         wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
    return dlg.ShowModal() == wxID_YES;
}
