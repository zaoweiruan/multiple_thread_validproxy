#ifndef UI_EVENTS_H
#define UI_EVENTS_H

#include <wx/wx.h>
#include <wx/event.h>
#include <string>
#include "Logger.h"
#include "LogStatistics.h"
#include <vector>
#include <unordered_map>
#include "Profileitem.h"
#include "ProfileExItem.h"
#include "Subitem.h"
#include "Utils.h"
#include "StandaloneProxyPool.h"

// ---------------------------------------------------------------
// Custom event IDs — range starting from wxID_HIGHEST + 1
// ---------------------------------------------------------------
enum class UIEventId {
    SUBSCRIPTION_UPDATED = wxID_HIGHEST + 1,
    PROXY_TEST_PROGRESS,
    PROXY_TEST_COMPLETED,
    PROXY_FOUND,
    LOG_MESSAGE,
    CONFIG_CHANGED,
    STATUS_UPDATE,
    SUBSCRIPTION_SELECTED,
    PROXY_LIST_LOADED,
    SUB_LIST_LOADED
};

// ---------------------------------------------------------------
// Forward declarations and event type declarations
// wxDECLARE_EVENT only needs a forward-declared class because
// wxEventTypeTag<T> stores a pointer to wxEventType and does
// not require T to be complete.
// ---------------------------------------------------------------
class ProxyTestProgressEvent;
class LogMessageEvent;
class LogStatisticsEvent;
class StatusUpdateEvent;
class SubscriptionSelectedEvent;
class SubscriptionTestEvent;
class ProxySelectionEvent;
class ProxyListLoadedEvent;
class SubListLoadedEvent;
class StandaloneProxyEvent;
class OnlineProbeFinishedEvent;
class RunningDurationsLoadedEvent;
class LocateProxyEvent;
class PoolMembersUpdatedEvent;
class SubscriptionRefreshEvent;
class TestOnlineProxiesEvent;

wxDECLARE_EVENT(wxEVT_PROXY_TEST_PROGRESS, ProxyTestProgressEvent);
wxDECLARE_EVENT(wxEVT_LOG_MESSAGE, LogMessageEvent);
wxDECLARE_EVENT(wxEVT_LOG_STATISTICS, LogStatisticsEvent);
wxDECLARE_EVENT(wxEVT_STATUS_UPDATE, StatusUpdateEvent);
wxDECLARE_EVENT(wxEVT_SUBSCRIPTION_SELECTED, SubscriptionSelectedEvent);
wxDECLARE_EVENT(wxEVT_SUBSCRIPTION_TEST, SubscriptionTestEvent);
wxDECLARE_EVENT(wxEVT_PROXY_SELECTION, ProxySelectionEvent);
wxDECLARE_EVENT(wxEVT_PROXY_LIST_LOADED, ProxyListLoadedEvent);
wxDECLARE_EVENT(wxEVT_SUB_LIST_LOADED, SubListLoadedEvent);
wxDECLARE_EVENT(wxEVT_STANDALONE_PROXY, StandaloneProxyEvent);
wxDECLARE_EVENT(wxEVT_ONLINE_PROBE_FINISHED, OnlineProbeFinishedEvent);
wxDECLARE_EVENT(wxEVT_RUNNING_DURATIONS_LOADED, RunningDurationsLoadedEvent);
wxDECLARE_EVENT(wxEVT_LOCATE_PROXY, LocateProxyEvent);
wxDECLARE_EVENT(wxEVT_POOL_MEMBERS_UPDATED, PoolMembersUpdatedEvent);
wxDECLARE_EVENT(wxEVT_SUBSCRIPTION_REFRESH, SubscriptionRefreshEvent);
wxDECLARE_EVENT(wxEVT_TEST_ONLINE_PROXIES, TestOnlineProxiesEvent);

// ---------------------------------------------------------------
// ProxyTestProgressEvent — sent during batch testing
// ---------------------------------------------------------------
class ProxyTestProgressEvent : public wxEvent {
public:
    ProxyTestProgressEvent(int current = 0, int total = 0,
                           const std::string& proxyId = "",
                           const std::string& remarks = "",
                           const std::string& delay = "",
                           const std::string& message = "",
                           bool isCompleted = false)
        : wxEvent(0, wxEVT_PROXY_TEST_PROGRESS),
          current_(current), total_(total),
          proxyId_(proxyId), remarks_(remarks),
          delay_(delay), message_(message),
          isCompleted_(isCompleted) {}

    wxEvent* Clone() const override { return new ProxyTestProgressEvent(*this); }

    int getCurrent() const { return current_; }
    int getTotal() const { return total_; }
    std::string getProxyId() const { return proxyId_; }
    std::string getRemarks() const { return remarks_; }
    std::string getDelay() const { return delay_; }
    std::string getMessage() const { return message_; }
    bool isCompleted() const { return isCompleted_; }

private:
    int current_, total_;
    std::string proxyId_, remarks_, delay_, message_;
    bool isCompleted_;
};

// ---------------------------------------------------------------
// LogMessageEvent — sent from worker threads to UI log panel
// ---------------------------------------------------------------
class LogMessageEvent : public wxEvent {
public:
    LogMessageEvent(const std::string& msg = "", LogLevel level = LogLevel::INFO)
        : wxEvent(0, wxEVT_LOG_MESSAGE), message_(msg), level_(level) {}

    wxEvent* Clone() const override { return new LogMessageEvent(*this); }

    std::string getMessage() const { return message_; }
    LogLevel getLevel() const { return level_; }

private:
    std::string message_;
    LogLevel level_;
};

// ---------------------------------------------------------------
// LogStatisticsEvent — sent from the statistics worker thread to
// the UI thread when log-file parsing finishes
// ---------------------------------------------------------------
class LogStatisticsEvent : public wxEvent {
public:
    LogStatisticsEvent(const std::string& filePath = "",
                       const LogStatisticsResult& result = LogStatisticsResult())
        : wxEvent(0, wxEVT_LOG_STATISTICS), filePath_(filePath), result_(result) {}

    wxEvent* Clone() const override { return new LogStatisticsEvent(*this); }

    const LogStatisticsResult& getResult() const { return result_; }
    std::string getFilePath() const { return filePath_; }

private:
    std::string filePath_;
    LogStatisticsResult result_;
};

// ---------------------------------------------------------------
// StatusUpdateEvent — status bar updates from worker threads
// ---------------------------------------------------------------
class StatusUpdateEvent : public wxEvent {
public:
    StatusUpdateEvent(int field = 0, const std::string& text = "")
        : wxEvent(0, wxEVT_STATUS_UPDATE), field_(field), text_(text) {}

    wxEvent* Clone() const override { return new StatusUpdateEvent(*this); }

    int getField() const { return field_; }
    std::string getText() const { return text_; }

private:
    int field_;
    std::string text_;
};

// ---------------------------------------------------------------
// SubscriptionSelectedEvent — sent when a subscription is clicked
// ---------------------------------------------------------------
class SubscriptionSelectedEvent : public wxEvent {
public:
    explicit SubscriptionSelectedEvent(const std::string& subId = "")
        : wxEvent(0, wxEVT_SUBSCRIPTION_SELECTED), subId_(subId) {}

    wxEvent* Clone() const override { return new SubscriptionSelectedEvent(*this); }

    std::string getSubId() const { return subId_; }

private:
    std::string subId_;
};

// ---------------------------------------------------------------
// SubscriptionRefreshEvent — sent after the subscription list is
// refreshed, telling MainFrame to reload the proxy list (all proxies)
// ---------------------------------------------------------------
class SubscriptionRefreshEvent : public wxEvent {
public:
    explicit SubscriptionRefreshEvent()
        : wxEvent(0, wxEVT_SUBSCRIPTION_REFRESH) {}

    wxEvent* Clone() const override { return new SubscriptionRefreshEvent(*this); }
};

// ---------------------------------------------------------------
// SubscriptionTestEvent — sent when user right-clicks a subscription
// and selects "Test", instructing MainFrame to run proxy testing
// ---------------------------------------------------------------
class SubscriptionTestEvent : public wxEvent {
public:
    explicit SubscriptionTestEvent(const std::string& subId = "")
        : wxEvent(0, wxEVT_SUBSCRIPTION_TEST), subId_(subId) {}

    wxEvent* Clone() const override { return new SubscriptionTestEvent(*this); }

    std::string getSubId() const { return subId_; }

private:
    std::string subId_;
};

// ---------------------------------------------------------------
// ProxySelectionEvent — sent when a proxy is selected in the list
// ---------------------------------------------------------------
class ProxySelectionEvent : public wxEvent {
public:
    ProxySelectionEvent(const std::string& indexId = "",
                        const std::string& host = "",
                        const std::string& port = "",
                        const std::string& delay = "",
                        const std::string& message = "",
                        int failures = 0,
                        const std::string& remarks = "")
        : wxEvent(0, wxEVT_PROXY_SELECTION),
          indexId_(indexId), host_(host), port_(port),
          delay_(delay), message_(message), remarks_(remarks),
          failures_(failures) {}

    wxEvent* Clone() const override { return new ProxySelectionEvent(*this); }

    std::string getIndexId() const { return indexId_; }
    std::string getHost() const { return host_; }
    std::string getPort() const { return port_; }
    std::string getDelay() const { return delay_; }
    std::string getMessage() const { return message_; }
    int getFailures() const { return failures_; }
    std::string getRemarks() const { return remarks_; }

private:
    std::string indexId_, host_, port_, delay_, message_, remarks_;
    int failures_;
};

// ---------------------------------------------------------------
// ProxyListLoadedEvent — proxy list data ready from async reader
// ---------------------------------------------------------------
class ProxyListLoadedEvent : public wxEvent {
public:
    ProxyListLoadedEvent(const std::string& subId,
                        std::vector<db::models::Profileitem> proxies,
                        std::vector<db::models::ProfileExItem> exItems,
                        utils::ProxyListMaps maps = utils::ProxyListMaps())
        : wxEvent(0, wxEVT_PROXY_LIST_LOADED),
          subId_(subId),
          proxies_(std::move(proxies)),
          exItems_(std::move(exItems)),
          maps_(std::move(maps)) {}

    wxEvent* Clone() const override { return new ProxyListLoadedEvent(*this); }

    const std::string& getSubId() const { return subId_; }
    std::vector<db::models::Profileitem> takeProxies() { return std::move(proxies_); }
    std::vector<db::models::ProfileExItem> takeExItems() { return std::move(exItems_); }
    utils::ProxyListMaps takeMaps() { return std::move(maps_); }

private:
    std::string subId_;
    std::vector<db::models::Profileitem> proxies_;
    std::vector<db::models::ProfileExItem> exItems_;
    utils::ProxyListMaps maps_;
};

// ---------------------------------------------------------------
// SubListLoadedEvent — subscription list data ready from async reader
// ---------------------------------------------------------------
class SubListLoadedEvent : public wxEvent {
public:
    SubListLoadedEvent(std::vector<db::models::Subitem> subs,
                      std::unordered_map<std::string, int> proxyCounts)
        : wxEvent(0, wxEVT_SUB_LIST_LOADED),
          subs_(std::move(subs)),
          proxyCounts_(std::move(proxyCounts)) {}

    wxEvent* Clone() const override { return new SubListLoadedEvent(*this); }

    std::vector<db::models::Subitem> takeSubs() { return std::move(subs_); }
    std::unordered_map<std::string, int> takeProxyCounts() { return std::move(proxyCounts_); }

private:
    std::vector<db::models::Subitem> subs_;
    std::unordered_map<std::string, int> proxyCounts_;
};

// ---------------------------------------------------------------
// RunningDurationsLoadedEvent — live running-session durations (ms)
// keyed by indexId, fetched on a background thread.  The payload is
// intentionally tiny: only proxy_runtime_history rows whose ended_at
// IS NULL (in-progress sessions), so the UI can merge elapsed time
// into the Runtime/Health columns without a full DB re-read.
// ---------------------------------------------------------------
class RunningDurationsLoadedEvent : public wxEvent {
public:
    explicit RunningDurationsLoadedEvent(
        std::unordered_map<std::string, long long> durations)
        : wxEvent(0, wxEVT_RUNNING_DURATIONS_LOADED),
          durations_(std::move(durations)) {}

    wxEvent* Clone() const override { return new RunningDurationsLoadedEvent(*this); }

    std::unordered_map<std::string, long long> takeDurations() {
        return std::move(durations_);
    }

private:
    std::unordered_map<std::string, long long> durations_;
};

// ---------------------------------------------------------------
// StandaloneProxyEvent — sent when a standalone proxy starts/stops
// ---------------------------------------------------------------
class StandaloneProxyEvent : public wxEvent {
public:
    StandaloneProxyEvent(const std::string& indexId = "",
                         const std::string& address = "",
                         int socksPort = 0,
                         bool started = true,
                         const std::string& error = "")
        : wxEvent(0, wxEVT_STANDALONE_PROXY),
          indexId_(indexId), address_(address), error_(error),
          socksPort_(socksPort), started_(started) {}

    wxEvent* Clone() const override { return new StandaloneProxyEvent(*this); }

    std::string getIndexId() const { return indexId_; }
    std::string getAddress() const { return address_; }
    int getSocksPort() const { return socksPort_; }
    bool isStarted() const { return started_; }
    std::string getError() const { return error_; }

private:
    std::string indexId_, address_, error_;
    int socksPort_;
    bool started_;
};

// ---------------------------------------------------------------
// OnlineProbeFinishedEvent — posted by AppController after the periodic
// SILENT probe completes. Carries the indexIds that were actually tested
// so the proxy list can refresh only those rows (incremental, no full
// 53k-row reload). Replaces the old StatusUpdateEvent("ONLINE_PROBE_DONE").
// ---------------------------------------------------------------
class OnlineProbeFinishedEvent : public wxEvent {
public:
    explicit OnlineProbeFinishedEvent(std::vector<std::string> indexIds = std::vector<std::string>())
        : wxEvent(0, wxEVT_ONLINE_PROBE_FINISHED),
          indexIds_(std::move(indexIds)) {}

    wxEvent* Clone() const override { return new OnlineProbeFinishedEvent(*this); }

    std::vector<std::string> takeIndexIds() { return std::move(indexIds_); }
    const std::vector<std::string>& getIndexIds() const { return indexIds_; }

private:
    std::vector<std::string> indexIds_;
};

// ---------------------------------------------------------------
// LocateProxyEvent — sent when a row in the standalone monitor
// dialog is double-clicked, requesting MainFrame to locate (select
// + scroll into view) the corresponding proxy in ProxyListPanel.
// ---------------------------------------------------------------
class LocateProxyEvent : public wxEvent {
public:
    explicit LocateProxyEvent(const std::string& indexId = "")
        : wxEvent(0, wxEVT_LOCATE_PROXY), indexId_(indexId) {}

    wxEvent* Clone() const override { return new LocateProxyEvent(*this); }

    std::string getIndexId() const { return indexId_; }

private:
    std::string indexId_;
};

// ---------------------------------------------------------------
// PoolMembersUpdatedEvent — posted by AppController whenever the standalone
// proxy pool's membership/health snapshot changes (after each evaluation
// cycle). Carries a copy of the current member views for the UI to render.
// ---------------------------------------------------------------
class PoolMembersUpdatedEvent : public wxEvent {
public:
    explicit PoolMembersUpdatedEvent(std::vector<proxy::PoolMemberView> members = std::vector<proxy::PoolMemberView>())
        : wxEvent(0, wxEVT_POOL_MEMBERS_UPDATED),
          members_(std::move(members)) {}

    wxEvent* Clone() const override { return new PoolMembersUpdatedEvent(*this); }

    std::vector<proxy::PoolMemberView> takeMembers() { return std::move(members_); }

private:
    std::vector<proxy::PoolMemberView> members_;
};

// ---------------------------------------------------------------
// TestOnlineProxiesEvent — posted by AppController after the "Test
// Online Proxies" batch connectivity test against all currently
// running standalone proxy processes completes. Carries the list of
// failed proxy indexIds plus summary counts so the ProxyListPanel can
// refresh and notify the user which proxies are offline/unreachable.
// ---------------------------------------------------------------
class TestOnlineProxiesEvent : public wxEvent {
public:
    TestOnlineProxiesEvent(std::vector<std::string> failedIndexIds = std::vector<std::string>(),
                           int total = 0,
                           int success = 0,
                           int failed = 0)
        : wxEvent(0, wxEVT_TEST_ONLINE_PROXIES),
          failedIndexIds_(std::move(failedIndexIds)),
          total_(total), success_(success), failed_(failed) {}

    wxEvent* Clone() const override { return new TestOnlineProxiesEvent(*this); }

    std::vector<std::string> takeFailedIndexIds() { return std::move(failedIndexIds_); }
    int getTotal() const { return total_; }
    int getSuccess() const { return success_; }
    int getFailed() const { return failed_; }

private:
    std::vector<std::string> failedIndexIds_;
    int total_, success_, failed_;
};

#endif // UI_EVENTS_H
