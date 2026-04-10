#ifndef XRAY_API_H
#define XRAY_API_H

#include <string>
#include <vector>

namespace xray {

class XrayApi {
public:
    XrayApi(const std::string& xrayPath, const std::string& serverAddr);
    bool addOutbound(const std::string& outboundJson, const std::string& tag, std::string& resultOutput);
    bool removeOutbound(const std::string& tag);
    bool listOutbounds();
    bool ping(std::string& resultOutput);
    std::string getLastError() const;

private:
    std::string xrayPath_;
    std::string serverAddr_;
    std::string lastError_;
    bool runCommand(const std::string& args, std::string& output);
};

} // namespace xray

#endif // XRAY_API_H
