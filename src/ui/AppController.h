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
  void updateSubscriptionAsync(const std::string& subId, wxEvtHandler* wxHandler);
  void updateAllSubscriptionsAsync(wxEvtHandler* wxHandler);
  bool importSubscription(const std::string& url);

  // Proxies
  std::vector<db::models::Profileitem> loadProxies(const std::string& subId = "");
  std::vector<db::models::ProfileExItem> loadProxyResults();

  // Tests
  void testSubscriptionAsync(const std::string& subId, wxEvtHandler* wxHandler);
  void cancelTest() { cancelRequested_ = true; }
  bool isTestCancelled() const { return cancelRequested_.load(); }

  // Find (async)
  ProxyFinder::TestResult findFirstProxy();
  ProxyFinder::TestResult findBestProxy();
  void findFirstProxyAsync(wxEvtHandler* wxHandler);
  void findBestProxyAsync(wxEvtHandler* wxHandler);

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
