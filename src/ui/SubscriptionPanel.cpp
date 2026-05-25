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
    ID_SUB_ADD = wxID_HIGHEST + 400,
    ID_SUB_EDIT,
    ID_SUB_DELETE,
    ID_SUB_UPDATE,
    ID_SUB_TEST,
    ID_SUB_IMPORT,
};

// -------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(SubscriptionPanel, wxPanel)
    EVT_DATAVIEW_SELECTION_CHANGED(wxID_ANY, SubscriptionPanel::onSelectionChanged)
    EVT_DATAVIEW_ITEM_CONTEXT_MENU(wxID_ANY, SubscriptionPanel::onContextMenu)
    EVT_MENU(ID_SUB_ADD, SubscriptionPanel::onAddSubscription)
    EVT_MENU(ID_SUB_EDIT, SubscriptionPanel::onEditSubscription)
    EVT_MENU(ID_SUB_DELETE, SubscriptionPanel::onDeleteSubscription)
    EVT_MENU(ID_SUB_UPDATE, SubscriptionPanel::onUpdateSubscription)
    EVT_MENU(ID_SUB_TEST, SubscriptionPanel::onTestSubscription)
    EVT_MENU(ID_SUB_IMPORT, SubscriptionPanel::onImportSubscription)
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

    // Store
    store_ = new wxDataViewListStore();
    listCtrl_->AssociateModel(store_);
    store_->DecRef(); // AssociateModel took ownership

    // Columns — Append[Toggle|Text]Column(label, model_column, mode, width, ...)
    listCtrl_->AppendTextColumn("#", 0, wxDATAVIEW_CELL_INERT, 30);  // Counter for row number
    listCtrl_->AppendToggleColumn("启用", 1, wxDATAVIEW_CELL_ACTIVATABLE, 30);  // Changed "On" to "启用"
    listCtrl_->AppendTextColumn("Name", 2, wxDATAVIEW_CELL_EDITABLE, 200);
    listCtrl_->AppendTextColumn("Proxies", 3, wxDATAVIEW_CELL_INERT, 70, wxALIGN_RIGHT);
    listCtrl_->AppendTextColumn("Update", 4, wxDATAVIEW_CELL_INERT, 130);

    sizer->Add(listCtrl_, 1, wxEXPAND | wxALL, 2);

    // Button bar
    wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    wxButton* addBtn = new wxButton(this, ID_SUB_ADD, "+", wxDefaultPosition, wxSize(28, 28));
    addBtn->SetToolTip("Add subscription");
    wxButton* refreshBtn = new wxButton(this, wxID_ANY, "Refresh", wxDefaultPosition, wxSize(-1, 28));
    refreshBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { loadSubscriptions(); });

    btnSizer->Add(addBtn, 0, wxRIGHT, 4);
    btnSizer->Add(refreshBtn, 0);
    btnSizer->AddStretchSpacer();
    sizer->Add(btnSizer, 0, wxEXPAND | wxALL, 2);

    SetSizer(sizer);

    // Connect toggle event - persist enabled state to database
    listCtrl_->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, [this](wxDataViewEvent& event) {
        int row = wxPtrToUInt(event.GetItem().GetID()) - 1;
        if (row >= 0 && row < (int)subs_.size()) {
            wxVariant val;
            event.GetModel()->GetValue(val, event.GetItem(), 1);
            bool enabled = val.GetBool();
            subs_[row].enabled = enabled ? "1" : "0";
            // Persist to database
            if (controller_) {
                controller_->updateSubscriptionEnabled(subs_[row].id, enabled);
            }
        }
    });
}

void SubscriptionPanel::loadSubscriptions() {
    subs_ = controller_->loadSubscriptions();
    store_->DeleteAllItems();

    int rowNum = 1;  // Counter for row number
    for (const auto& sub : subs_) {
        wxVector<wxVariant> row;
        row.push_back(wxVariant(wxString::Format("%d", rowNum++)));  // Counter for numbering
        row.push_back(wxVariant(sub.enabled == "1"));
        row.push_back(wxVariant(sub.remarks));

        // Count proxies for this subscription
        auto proxies = controller_->loadProxies(sub.id);
        row.push_back(wxVariant(wxString::Format("%d", static_cast<int>(proxies.size()))));
        
        // Format updatetime from unix timestamp to readable datetime
        wxString updateTimeStr = formatUpdateTime(sub.updatetime);
        row.push_back(wxVariant(updateTimeStr));
        store_->AppendItem(row);
    }
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
        wxWindow* parent = GetParent();
        if (parent) {
            SubscriptionSelectedEvent evt(subId);
            wxPostEvent(parent, evt);
        }
    }
    (void)event; // Suppress unused parameter warning
}

void SubscriptionPanel::onContextMenu(wxDataViewEvent&) {
    std::string subId = getSelectedSubId();
    wxMenu menu;
    menu.Append(ID_SUB_UPDATE, "Update");
    menu.Append(ID_SUB_TEST, "Test");
    menu.AppendSeparator();
    menu.Append(ID_SUB_EDIT, "Edit...");
    menu.Append(ID_SUB_DELETE, "Delete");
    menu.AppendSeparator();
    menu.Append(ID_SUB_ADD, "Add...");
    menu.Append(ID_SUB_IMPORT, "Import from URL...");

    // Disable context-dependent items if nothing selected
    if (subId.empty()) {
        menu.Enable(ID_SUB_UPDATE, false);
        menu.Enable(ID_SUB_TEST, false);
        menu.Enable(ID_SUB_EDIT, false);
        menu.Enable(ID_SUB_DELETE, false);
    }

    PopupMenu(&menu);
}

void SubscriptionPanel::onAddSubscription(wxCommandEvent&) {
    showAddDialog();
}

void SubscriptionPanel::onEditSubscription(wxCommandEvent&) {
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
    std::string subId = getSelectedSubId();
    if (subId.empty()) return;
    std::string remarks;
    for (const auto& sub : subs_) {
        if (sub.id == subId) { remarks = sub.remarks; break; }
    }
    if (confirmDelete(subId, remarks)) {
        // Delete via DAO
        // TODO: implement DAO delete method
        loadSubscriptions();
    }
}

void SubscriptionPanel::onUpdateSubscription(wxCommandEvent&) {
    std::string subId = getSelectedSubId();
    if (!subId.empty()) {
        controller_->updateSubscriptionAsync(subId, GetParent());
    }
}

void SubscriptionPanel::onTestSubscription(wxCommandEvent&) {
    std::string subId = getSelectedSubId();
    if (!subId.empty()) {
        if (GetParent()) {
            wxQueueEvent(GetParent(), new SubscriptionTestEvent(subId));
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

void SubscriptionPanel::showAddDialog() {
    wxDialog dlg(this, wxID_ANY, "Add Subscription", wxDefaultPosition, wxSize(450, 300));
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
    wxFlexGridSizer* grid = new wxFlexGridSizer(2, 10, 10);

    wxTextCtrl* urlCtrl;
    wxTextCtrl* nameCtrl;
    wxTextCtrl* uaCtrl;
    wxCheckBox* enabledCtrl;

    grid->Add(new wxStaticText(&dlg, wxID_ANY, "URL:"));
    urlCtrl = new wxTextCtrl(&dlg, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    grid->Add(urlCtrl, 1, wxEXPAND);

    grid->Add(new wxStaticText(&dlg, wxID_ANY, "Name:"));
    nameCtrl = new wxTextCtrl(&dlg, wxID_ANY, "");
    grid->Add(nameCtrl, 1, wxEXPAND);

    grid->Add(new wxStaticText(&dlg, wxID_ANY, "User-Agent:"));
    uaCtrl = new wxTextCtrl(&dlg, wxID_ANY, "");
    grid->Add(uaCtrl, 1, wxEXPAND);

    grid->Add(new wxStaticText(&dlg, wxID_ANY, "Enabled:"));
    enabledCtrl = new wxCheckBox(&dlg, wxID_ANY, "");
    enabledCtrl->SetValue(true);
    grid->Add(enabledCtrl);

    topSizer->Add(grid, 1, wxEXPAND | wxALL, 10);

    wxSizer* btnSizer = dlg.CreateSeparatedButtonSizer(wxOK | wxCANCEL);
    if (btnSizer) topSizer->Add(btnSizer, 0, wxEXPAND | wxALL, 10);

    dlg.SetSizer(topSizer);
    topSizer->Fit(&dlg);

    if (dlg.ShowModal() == wxID_OK) {
        wxString url = urlCtrl->GetValue();
        if (!url.empty()) {
            controller_->importSubscription(url.ToStdString());
            loadSubscriptions();
        }
    }
}

void SubscriptionPanel::showEditDialog(const db::models::Subitem& sub) {
    wxDialog dlg(this, wxID_ANY, "Edit Subscription", wxDefaultPosition, wxSize(500, 400));
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
    wxFlexGridSizer* grid = new wxFlexGridSizer(2, 10, 10);
    grid->AddGrowableCol(1);

    wxTextCtrl* nameCtrl;
    wxTextCtrl* urlCtrl;
    wxTextCtrl* uaCtrl;
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

    grid->Add(new wxStaticText(&dlg, wxID_ANY, "User-Agent:"));
    uaCtrl = new wxTextCtrl(&dlg, wxID_ANY, sub.useragent);
    grid->Add(uaCtrl, 1, wxEXPAND);

    topSizer->Add(grid, 1, wxEXPAND | wxALL, 10);
    wxSizer* btnSizer = dlg.CreateSeparatedButtonSizer(wxOK | wxCANCEL);
    if (btnSizer) topSizer->Add(btnSizer, 0, wxEXPAND | wxALL, 10);

    dlg.SetSizer(topSizer);
    topSizer->Fit(&dlg);
    dlg.SetMinSize(wxSize(450, 350));

    if (dlg.ShowModal() == wxID_OK) {
        // TODO: implement DAO update
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
