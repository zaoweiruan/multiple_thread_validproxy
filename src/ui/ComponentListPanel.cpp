#include "ComponentListPanel.h"

#include <wx/colour.h>
#include <wx/sizer.h>

#include <string>

using config::CombinationStrategy;
using config::ComponentRegistry;
using config::ComponentMetadata;

ComponentListPanel::ComponentListPanel(wxWindow* parent, sqlite3* db)
    : wxPanel(parent, wxID_ANY),
      list_(nullptr) {
    (void)db;

    list_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxLC_REPORT | wxLC_SINGLE_SEL);

    list_->InsertColumn(0, "#", wxLIST_FORMAT_LEFT, 40);
    list_->InsertColumn(1, "组件类型", wxLIST_FORMAT_LEFT, 140);
    list_->InsertColumn(2, "配置节", wxLIST_FORMAT_LEFT, 120);
    list_->InsertColumn(3, "组合策略", wxLIST_FORMAT_LEFT, 90);
    list_->InsertColumn(4, "来源 Bundle", wxLIST_FORMAT_LEFT, 160);
    list_->InsertColumn(5, "状态", wxLIST_FORMAT_LEFT, 90);

    wxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(list_, 1, wxEXPAND | wxALL, 4);
    SetSizer(sizer);
}

void ComponentListPanel::setComponents(
    const std::vector<config::ComponentConfig>& components,
    const std::vector<config::ComponentSource>& sources) {
    components_ = components;
    sources_ = sources;
    rebuild();
}

void ComponentListPanel::setConflictStatus(
    const std::vector<config::Conflict>& conflicts) {
    conflicts_ = conflicts;
    rebuild();
}

void ComponentListPanel::clear() {
    components_.clear();
    sources_.clear();
    conflicts_.clear();
    if (list_ != nullptr) {
        list_->DeleteAllItems();
    }
}

bool ComponentListPanel::hasErrors(
    const std::vector<config::Conflict>& conflicts) {
    for (const config::Conflict& c : conflicts) {
        if (c.severity == config::ConflictSeverity::ERROR) {
            return true;
        }
    }
    return false;
}

void ComponentListPanel::rebuild() {
    if (list_ == nullptr) {
        return;
    }
    list_->DeleteAllItems();

    ComponentRegistry& registry = ComponentRegistry::instance();

    for (size_t i = 0; i < components_.size(); ++i) {
        const config::ComponentConfig& comp = components_[i];
        const ComponentMetadata* meta = registry.getMetadata(comp.type);

        std::string typeName = "unknown";
        std::string section;
        std::string strategy;
        if (meta != nullptr) {
            typeName = meta->configSection.empty()
                           ? "unknown"
                           : meta->configSection;
            section = meta->configSection;
            if (meta->strategy == CombinationStrategy::OVERRIDE) {
                strategy = "OVERRIDE";
            } else if (meta->strategy == CombinationStrategy::MERGE) {
                strategy = "MERGE";
            } else {
                strategy = "APPEND";
            }
        } else {
            strategy = "-";
        }

        std::string sourceId;
        if (i < sources_.size()) {
            sourceId = sources_[i].sourceBundleId;
        }

        long row = list_->InsertItem(static_cast<long>(i),
                                     wxString::FromUTF8(std::to_string(i)));
        list_->SetItem(row, 1, wxString::FromUTF8(typeName));
        list_->SetItem(row, 2, wxString::FromUTF8(section));
        list_->SetItem(row, 3, wxString::FromUTF8(strategy));
        list_->SetItem(row, 4, wxString::FromUTF8(sourceId));

        // 冲突高亮（设计文档 §8.2）：按组件类型名匹配 conflict.component1
        bool hasError = false;
        bool hasWarn = false;
        for (const config::Conflict& c : conflicts_) {
            if (c.component1 != typeName) {
                continue;
            }
            if (c.severity == config::ConflictSeverity::ERROR) {
                hasError = true;
            } else {
                hasWarn = true;
            }
        }

        if (hasError) {
            list_->SetItem(row, 5, "错误");
            list_->SetItemBackgroundColour(row, wxColour(255, 200, 200));
        } else if (hasWarn) {
            list_->SetItem(row, 5, "警告");
            list_->SetItemBackgroundColour(row, wxColour(255, 240, 200));
        } else {
            list_->SetItem(row, 5, "✅");
        }
    }
}