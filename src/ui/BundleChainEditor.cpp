#include "BundleChainEditor.h"
#include "BundleSelectorDialog.h"
#include "config/BundleChainResolver.h"
#include "config/ComponentBundle.h"
#include "config/ComponentBundleDAO.h"
#include <boost/json.hpp>

// ============================================================
// BundleChainEditor: 编辑 BundleChain（Bundle 列表 + 参数 JSON）
// 设计文档 §8.1/§8.2 —— 解析时经 BundleChainResolver 生成组件
// 与冲突，供外层 ComponentListPanel 显示。
// ============================================================

wxBEGIN_EVENT_TABLE(BundleChainEditor, wxPanel)
    EVT_BUTTON(wxID_ADD, BundleChainEditor::onAddBundle)
    EVT_BUTTON(wxID_REMOVE, BundleChainEditor::onRemoveBundle)
    EVT_BUTTON(wxID_EXECUTE, BundleChainEditor::onResolve)
    EVT_LIST_ITEM_SELECTED(wxID_ANY, BundleChainEditor::onBundleSelected)
wxEND_EVENT_TABLE()

BundleChainEditor::BundleChainEditor(wxWindow* parent, sqlite3* db)
    : wxPanel(parent, wxID_ANY), db_(db) {

    addBtn_ = new wxButton(this, wxID_ADD, wxT("添加 Bundle"));
    removeBtn_ = new wxButton(this, wxID_REMOVE, wxT("移除"));
    resolveBtn_ = new wxButton(this, wxID_EXECUTE, wxT("解析"));

    bundleList_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(520, 200),
                                 wxLC_REPORT | wxLC_SINGLE_SEL);
    bundleList_->InsertColumn(0, wxT("Bundle ID"), wxLIST_FORMAT_LEFT, 200);
    bundleList_->InsertColumn(1, wxT("名称"), wxLIST_FORMAT_LEFT, 160);
    bundleList_->InsertColumn(2, wxT("版本"), wxLIST_FORMAT_LEFT, 60);

    wxStaticText* paramLabel = new wxStaticText(this, wxID_ANY, wxT("参数 (JSON)"));
    paramEditor_ = new wxTextCtrl(this, wxID_ANY, wxT("{}"),
                                  wxDefaultPosition, wxSize(520, 120),
                                  wxTE_MULTILINE);

    wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    btnSizer->Add(addBtn_, 0, wxRIGHT, 6);
    btnSizer->Add(removeBtn_, 0, wxRIGHT, 6);
    btnSizer->Add(resolveBtn_, 0, 0, 0);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
    topSizer->Add(btnSizer, 0, wxALL, 6);
    topSizer->Add(bundleList_, 1, wxEXPAND | wxLEFT | wxRIGHT, 6);
    topSizer->Add(paramLabel, 0, wxTOP | wxLEFT | wxRIGHT, 8);
    topSizer->Add(paramEditor_, 0, wxEXPAND | wxALL, 6);
    SetSizer(topSizer);

    refreshBundleList();
}

void BundleChainEditor::setChain(const config::BundleChain& chain) {
    chain_ = chain;
    refreshBundleList();
    paramEditor_->ChangeValue(wxT("{}"));
    if (!chain_.params.empty()) {
        boost::json::object obj;
        for (const auto& kv : chain_.params) {
            obj[kv.first] = kv.second;
        }
        paramEditor_->ChangeValue(wxString(boost::json::serialize(obj)));
    }
}

config::BundleChain BundleChainEditor::getChain() const {
    config::BundleChain chain = chain_;
    // 从编辑器解析 JSON 参数（失败时保留原值）
    const std::string text = std::string(paramEditor_->GetValue().ToUTF8());
    boost::system::error_code ec;
    boost::json::value v = boost::json::parse(text, ec);
    if (!ec && v.is_object()) {
        chain.params.clear();
        for (const boost::json::key_value_pair& kv : v.as_object()) {
            chain.params[std::string(kv.key())] = kv.value();
        }
    }
    return chain;
}

void BundleChainEditor::clear() {
    chain_.bundleIds.clear();
    chain_.params.clear();
    refreshBundleList();
    paramEditor_->ChangeValue(wxT("{}"));
}

config::BundleChainResolveResult BundleChainEditor::resolve() const {
    config::BundleChainResolver resolver(db_);
    return resolver.resolveBundleChain(getChain());
}

void BundleChainEditor::refreshBundleList() {
    bundleList_->DeleteAllItems();
    config::ComponentBundleDAO dao(db_);
    // 逐个查询已选 bundle 显示元信息
    long row = 0;
    for (const std::string& bundleId : chain_.bundleIds) {
        std::optional<config::ComponentBundle> b = dao.getById(bundleId);
        std::string name = bundleId;
        int version = -1;
        if (b.has_value() && b->version > 0) {
            name = b->name;
            version = b->version;
        }
        bundleList_->InsertItem(row, wxString(bundleId));
        bundleList_->SetItem(row, 1, wxString(name));
        bundleList_->SetItem(row, 2, version > 0 ? wxString(std::to_string(version)) : wxT("-"));
        ++row;
    }
}

void BundleChainEditor::onAddBundle(wxCommandEvent& event) {
    (void)event;
    std::string id = BundleSelectorDialog::SelectBundle(this, db_, coreType_);
    if (id.empty()) {
        return;
    }
    for (const std::string& existing : chain_.bundleIds) {
        if (existing == id) {
            return;
        }
    }
    chain_.bundleIds.push_back(id);
    refreshBundleList();
}

void BundleChainEditor::onRemoveBundle(wxCommandEvent& event) {
    (void)event;
    long sel = bundleList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || sel >= static_cast<long>(chain_.bundleIds.size())) {
        return;
    }
    chain_.bundleIds.erase(chain_.bundleIds.begin() + sel);
    refreshBundleList();
}

void BundleChainEditor::onResolve(wxCommandEvent& event) {
    (void)event;
    wxBusyCursor busy;
    const config::BundleChainResolveResult result = resolve();
    if (onResolved) {
        onResolved(result);
    }
}

void BundleChainEditor::onBundleSelected(wxListEvent& event) {
    (void)event;
}