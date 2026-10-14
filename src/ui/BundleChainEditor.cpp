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

BundleChainEditor::BundleChainEditor(wxWindow* parent, sqlite3* db, bool showButtons)
    : wxPanel(parent, wxID_ANY), db_(db) {

    // Bundle 列表占满本控件：参数 (JSON) 输入框已废除（模板不该存具体参数值，
    // 见下方 resolveToConfig 的占位符说明），本控件现在只有「按钮 + 列表」。
    bundleList_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(-1, -1),
                                 wxLC_REPORT | wxLC_SINGLE_SEL);
    bundleList_->InsertColumn(0, wxT("Bundle ID"), wxLIST_FORMAT_LEFT, 130);
    bundleList_->InsertColumn(1, wxT("名称"), wxLIST_FORMAT_LEFT, 100);
    bundleList_->InsertColumn(2, wxT("版本"), wxLIST_FORMAT_LEFT, 50);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    if (showButtons) {
        addBtn_ = new wxButton(this, wxID_ADD, wxT("添加 Bundle"));
        removeBtn_ = new wxButton(this, wxID_REMOVE, wxT("移除"));
        resolveBtn_ = new wxButton(this, wxID_EXECUTE, wxT("解析"));

        wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
        btnSizer->Add(addBtn_, 0, wxRIGHT, 6);
        btnSizer->Add(removeBtn_, 0, wxRIGHT, 6);
        btnSizer->Add(resolveBtn_, 0, 0, 0);
        topSizer->Add(btnSizer, 0, wxALL, 6);
    }
    topSizer->Add(bundleList_, 1, wxEXPAND | wxALL, 6);
    SetSizer(topSizer);

    refreshBundleList();
}

void BundleChainEditor::triggerAddBundle() {
    wxCommandEvent e(wxID_ANY);
    onAddBundle(e);
}

void BundleChainEditor::triggerRemoveBundle() {
    wxCommandEvent e(wxID_ANY);
    onRemoveBundle(e);
}

void BundleChainEditor::triggerResolve() {
    wxCommandEvent e(wxID_ANY);
    onResolve(e);
}

void BundleChainEditor::setChain(const config::BundleChain& chain) {
    chain_ = chain;
    refreshBundleList();
}

config::BundleChain BundleChainEditor::getChain() const {
    // params 字段已废除：模板不应存具体参数值（占位符由运行期上下文填值）。
    // 保留 chain_.bundleIds 即可。
    config::BundleChain chain = chain_;
    chain.params.clear();
    return chain;
}

void BundleChainEditor::clear() {
    chain_.bundleIds.clear();
    chain_.params.clear();
    refreshBundleList();
}

config::BundleChainResolveResult BundleChainEditor::resolve() const {
    config::BundleChainResolver resolver(db_);
    return resolver.resolveBundleChain(getChain());
}

config::BundleChainResolveResult BundleChainEditor::resolveToConfig() const {
    config::BundleChainResolver resolver(db_);
    // 预览用上下文：按当前模板核心类型选择后端，无 profile（占位符取
    // chain.params / 默认值），db 指向当前连接供组件解析使用。
    config::PipelineContext ctx;
    ctx.proxyType = (coreType_ == config::CoreType::SINGBOX)
                        ? config::ProxyType::SingBox
                        : config::ProxyType::Xray;
    ctx.hasProfile = false;
    ctx.vars = config::ConfigVariables();
    ctx.db = db_;
    ctx.clientName = "chain-preview";
    // 预览保留 ${...} 占位符原文：模板不存具体参数值（值由运行期上下文提供），
    // 预览的用途是让人看出生成结果的结构与「运行期还需哪些参数」。
    // 真实生成路径（ConfigGenerator）不做此设置，仍会替换成实际值。
    ctx.substitutePlaceholders = false;
    return resolver.resolveToConfig(getChain(), ctx);
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
    // 解析并生成最终配置（result.config 供 UI 预览）
    const config::BundleChainResolveResult result = resolveToConfig();
    if (onResolved) {
        onResolved(result);
    }
}

void BundleChainEditor::onBundleSelected(wxListEvent& event) {
    (void)event;
}