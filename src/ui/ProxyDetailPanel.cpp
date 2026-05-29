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
    
    // Create labels for all 33 ProfileItem fields
    for (int i = 0; i < 33; ++i) {
        fieldLabels_[i] = new wxStaticText(this, wxID_ANY, std::string(allFields_[i]) + ": -");
        mainSizer_->Add(fieldLabels_[i], 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);
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
        fieldLabels_[0]->SetLabel("ConfigType: " + proxy->configtype);
        fieldLabels_[1]->SetLabel("ConfigVersion: " + proxy->configversion);
        fieldLabels_[2]->SetLabel("Address: " + proxy->address);
        fieldLabels_[3]->SetLabel("Port: " + proxy->port);
        fieldLabels_[4]->SetLabel("Ports: " + proxy->ports);
        fieldLabels_[5]->SetLabel("Id: " + proxy->id);
        fieldLabels_[6]->SetLabel("AlterId: " + proxy->alterid);
        fieldLabels_[7]->SetLabel("Security: " + proxy->security);
        fieldLabels_[8]->SetLabel("Network: " + proxy->network);
        fieldLabels_[9]->SetLabel("Remarks: " + proxy->remarks);
        fieldLabels_[10]->SetLabel("HeaderType: " + proxy->headertype);
        fieldLabels_[11]->SetLabel("RequestHost: " + proxy->requesthost);
        fieldLabels_[12]->SetLabel("Path: " + proxy->path);
        fieldLabels_[13]->SetLabel("StreamSecurity: " + proxy->streamsecurity);
        fieldLabels_[14]->SetLabel("AllowInsecure: " + proxy->allowinsecure);
        fieldLabels_[15]->SetLabel("Subid: " + proxy->subid);
        fieldLabels_[16]->SetLabel("IsSub: " + proxy->issub);
        fieldLabels_[17]->SetLabel("Flow: " + proxy->flow);
        fieldLabels_[18]->SetLabel("Sni: " + proxy->sni);
        fieldLabels_[19]->SetLabel("Alpn: " + proxy->alpn);
        fieldLabels_[20]->SetLabel("CoreType: " + proxy->coretype);
        fieldLabels_[21]->SetLabel("PreSocksPort: " + proxy->presocksport);
        fieldLabels_[22]->SetLabel("Fingerprint: " + proxy->fingerprint);
        fieldLabels_[23]->SetLabel("DisplayLog: " + proxy->displaylog);
        fieldLabels_[24]->SetLabel("PublicKey: " + proxy->publickey);
        fieldLabels_[25]->SetLabel("ShortId: " + proxy->shortid);
        fieldLabels_[26]->SetLabel("SpiderX: " + proxy->spiderx);
        fieldLabels_[27]->SetLabel("Mldsa65Verify: " + proxy->mldsa65verify);
        fieldLabels_[28]->SetLabel("Extra: " + proxy->extra);
        fieldLabels_[29]->SetLabel("MuxEnabled: " + proxy->muxenabled);
        fieldLabels_[30]->SetLabel("Cert: " + proxy->cert);
        fieldLabels_[31]->SetLabel("CertSha: " + proxy->certsha);
        fieldLabels_[32]->SetLabel("EchConfigList: " + proxy->echconfiglist);
    } else {
        // Reset all to "-"
        for (int i = 0; i < 33; ++i) {
            fieldLabels_[i]->SetLabel(std::string(allFields_[i]) + ": -");
        }
    }
    
    Refresh();
}