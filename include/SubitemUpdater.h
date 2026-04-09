#ifndef SUBITEM_UPDATER_H
#define SUBITEM_UPDATER_H

#include <string>
#include <vector>
#include <set>
#include <sqlite3.h>
#include <curl/curl.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <ctime>
#include <thread>
#include <functional>
#include <memory>

#include "Subitem.h"
#include "Profileitem.h"
#include "Profileexitem.h"
#include "ConfigGenerator.h"
#include "XrayApi.h"

class SubitemUpdater {
public:
    explicit SubitemUpdater(sqlite3* db, std::ofstream* logOut = nullptr,
                           const std::string& xrayPath = "",
                           int xrayApiPort = 10086,
                           int testTimeoutMs = 3000);

    bool run();
    bool runSingle(const std::string& subId);
    bool runSingleWithProxy(const std::string& subId, int socksPort);

private:
    struct FallbackProxy {
        int socksPort;
        int delay;
        std::string indexId;
    };
    
    sqlite3* db_;
    std::ofstream* logOut_;
    std::string fallbackSubId_;
    std::string xrayPath_;
    int xrayApiPort_;
    int test_timeout_ms_;

    void log(const std::string& msg);
    std::string fetchUrl(const std::string& url);
    std::string fetchUrlViaProxy(const std::string& url, int socksPort);
    std::vector<db::models::Profileitem> parseSubscription(const std::string& content, const std::string& subid);
    bool updateProfileItems(const std::string& subid, const std::vector<db::models::Profileitem>& profiles);
    int findBestFallbackProxy();
    std::string decodeBase64(const std::string& input);
    std::string urlDecode(const std::string& input);
    
    std::vector<FallbackProxy> getAllFallbackProxies(int maxCount);
    bool startXrayForSubscription();
    bool testSubscriptionViaXray(int socksPort, const std::string& subUrl);
    void cleanupXray();
};

#endif // SUBITEM_UPDATER_H
