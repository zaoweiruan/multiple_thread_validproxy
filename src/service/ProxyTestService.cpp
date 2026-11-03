#include "service/ProxyTestService.h"
#include "ProxyBatchTester.h"
#include "Profileitem.h"
#include "ProxyFinder.h"
#include "ConfigGenerator.h"
#include "Utils.h"
#include "XrayManager.h"
#include "Logger.h"

namespace service {

#include "NetworkMonitor.h"

ProxyTestService::ProxyTestService(sqlite3* db, const config::AppConfig& config, const ::NetworkMonitor* netMon)
    : db_(db), config_(config), netMon_(netMon) {
}

bool ProxyTestService::runSubscription(const std::string& subId, std::atomic<bool>* cancel) {
    ProxyBatchTester tester(db_, config_, "", cancel, netMon_);
    return tester.runWithSubId(subId);
}

bool ProxyTestService::runSingleProxy(const std::string& indexId, std::atomic<bool>* cancel) {
    ProxyBatchTester tester(db_, config_, "", cancel, netMon_);
    bool ok = tester.runWithIndexId(indexId);
    lastResult_ = tester.getLastResult();
    return ok;
}

bool ProxyTestService::runAllProxies(std::atomic<bool>* cancel) {
    ProxyBatchTester tester(db_, config_, "", cancel, netMon_);
    bool ok = tester.run();
    lastTotalProxies_ = tester.getTotalProxies();
    return ok;
}

TestResult ProxyTestService::findFirstProxy(std::atomic<bool>* cancel) {
    std::string xrayPath = config_.proxy.xray_executable;
    std::string configDir = utils::getExecutableDir() + "/config";
    XrayManager* manager = XrayManager::getInstance(xrayPath, configDir, config_.xray_workers, db_);
    ProxyFinder finder(db_, manager, xrayPath, config_.test_url, "", config_.test_timeout_ms);
    finder.findFirstWorkingProxy();
    return finder.getLastResult();
}

TestResult ProxyTestService::findBestProxy(std::atomic<bool>* cancel) {
    std::string xrayPath = config_.proxy.xray_executable;
    std::string configDir = utils::getExecutableDir() + "/config";
    XrayManager* manager = XrayManager::getInstance(xrayPath, configDir, config_.xray_workers, db_);
    ProxyFinder finder(db_, manager, xrayPath, config_.test_url, "", config_.test_timeout_ms);
    finder.findWorkingProxy();
    return finder.getLastResult();
}

} // namespace service