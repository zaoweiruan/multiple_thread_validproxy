#ifndef UI_APP_CONTROLLER_H
#define UI_APP_CONTROLLER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
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
#include "StandaloneProxyPool.h"
#include "PoolCandidate.h"

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
    // True when this process instance was launched by this application during
    // the current session (startStandaloneProxy). Adopted orphans keep it
    // false: their port must be resolved from live facts, whereas a session
    // started proxy already knows its own SOCKS port and only needs its
    // history row back-filled (spec v1.6 "bypass detection" route).
    bool startedByThisSession = false;
};

// A standalone proxy launched by THIS application during the current session.
// `creationTime` is the process creation time captured at launch: it proves
// that the pid still refers to the very instance we started (a recycled pid
// would otherwise be mistaken for one of ours).
struct SessionStartedProxy {
    int socksPort = 0;
    std::string creationTime;  // empty when the creation time was unreadable
};

// One row of the standalone-proxy monitor dialog: a watched process joined
// with its in-progress proxy_runtime_history row. durationMs is computed
// live (now - startedAt) so the dialog refreshes smoothly between heartbeats.
struct StandaloneMonitorRow {
    std::string indexId;
    std::string host;        // ProfileItem.Address; empty when profile missing
    std::string startedAt;   // "yyyy-MM-dd HH:mm:ss"; empty when unknown
    int64_t durationMs = 0;  // live elapsed time
    int socksPort = 0;       // 0 = port unknown (config parse failed)
    int64_t pid = -1;        // -1 = history row missing
    long long lastDelayMs = -1; // last test delay (ms); -1 = unknown / not tested
};

// ---------------------------------------------------------------
// UnifiedMonitorRow — one row of the unified floating-widget monitor.
// Merges standalone watched proxies (StandaloneMonitorRow) with proxy-pool
// members (proxy::PoolMemberView) so the floating panel can render both
// kinds in a single list with a "type" column.
// ---------------------------------------------------------------
enum class MonitorType {
    Standalone,
    Pool
};

struct UnifiedMonitorRow {
    MonitorType type = MonitorType::Standalone;
    std::string indexId;      // standalone: proxy indexId; pool: std::to_string(PoolMemberView::indexId)
    std::string tag;          // pool: "px-<indexId>"; standalone: empty
    std::string host;         // profile address; empty when unknown
    int socksPort = 0;        // 0 = port unknown
    int64_t pid = -1;         // -1 = unknown (pool rows always -1)
    int64_t durationMs = 0;   // standalone: live elapsed time; pool: 0
    std::string state;        // standalone: "运行中"; pool: "active"/"remove-requested"/"draining"
    long long lastDelayMs = -1; // pool: last probe delay; standalone: -1
    bool lastAlive = false;   // pool: last probe alive flag; standalone: false
    int failStreak = 0;       // pool: consecutive failures; standalone: 0
    std::string lastError;    // pool: last probe error; standalone: empty
};

class AppController {
public:
  AppController(sqlite3* db, const config::AppConfig& cfg);
  ~AppController();

// Config
   config::AppConfig getConfig() const { return config_; }
   bool saveConfig(const config::AppConfig& cfg);
   bool isRunning() const { return isRunning_.load(); }
    // True while the periodic SILENT probe (background task) is running.
    // The config dialog uses this to allow ordinary config saves during the
    // probe (it snapshots config_ under configMutex_) while still blocking
    // saves during user-initiated operations.
    bool isOnlineProbeRunning() const { return onlineProbeRunning_.load(); }

  // Database switching (close old + open new)
  sqlite3* switchDatabase(const std::string& newPath);

  // Stops the periodic silent probe and waits for its thread to finish, so
  // the live database handle can be swapped safely. The probe holds the
  // sqlite3* handle for the whole round, therefore it must be quiesced
  // BEFORE switchDatabase() closes the old handle.
  // Must be called from the UI thread (the probe is started from there).
  // Returns false when the probe thread could not be joined in time; the
  // caller must then refuse the database switch.
  bool quiesceOnlineProbe();

// Subscriptions
   std::vector<db::models::Subitem> loadSubscriptions();
   void loadSubscriptionsAsync(wxEvtHandler* handler);
   bool updateSubscriptionEnabled(const std::string& id, bool enabled);
  bool updateSubitem(const db::models::Subitem& sub);
    bool deleteSubscription(const std::string& subId);
    bool deleteProxiesBySubId(const std::string& subId);
    void updateSubscriptionAsync(const std::string& subId, wxEvtHandler* wxHandler);
  void updateAllSubscriptionsAsync(wxEvtHandler* wxHandler);
  bool importSubscription(const std::string& url, wxEvtHandler* wxHandler);

// Proxies
std::vector<db::models::Profileitem> loadProxies(const std::string& subId = "");
void loadProxiesAsync(const std::string& subId, wxEvtHandler* handler);
std::unordered_map<std::string, int> countProxiesBySubId();
std::unordered_map<std::string, int> countValidProxiesBySubId();
std::optional<db::models::Profileitem> getProxyByIndexId(const std::string& indexId);
// Returns the owning subscription id (Subitem.id) for a proxy indexId, or an
// empty string when the proxy does not exist or has no assigned subscription.
std::string getSubIdByProxyIndexId(const std::string& indexId);
std::vector<db::models::ProfileExItem> loadProxyResults();
// Small indexed query: loads only the ProfileExItem rows whose indexId is in
// the given list (probe-triggered incremental refresh).  Keeps the full-table
// re-read off the UI thread for large databases.
std::vector<db::models::ProfileExItem> loadProxyResultsFor(const std::vector<std::string>& indexIds);

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

// Snapshot of all currently watched standalone proxies (running && managed).
// Copies the watched set under standaloneMutex_, then joins with in-progress
// history rows for pid/startedAt. Safe to call from the UI thread.
std::vector<StandaloneMonitorRow> getWatchedStandaloneMonitors();

// Unified snapshot for the floating monitor panel: standalone watched proxies
// first, then proxy-pool members. Each source is snapshotted under its own
// mutex (no cross-lock ordering dependency). Safe to call from the UI thread.
std::vector<UnifiedMonitorRow> getUnifiedMonitorRows();

// ---------------------------------------------------------------
// Testing / Cancellation
// ---------------------------------------------------------------
void testSubscriptionAsync(const std::string& subId, wxEvtHandler* wxHandler);
void testSingleProxyAsync(const std::string& indexId, wxEvtHandler* wxHandler);
void testAllProxiesAsync(wxEvtHandler* wxHandler);
void testOnlineProxiesAsync(wxEvtHandler* wxHandler, bool silent = false);
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

     // Standalone proxy POOL (single xray process, dynamic member injection via
     // balancer+observatory). Phase 1 = empty pool + double-click inject.
     bool isProxyPoolEnabled() const;
     bool startProxyPool();
     void stopProxyPool();
     bool isProxyPoolRunning() const;
     // Inject a profile (by indexId) into the running pool as member px-<indexId>.
     bool injectProxyToPool(const std::string& indexId);
     // Remove a pool member. graceful=true defers handler removal to the
     // evaluator (two-phase); false removes immediately.
     bool removePoolMember(long long indexId, bool graceful);
     std::vector<proxy::PoolMemberView> getPoolMembers() const;
      // Candidate proxies for the "add to pool" picker (capped). Returns only
      // valid proxies (ProfileExItem.delay > 0) with extended columns
      // (delay / message / start_count / crash_count) for the picker UI.
      std::vector<pool_candidate::PoolCandidateItem> getPoolCandidateProfiles(int limit = 300);
     void setPoolReportHealth(bool on);
     void setPoolAutoPruneDead(bool on);
     void setPoolAutoOptimize(bool on);
     // Independent-proxy periodic silent probe (decoupled from the floating
     // widget switch per 方案 B). isIndependentProbeEnabled() is read by the
     // monitor timer each tick; setIndependentProbeEnabled() is the live
     // runtime toggle from StandaloneFloatingWidget (persisted via ConfigDialog).
     bool isIndependentProbeEnabled() const;
     void setIndependentProbeEnabled(bool on);
     // Trigger an immediate health probe of the running pool (no-op when idle).
     void probePoolNow();

    // Batch region resolution (after testing)
    int resolveRegionsForValidProxies(const std::string& subId = "");

    // Async batch region resolution (standalone, via UI menu)
    void resolveRegionsBatchAsync(wxEvtHandler* handler, const std::string& subId = "");

    // Async single proxy region resolution (via UI menu - uses batch resolver internally)
    void resolveSingleProxyRegionAsync(const std::string& indexId, wxEvtHandler* handler);

    // Associate the controller with the top-level window (used for event dispatch and shutdown).
    void setTopWindow(wxWindow* win);

private:
  // Tries to close the database handles that a previous switch could not
  // close (sqlite3_close() returned SQLITE_BUSY). Handles that are still
  // busy stay in the list for a later attempt.
  void closeRetiredDbHandles();
  void doUpdateSubscription(const std::string& subId, wxEvtHandler* wxHandler);
  void doUpdateAllSubscriptions(wxEvtHandler* wxHandler);
  void doTestSubscription(const std::string& subId, wxEvtHandler* wxHandler);
  void doTestSingleProxy(const std::string& indexId, wxEvtHandler* wxHandler);
  void doTestAllProxies(wxEvtHandler* wxHandler);
  void doTestOnlineProxies(wxEvtHandler* wxHandler, bool silent);
  void doFindFirstProxy(wxEvtHandler* wxHandler);
  void doFindBestProxy(wxEvtHandler* wxHandler);
  void doSyncDatabases(wxEvtHandler* wxHandler);
    void doAutoTaskImpl(wxEvtHandler* wxHandler, bool resume);
    void doRunAutoTask(wxEvtHandler* wxHandler);
    void doResumeAutoTask(wxEvtHandler* wxHandler);
    void doResolveRegionsBatch(wxEvtHandler* handler, const std::string& subId);
    void doResolveSingleProxyRegion(const std::string& indexId, wxEvtHandler* handler);

  sqlite3* db_;
  // Handles of databases that were replaced but could not be closed yet
  // (sqlite3_close() returns SQLITE_BUSY while a statement is still
  // running). They are closed on the next switch and in the destructor so
  // the old file is not locked on Windows.
  std::vector<sqlite3*> retiredDbHandles_;
  config::AppConfig config_;
  // Guards config_ against saveConfig() racing the background silent probe's
  // config snapshot (doTestOnlineProxies takes the snapshot under this mutex).
  mutable std::mutex configMutex_;
  std::atomic<bool> cancelRequested_{false};
  std::atomic<bool> isRunning_{false};
  // Set while the periodic silent probe runs (cleared by ScopeGuard in
  // doTestOnlineProxies). Distinct from isRunning_ so the config dialog can
  // allow saves during this background task.
  std::atomic<bool> onlineProbeRunning_{false};
  // 周期 silent 探活专用独立线程：不占共享 workerThread_/isRunning_，
  // 用户操作优先（手动测试先取消探活再启动）。
  std::thread probeThread_;
  // Worker thread (single at a time)
std::thread workerThread_;
   TestResult lastFindResult_;
   NetworkMonitor netMon_;
   bool netMonEnabled_{false};  // Cache for MainFrame to query

  // Standalone proxy state
  mutable std::mutex standaloneMutex_;
  std::unordered_map<std::string, StandaloneProxyInfo> standaloneProxies_;

  // Standalone proxy POOL (single xray process, dynamic members)
  mutable std::mutex poolMutex_;
  std::shared_ptr<proxy::StandaloneProxyPool> proxyPool_;
  int poolSocksPort_ = 0;  // reserved via PortManager; freed on stop
  int poolApiPort_ = 0;

  // Top-level window for event dispatch / shutdown
  wxWindow* topWindow_{nullptr};

  // Listener for standalone process exits
  proc::ProcessExitListener* exitListener_{nullptr};
  db::models::ProxyRuntimeHistoryDAO historyDao_{db_};  // for standalone proxy runtime tracking
  db::models::ProfileExItemDAO exDao_{db_};             // for startup-time message writeback
  std::optional<int64_t> runtimeHistoryId_{std::nullopt};
  std::unordered_map<std::string, proc::ProcessExitListener::WatchKey> proxyWatchKeys_;  // Guarded by standaloneMutex_
  // Pids already reported as discarded during dangling adoption, mapped to the
  // creation-time fingerprint of the process instance that was discarded.
  // Adoption runs every checkIntervalMs, so without this the same pid would log
  // a WARN each cycle. The fingerprint makes a RECYCLED pid count as a new
  // instance: the OS may hand the same pid to a different process later, and
  // that process must be reported again (otherwise a genuine "process alive
  // but not monitored" situation stays invisible in the log file).
  // Guarded by standaloneMutex_.
  std::unordered_map<DWORD, std::string> discardedStandalonePids_;
  // In-flight guard for the dangling-adoption sweep. The sweep registers
  // watchers and appends history rows, so two concurrent sweeps would
  // duplicate both and overwrite each other's watcher key.
  // Released through ui::ScopeGuard so every exit path resets it.
  std::atomic<bool> adoptInFlight_{false};
  // Mismatching configuration records already reported by the adoption sweep,
  // keyed by indexId and holding the mismatched field list that was reported.
  // The sweep runs every checkIntervalMs, so without this the very same
  // mismatch would be logged at WARN on every cycle. A NEW field list (or a
  // new indexId) is reported again. Guarded by staleConfigMutex_.
  std::unordered_map<std::string, std::string> staleConfigWarned_;
  std::mutex staleConfigMutex_;
  // Pids of standalone proxies launched by THIS application during the
  // current session, mapped to the SOCKS port we configured them with.
  // The dangling-adoption scan uses this to recognise our own processes and
  // take the "bypass detection" route: skip the live OS probe (we already
  // know the port) and only make sure the history table has a row for them.
  // Keyed by pid because the command line of a session started proxy carries
  // no marker that survives a config file rewrite; the recorded creation time
  // proves that the pid still refers to the very instance we launched (a
  // recycled pid would otherwise be mistaken for one of ours).
  // Guarded by standaloneMutex_.
  std::unordered_map<DWORD, SessionStartedProxy> sessionStartedProxies_;

  // Flag: set to true before destructors to signal watchers to stop.
  std::atomic<bool> shutdownRequested_{false};
};

#endif // UI_APP_CONTROLLER_H
