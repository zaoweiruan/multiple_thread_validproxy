#ifndef UI_APP_CONTROLLER_H
#define UI_APP_CONTROLLER_H

#include <string>
#include <vector>
#include <atomic>
#include <thread>
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

// Subscriptions
   std::vector<db::models::Subitem> loadSubscriptions();
   bool updateSubscriptionEnabled(const std::string& id, bool enabled);
   bool updateSubitem(const db::models::Subitem& sub);
   void updateSubscriptionAsync(const std::string& subId, wxEvtHandler* wxHandler);
  void updateAllSubscriptionsAsync(wxEvtHandler* wxHandler);
  bool importSubscription(const std::string& url);

// Proxies
std::vector<db::models::Profileitem> loadProxies(const std::string& subId = "");
std::optional<db::models::Profileitem> getProxyByIndexId(const std::string& indexId);
std::vector<db::models::ProfileExItem> loadProxyResults();

// ---------------------------------------------------------------
// Testing / Cancellation
// ---------------------------------------------------------------
void testSubscriptionAsync(const std::string& subId, wxEvtHandler* wxHandler);
void testSingleProxyAsync(const std::string& indexId, wxEvtHandler* wxHandler);
void cancelTest();
bool isTestCancelled() const;

// Find (async)
   ProxyFinder::TestResult findFirstProxy();
   ProxyFinder::TestResult findBestProxy();
   void findFirstProxyAsync(wxEvtHandler* wxHandler);
   void findBestProxyAsync(wxEvtHandler* wxHandler);
   void findProxyByIndexIdAsync(const std::string& indexId, wxEvtHandler* wxHandler);

  // Export / Tool
  bool exportShareLinks();
  bool deduplicate();
  bool syncDatabases(const std::string& src = "", const std::string& dst = "");
  bool generateConfig(const std::string& indexId);
  void stopXray();

private:
  void doUpdateSubscription(const std::string& subId, wxEvtHandler* wxHandler);
  void doUpdateAllSubscriptions(wxEvtHandler* wxHandler);
  void doTestSubscription(const std::string& subId, wxEvtHandler* wxHandler);
  void doTestSingleProxy(const std::string& indexId, wxEvtHandler* wxHandler);
  void doFindFirstProxy(wxEvtHandler* wxHandler);
  void doFindBestProxy(wxEvtHandler* wxHandler);

  sqlite3* db_;
  config::AppConfig config_;
  std::atomic<bool> cancelRequested_{false};
  // Worker thread (single at a time)
  std::thread workerThread_;
  ProxyFinder::TestResult lastFindResult_;
};

#endif // UI_APP_CONTROLLER_H
