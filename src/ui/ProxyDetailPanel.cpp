#include "ProxyDetailPanel.h"

ProxyDetailPanel::ProxyDetailPanel(wxWindow* parent)
    : wxScrolled<wxPanel>(parent, wxID_ANY) {
    
    // Enable scrolling
    SetScrollRate(5, 5);
    
    mainSizer_ = new wxBoxSizer(wxVERTICAL);
    
    wxStaticText* title = new wxStaticText(this, wxID_ANY, "Proxy Details");
    wxFont titleFont = title->GetFont();
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(titleFont);
    
    mainSizer_->Add(title, 0, wxLEFT | wxTOP | wxRIGHT, 10);
    mainSizer_->AddSpacer(10);
    
    // Create labels for all 33 ProfileItem fields (read-only text for select/copy, no border)
    for (int i = 0; i < 33; ++i) {
        fieldLabels_[i] = new wxTextCtrl(this, wxID_ANY, std::string(allFields_[i]) + ": -",
                                         wxDefaultPosition, wxDefaultSize,
                                         wxTE_READONLY | wxBORDER_NONE);
        fieldLabels_[i]->SetBackgroundColour(GetBackgroundColour());
        fieldLabels_[i]->SetMinSize(wxSize(-1, 22));
        mainSizer_->Add(fieldLabels_[i], 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 2);
    }
    
    SetSizer(mainSizer_);
    Layout();
    FitInside();
}

ProxyDetailPanel::~ProxyDetailPanel() = default;

void ProxyDetailPanel::UpdateDetail(const std::string& indexId, const std::string& host, const std::string& port,
                                    const std::string& delay, const std::string& message,
                                    int failures, const std::string& remarks,
                                    const db::models::Profileitem* proxy) {
    (void)indexId;
    (void)host;
    (void)port;
    (void)delay;
    (void)message;
    (void)failures;
    (void)remarks;
    
    if (proxy) {
        // Update all 33 fields from proxy
        fieldLabels_[0]->SetValue("ConfigType: " + proxy->configtype);
        fieldLabels_[1]->SetValue("ConfigVersion: " + proxy->configversion);
        fieldLabels_[2]->SetValue("Address: " + proxy->address);
        fieldLabels_[3]->SetValue("Port: " + proxy->port);
        fieldLabels_[4]->SetValue("Ports: " + proxy->ports);
        fieldLabels_[5]->SetValue("Id: " + proxy->id);
        fieldLabels_[6]->SetValue("AlterId: " + proxy->alterid);
        fieldLabels_[7]->SetValue("Security: " + proxy->security);
        fieldLabels_[8]->SetValue("Network: " + proxy->network);
        fieldLabels_[9]->SetValue("Remarks: " + proxy->remarks);
        fieldLabels_[10]->SetValue("HeaderType: " + proxy->headertype);
        fieldLabels_[11]->SetValue("RequestHost: " + proxy->requesthost);
        fieldLabels_[12]->SetValue("Path: " + proxy->path);
        fieldLabels_[13]->SetValue("StreamSecurity: " + proxy->streamsecurity);
        fieldLabels_[14]->SetValue("AllowInsecure: " + proxy->allowinsecure);
        fieldLabels_[15]->SetValue("Subid: " + proxy->subid);
        fieldLabels_[16]->SetValue("IsSub: " + proxy->issub);
        fieldLabels_[17]->SetValue("Flow: " + proxy->flow);
        fieldLabels_[18]->SetValue("Sni: " + proxy->sni);
        fieldLabels_[19]->SetValue("Alpn: " + proxy->alpn);
        fieldLabels_[20]->SetValue("CoreType: " + proxy->coretype);
        fieldLabels_[21]->SetValue("PreSocksPort: " + proxy->presocksport);
        fieldLabels_[22]->SetValue("Fingerprint: " + proxy->fingerprint);
        fieldLabels_[23]->SetValue("DisplayLog: " + proxy->displaylog);
        fieldLabels_[24]->SetValue("PublicKey: " + proxy->publickey);
        fieldLabels_[25]->SetValue("ShortId: " + proxy->shortid);
        fieldLabels_[26]->SetValue("SpiderX: " + proxy->spiderx);
        fieldLabels_[27]->SetValue("Mldsa65Verify: " + proxy->mldsa65verify);
        fieldLabels_[28]->SetValue("Extra: " + proxy->extra);
        fieldLabels_[29]->SetValue("MuxEnabled: " + proxy->muxenabled);
        fieldLabels_[30]->SetValue("Cert: " + proxy->cert);
        fieldLabels_[31]->SetValue("CertSha: " + proxy->certsha);
        fieldLabels_[32]->SetValue("EchConfigList: " + proxy->echconfiglist);
    } else {
        // Reset all to "-"
        for (int i = 0; i < 33; ++i) {
            fieldLabels_[i]->SetValue(std::string(allFields_[i]) + ": -");
        }
    }
    
    Refresh();
}