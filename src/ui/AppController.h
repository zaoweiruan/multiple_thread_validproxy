#ifndef UI_APP_CONTROLLER_H
#define UI_APP_CONTROLLER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <tuple>
#include <atomic>
#include <thread>
#include <memory>
#include <sqlite3.h>
#include <wx/event.h>

#include "ConfigReader.h"
#include "Subitem.h"
#include "Profileitem.h"
#include "ProfileExItem.h"
#include "ProxyFinder.h"

class wxEvtHandler;

class AppController {
public:
  AppController(sqlite3* db, const config::AppConfig& cfg);
  ~AppController();

  // Config
  config::AppConfig getConfig() const { return config_; }
  bool saveConfig(const config::AppConfig& cfg);

  // Database switching (close old + open new)
  sqlite3* switchDatabase(const std::string& newPath);

// Subscriptions
   std::vector<db::models::Subitem> loadSubscriptions();
   void loadSubscriptionsAsync(wxEvtHandler* handler);
   bool updateSubscriptionEnabled(const std::string& id, bool enabled);
bool updateSubitem(const db::models::Subitem& sub);
    bool deleteSubscription(const std::string& subId);
    void updateSubscriptionAsync(const std::string& subId, wxEvtHandler* wxHandler);
  void updateAllSubscriptionsAsync(wxEvtHandler* wxHandler);
  bool importSubscription(const std::string& url);

// Proxies
std::vector<db::models::Profileitem> loadProxies(const std::string& subId = "");
void loadProxiesAsync(const std::string& subId, wxEvtHandler* handler);
std::unordered_map<std::string, int> countProxiesBySubId();
std::optional<db::models::Profileitem> getProxyByIndexId(const std::string& indexId);
std::vector<db::models::ProfileExItem> loadProxyResults();

// ---------------------------------------------------------------
// Testing / Cancellation
// ---------------------------------------------------------------
void testSubscriptionAsync(const std::string& subId, wxEvtHandler* wxHandler);
void testSingleProxyAsync(const std::string& indexId, wxEvtHandler* wxHandler);
void testAllProxiesAsync(wxEvtHandler* wxHandler);
void cancelTest();
bool isTestCancelled() const;

// Find (async)
   ProxyFinder::TestResult findFirstProxy();
   ProxyFinder::TestResult findBestProxy();
   void findFirstProxyAsync(wxEvtHandler* wxHandler);
   void findBestProxyAsync(wxEvtHandler* wxHandler);
   void findProxyByIndexIdAsync(const std::string& indexId, wxEvtHandler* wxHandler);
   void syncDatabasesAsync(wxEvtHandler* wxHandler);

  // Export / Tool
  std::tuple<bool, int, std::string> exportShareLinks();
  bool deduplicate();
  bool syncDatabases(const std::string& src = "", const std::string& dst = "");
  bool generateConfig(const std::string& indexId);
  void stopXray();

private:
  void doUpdateSubscription(const std::string& subId, wxEvtHandler* wxHandler);
  void doUpdateAllSubscriptions(wxEvtHandler* wxHandler);
  void doTestSubscription(const std::string& subId, wxEvtHandler* wxHandler);
  void doTestSingleProxy(const std::string& indexId, wxEvtHandler* wxHandler);
  void doTestAllProxies(wxEvtHandler* wxHandler);
  void doFindFirstProxy(wxEvtHandler* wxHandler);
  void doFindBestProxy(wxEvtHandler* wxHandler);
  void doSyncDatabases(wxEvtHandler* wxHandler);

  sqlite3* db_;
  config::AppConfig config_;
  std::atomic<bool> cancelRequested_{false};
  std::atomic<bool> isRunning_{false};
  // Worker thread (single at a time)
  std::thread workerThread_;
  ProxyFinder::TestResult lastFindResult_;
};

#endif // UI_APP_CONTROLLER_H
