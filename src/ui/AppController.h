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
#include <mutex>
#include <optional>
#include <windows.h>
#include <cstdint>

#include "ConfigReader.h"
#include "Subitem.h"
#include "Profileitem.h"
#include "ProfileExItem.h"
#include "ProxyFinder.h"
#include "NetworkMonitor.h"
#include "ProcessExitListener.h"
#include "ProxyRuntimeHistory.h"

class wxEvtHandler;
class wxWindow;

// ---------------------------------------------------------------
// StandaloneProxyInfo — tracks an independently launched Xray proxy
// ---------------------------------------------------------------
struct StandaloneProxyInfo {
    std::string indexId;
    std::string configPath;
    std::string configFileName;
    int socksPort = 0;
    HANDLE processHandle = nullptr;
    bool running = false;
    bool managed = true;
    int64_t runtimeHistoryId = -1;
    proc::ProcessExitListener::WatchKey watchKey = 0;
    std::string startedAt;
};

class AppController {
public:
  AppController(sqlite3* db, const config::AppConfig& cfg);
  ~AppController();

// Config
   config::AppConfig getConfig() const { return config_; }
   bool saveConfig(const config::AppConfig& cfg);
   bool isRunning() const { return isRunning_.load(); }

  // Database switching (close old + open new)
  sqlite3* switchDatabase(const std::string& newPath);

// Subscriptions
   std::vector<db::models::Subitem> loadSubscriptions();
   void loadSubscriptionsAsync(wxEvtHandler* handler);
   bool updateSubscriptionEnabled(const std::string& id, bool enabled);
  bool updateSubitem(const db::models::Subitem& sub);
    bool deleteSubscription(const std::string& subId);
    bool deleteProxiesBySubId(const std::string& subId);
    void updateSubscriptionAsync(const std::string& subId, wxEvtHandler* wxHandler);
  void updateAllSubscriptionsAsync(wxEvtHandler* wxHandler);
  bool importSubscription(const std::string& url);

// Proxies
std::vector<db::models::Profileitem> loadProxies(const std::string& subId = "");
void loadProxiesAsync(const std::string& subId, wxEvtHandler* handler);
std::unordered_map<std::string, int> countProxiesBySubId();
std::unordered_map<std::string, int> countValidProxiesBySubId();
std::optional<db::models::Profileitem> getProxyByIndexId(const std::string& indexId);
std::vector<db::models::ProfileExItem> loadProxyResults();

// Live elapsed time (ms) of in-progress standalone sessions, keyed by
// indexId.  Reads proxy_runtime_history rows whose ended_at IS NULL
// (heartbeat duration_ms).  Used by the UI to show the Runtime column as
// total_runtime_ms + live elapsed time so the evaluation refreshes
// meaningfully while a proxy is running.
std::unordered_map<std::string, long long> getRunningDurations();

// Async variant: reads running-session durations on a background thread
// (dedicated read-only SQLite connection, same pattern as loadProxiesAsync)
// and posts a RunningDurationsLoadedEvent back to the UI thread.  Keeps DB
// I/O off the UI thread so the periodic evaluation refresh cannot freeze
// the main window even on large databases.
void getRunningDurationsAsync(wxEvtHandler* handler);

// ---------------------------------------------------------------
// Testing / Cancellation
// ---------------------------------------------------------------
void testSubscriptionAsync(const std::string& subId, wxEvtHandler* wxHandler);
void testSingleProxyAsync(const std::string& indexId, wxEvtHandler* wxHandler);
void testAllProxiesAsync(wxEvtHandler* wxHandler);
void cancelTest();
bool isTestCancelled() const;

// Find (async)
   TestResult findFirstProxy();
   TestResult findBestProxy();
   void findFirstProxyAsync(wxEvtHandler* wxHandler);
   void findBestProxyAsync(wxEvtHandler* wxHandler);
   void findProxyByIndexIdAsync(const std::string& indexId, wxEvtHandler* wxHandler);
   void syncDatabasesAsync(wxEvtHandler* wxHandler);

    // AutoTask
   void runAutoTaskAsync(wxEvtHandler* wxHandler);
   void resumeAutoTaskAsync(wxEvtHandler* wxHandler);

// Export / Tool
    std::tuple<bool, int, std::string> exportShareLinks();
   bool deduplicate();
   bool syncDatabases(const std::string& src = "", const std::string& dst = "");
   bool generateConfig(const std::string& indexId);
    void stopXray();
    NetworkMonitor* getNetworkMonitor() { return &netMon_; }

    // Network monitor control
    void restartNetworkMonitor();

    // Standalone proxy management
    bool startStandaloneProxy(const std::string& indexId, int overridePort = 0);
    // True when a standalone proxy process using the config file derived from
    // indexId is already running (same probe as inside startStandaloneProxy).
    // Lets the UI reject a duplicate start before doing port checks.
    bool isStandaloneProxyRunning(const std::string& indexId) const;
    bool stopStandaloneProxy(const std::string& indexId);
    void shutdownStandaloneProxies();
    int getStandaloneSocksPort(const std::string& indexId) const;
    std::vector<std::string> getRunningStandaloneIds() const;
    // Returns the count of currently running standalone proxy processes.
    int getRunningStandaloneCount() const;
    // Adopts dangling standalone proxy processes found in the system (xray /
    // sing-box started with a standalone_*.json config but not yet managed by
    // this controller, e.g. left over from a previous GUI session). Each is
    // registered in standaloneProxies_ and watched so its exit is recorded in
    // the runtime history. Called when the watch mechanism starts (GUI ready).
    void adoptDanglingStandaloneProxies();

    // Batch region resolution (after testing)
    int resolveRegionsForValidProxies(const std::string& subId = "");

    // Async batch region resolution (standalone, via UI menu)
    void resolveRegionsBatchAsync(wxEvtHandler* handler, const std::string& subId = "");

    // Async single proxy region resolution (via UI menu - uses batch resolver internally)
    void resolveSingleProxyRegionAsync(const std::string& indexId, wxEvtHandler* handler);

    // Associate the controller with the top-level window (used for event dispatch and shutdown).
    void setTopWindow(wxWindow* win);

private:
  void doUpdateSubscription(const std::string& subId, wxEvtHandler* wxHandler);
  void doUpdateAllSubscriptions(wxEvtHandler* wxHandler);
  void doTestSubscription(const std::string& subId, wxEvtHandler* wxHandler);
  void doTestSingleProxy(const std::string& indexId, wxEvtHandler* wxHandler);
  void doTestAllProxies(wxEvtHandler* wxHandler);
  void doFindFirstProxy(wxEvtHandler* wxHandler);
  void doFindBestProxy(wxEvtHandler* wxHandler);
  void doSyncDatabases(wxEvtHandler* wxHandler);
    void doAutoTaskImpl(wxEvtHandler* wxHandler, bool resume);
    void doRunAutoTask(wxEvtHandler* wxHandler);
    void doResumeAutoTask(wxEvtHandler* wxHandler);
    void doResolveRegionsBatch(wxEvtHandler* handler, const std::string& subId);
    void doResolveSingleProxyRegion(const std::string& indexId, wxEvtHandler* handler);

  sqlite3* db_;
  config::AppConfig config_;
  std::atomic<bool> cancelRequested_{false};
  std::atomic<bool> isRunning_{false};
  // Worker thread (single at a time)
std::thread workerThread_;
   TestResult lastFindResult_;
   NetworkMonitor netMon_;
   bool netMonEnabled_{false};  // Cache for MainFrame to query

  // Standalone proxy state
  mutable std::mutex standaloneMutex_;
  std::unordered_map<std::string, StandaloneProxyInfo> standaloneProxies_;

  // Top-level window for event dispatch / shutdown
  wxWindow* topWindow_{nullptr};

  // Listener for standalone process exits
  proc::ProcessExitListener* exitListener_{nullptr};
  db::models::ProxyRuntimeHistoryDAO historyDao_{db_};  // for standalone proxy runtime tracking
  std::optional<int64_t> runtimeHistoryId_{std::nullopt};
  std::unordered_map<std::string, proc::ProcessExitListener::WatchKey> proxyWatchKeys_;

  // Flag: set to true before destructors to signal watchers to stop.
  std::atomic<bool> shutdownRequested_{false};
};

#endif // UI_APP_CONTROLLER_H
