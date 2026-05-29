#ifndef UI_PROXY_DETAIL_PANEL_H
#define UI_PROXY_DETAIL_PANEL_H

#include <wx/wx.h>
#include <wx/stattext.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/scrolwin.h>
#include <wx/statbox.h>

#include <string>
#include <array>
#include "Profileitem.h"

class ProxyDetailPanel : public wxScrolled<wxPanel> {
public:
    explicit ProxyDetailPanel(wxWindow* parent);
    ~ProxyDetailPanel() override;

    void UpdateDetail(const std::string& indexId, 
                      const std::string& host, const std::string& port,
                      const std::string& delay, const std::string& message,
                      int failures, const std::string& remarks,
                      const db::models::Profileitem* proxy = nullptr);

private:
    // Store labels for all ProfileItem fields (33 fields)
    wxStaticText* fieldLabels_[33];
    
    // Field names for labels
    const std::array<const char*, 33> allFields_ = {
        "ConfigType", "ConfigVersion", "Address", "Port", "Ports", "Id", 
        "AlterId", "Security", "Network", "Remarks", "HeaderType", "RequestHost", 
        "Path", "StreamSecurity", "AllowInsecure", "Subid", "IsSub", "Flow", 
        "Sni", "Alpn", "CoreType", "PreSocksPort", "Fingerprint", "DisplayLog", 
        "PublicKey", "ShortId", "SpiderX", "Mldsa65Verify", "Extra", "MuxEnabled", 
        "Cert", "CertSha", "EchConfigList"
    };

    wxBoxSizer* mainSizer_;
};

#endif // UI_PROXY_DETAIL_PANEL_H