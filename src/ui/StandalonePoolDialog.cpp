#include "StandalonePoolDialog.h"
#include "AppController.h"
#include "Logger.h"
#include <wx/msgdlg.h>

StandalonePoolDialog::StandalonePoolDialog(wxWindow* parent, AppController* controller)
    : wxDialog(parent, wxID_ANY, "独立代理池", wxDefaultPosition, wxSize(660, 440)),
      controller_(controller) {
    wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

    statusText_ = new wxStaticText(this, wxID_ANY, "代理池状态: 未运行");
    top->Add(statusText_, 0, wxALL, 8);

    list_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(620, 250),
                           wxLC_REPORT | wxLC_SINGLE_SEL);
    list_->AppendColumn("Tag", wxLIST_FORMAT_LEFT, 120);
    list_->AppendColumn("状态", wxLIST_FORMAT_LEFT, 110);
    list_->AppendColumn("延迟(ms)", wxLIST_FORMAT_LEFT, 80);
    list_->AppendColumn("存活", wxLIST_FORMAT_LEFT, 60);
    list_->AppendColumn("失败次数", wxLIST_FORMAT_LEFT, 70);
    list_->AppendColumn("错误", wxLIST_FORMAT_LEFT, 170);
    top->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

    wxBoxSizer* chkSizer = new wxBoxSizer(wxHORIZONTAL);
    reportChk_ = new wxCheckBox(this, wxID_ANY, "上报健康");
    pruneChk_ = new wxCheckBox(this, wxID_ANY, "自动剔除死亡");
    optimizeChk_ = new wxCheckBox(this, wxID_ANY, "自动优化");
    if (controller_) {
        config::AppConfig cfg = controller_->getConfig();
        reportChk_->SetValue(cfg.standalone_pool.evaluate.reportHealth);
        pruneChk_->SetValue(cfg.standalone_pool.evaluate.autoPruneDead);
        optimizeChk_->SetValue(cfg.standalone_pool.evaluate.autoOptimize);
    }
    chkSizer->Add(reportChk_, 0, wxRIGHT, 10);
    chkSizer->Add(pruneChk_, 0, wxRIGHT, 10);
    chkSizer->Add(optimizeChk_, 0, wxRIGHT, 10);
    top->Add(chkSizer, 0, wxALL, 8);

    wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    startStopBtn_ = new wxButton(this, wxID_ANY, "启动池");
    wxButton* delBtn = new wxButton(this, wxID_ANY, "删除选中");
    wxButton* refreshBtn = new wxButton(this, wxID_ANY, "刷新");
    addBtn_ = new wxButton(this, wxID_ANY, "添加代理");
    btnSizer->Add(startStopBtn_, 0, wxRIGHT, 8);
    btnSizer->Add(delBtn, 0, wxRIGHT, 8);
    btnSizer->Add(refreshBtn, 0, wxRIGHT, 8);
    btnSizer->Add(addBtn_, 0, wxRIGHT, 8);
    top->Add(btnSizer, 0, wxALL, 8);

    SetSizerAndFit(top);

    startStopBtn_->Bind(wxEVT_BUTTON, &StandalonePoolDialog::onStartStop, this);
    delBtn->Bind(wxEVT_BUTTON, &StandalonePoolDialog::onDelete, this);
    refreshBtn->Bind(wxEVT_BUTTON, &StandalonePoolDialog::onRefresh, this);
    addBtn_->Bind(wxEVT_BUTTON, &StandalonePoolDialog::onAdd, this);
    reportChk_->Bind(wxEVT_CHECKBOX, &StandalonePoolDialog::onToggleReport, this);
    pruneChk_->Bind(wxEVT_CHECKBOX, &StandalonePoolDialog::onTogglePrune, this);
    optimizeChk_->Bind(wxEVT_CHECKBOX, &StandalonePoolDialog::onToggleOptimize, this);

    updateStatusText();
    if (controller_) setMembers(controller_->getPoolMembers());
}

void StandalonePoolDialog::setMembers(const std::vector<proxy::PoolMemberView>& members) {
    currentMembers_ = members;
    list_->DeleteAllItems();
    for (std::size_t i = 0; i < members.size(); ++i) {
        long idx = static_cast<long>(i);
        list_->InsertItem(idx, members[i].tag);
        list_->SetItem(idx, 1, members[i].state);
        list_->SetItem(idx, 2, std::to_string(members[i].lastDelayMs));
        list_->SetItem(idx, 3, members[i].lastAlive ? "是" : "否");
        list_->SetItem(idx, 4, std::to_string(members[i].failStreak));
        list_->SetItem(idx, 5, members[i].lastError);
    }
    updateStatusText();
}

void StandalonePoolDialog::updateStatusText() {
    if (!controller_) {
        statusText_->SetLabel("代理池状态: 无控制器");
        return;
    }
    bool running = controller_->isProxyPoolRunning();
    statusText_->SetLabel(
        "代理池状态: " + std::string(running ? "运行中" : "未运行") +
        "  成员数: " + std::to_string(currentMembers_.size()));
    startStopBtn_->SetLabel(running ? "停止池" : "启动池");
}

void StandalonePoolDialog::onStartStop(wxCommandEvent& event) {
    if (!controller_) return;
    if (controller_->isProxyPoolRunning()) {
        controller_->stopProxyPool();
    } else {
        if (!controller_->startProxyPool()) {
            wxMessageBox("启动代理池失败：无法分配可用端口或 xray 启动失败。",
                         "代理池", wxOK | wxICON_WARNING);
        }
    }
    updateStatusText();
    setMembers(controller_->getPoolMembers());
}

void StandalonePoolDialog::onDelete(wxCommandEvent& event) {
    if (!controller_) return;
    long sel = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0) return;
    std::size_t idx = static_cast<std::size_t>(sel);
    if (idx >= currentMembers_.size()) return;
    int indexId = currentMembers_[idx].indexId;
    controller_->removePoolMember(indexId, true);
    setMembers(controller_->getPoolMembers());
}

void StandalonePoolDialog::onRefresh(wxCommandEvent& event) {
    if (!controller_) return;
    setMembers(controller_->getPoolMembers());
}

void StandalonePoolDialog::onToggleReport(wxCommandEvent& event) {
    if (controller_) controller_->setPoolReportHealth(reportChk_->GetValue());
}

void StandalonePoolDialog::onTogglePrune(wxCommandEvent& event) {
    if (controller_) controller_->setPoolAutoPruneDead(pruneChk_->GetValue());
}

void StandalonePoolDialog::onToggleOptimize(wxCommandEvent& event) {
    if (controller_) controller_->setPoolAutoOptimize(optimizeChk_->GetValue());
}

void StandalonePoolDialog::onAdd(wxCommandEvent& event) {
    if (!controller_) return;
    if (!controller_->isProxyPoolRunning()) {
        if (!controller_->startProxyPool()) {
            wxMessageBox("代理池启动失败，无法添加代理。请确认 xray 可执行文件已配置且端口可用。",
                         "代理池", wxOK | wxICON_WARNING);
            return;
        }
        updateStatusText();
    }
    AddPoolMemberDialog picker(this, controller_);
    if (picker.ShowModal() != wxID_OK) return;
    const std::vector<std::string>& chosen = picker.getSelectedIndexIds();
    if (chosen.empty()) return;
    int added = 0;
    for (const auto& idx : chosen) {
        if (controller_->injectProxyToPool(idx)) ++added;
    }
    setMembers(controller_->getPoolMembers());
    if (added > 0) {
        wxMessageBox(wxString::Format("已添加 %d 个代理到代理池。", added),
                     "代理池", wxOK | wxICON_INFORMATION);
    } else {
        wxMessageBox("没有代理被成功加入代理池。", "代理池", wxOK | wxICON_WARNING);
    }
}
