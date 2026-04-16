#ifndef SUBITEM_UPDATER_V2_H
#define SUBITEM_UPDATER_V2_H

#include <string>
#include <vector>
#include <sqlite3.h>
#include <fstream>
#include <iostream>
#include <optional>

#include "Subitem.h"
#include "Profileitem.h"
#include "Profileexitem.h"
#include "XrayManager.h"
#include "ProxyFinder.h"
#include "ConfigReader.h"

namespace update {

class SubitemUpdaterV2 {
public:
    SubitemUpdaterV2(sqlite3* db,
                    const std::string& xrayPath,
                    const config::AppConfig& config,
                    std::ofstream* logOut = nullptr,
                    const std::string& baseDir = "");

    ~SubitemUpdaterV2() {
        cleanupXray();
    }

    bool run();
    bool runSingle(const std::string& subId);
    bool runSingleWithProxy(const std::string& subId, int socksPort);

private:
    enum class Strategy {
        DirectFirst,
        ProxyFirst,
        DirectOnly
    };

    bool updateWithStrategy(const std::string& subUrl, const std::string& subId, Strategy strategy);
    std::string fetchUrl(const std::string& url);
    std::string fetchUrlViaProxy(const std::string& url, int socksPort);

    std::vector<db::models::Profileitem> parseSubscription(const std::string& content, const std::string& subid);
    bool updateProfileItems(const std::string& subid, const std::vector<db::models::Profileitem>& profiles);

    std::pair<int, int> getProxyPorts(const std::string& targetUrl = "");
    void releaseProxyPorts();

    bool startXray(const std::string& indexId, int socksPort, int apiPort);
    void cleanupXray();

    Strategy parseStrategy(const std::string& mode);
    std::string getCurrentTimestamp();
    void log(const std::string& msg);
    std::string decodeBase64(const std::string& input);
    std::string urlDecode(const std::string& input);

    sqlite3* db_;
    std::string xrayPath_;
    config::AppConfig config_;
    std::ofstream* logOut_;
    std::string baseDir_;

    XrayManager* xrayMgr_;
    ProxyFinder* proxyFinder_;

    int xrayProcessId_;
    HANDLE xrayJob_;
};

} // namespace update

#endif // SUBITEM_UPDATER_V2_H