#ifndef UI_EVENTS_H
#define UI_EVENTS_H

#include <wx/wx.h>
#include <wx/event.h>
#include <string>
#include "Logger.h"
#include <vector>
#include <unordered_map>
#include "Profileitem.h"
#include "ProfileExItem.h"
#include "Subitem.h"

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
class StatusUpdateEvent;
class SubscriptionSelectedEvent;
class SubscriptionTestEvent;
class ProxySelectionEvent;
class ProxyListLoadedEvent;
class SubListLoadedEvent;

wxDECLARE_EVENT(wxEVT_PROXY_TEST_PROGRESS, ProxyTestProgressEvent);
wxDECLARE_EVENT(wxEVT_LOG_MESSAGE, LogMessageEvent);
wxDECLARE_EVENT(wxEVT_STATUS_UPDATE, StatusUpdateEvent);
wxDECLARE_EVENT(wxEVT_SUBSCRIPTION_SELECTED, SubscriptionSelectedEvent);
wxDECLARE_EVENT(wxEVT_SUBSCRIPTION_TEST, SubscriptionTestEvent);
wxDECLARE_EVENT(wxEVT_PROXY_SELECTION, ProxySelectionEvent);
wxDECLARE_EVENT(wxEVT_PROXY_LIST_LOADED, ProxyListLoadedEvent);
wxDECLARE_EVENT(wxEVT_SUB_LIST_LOADED, SubListLoadedEvent);

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
                        std::vector<db::models::ProfileExItem> exItems)
        : wxEvent(0, wxEVT_PROXY_LIST_LOADED),
          subId_(subId),
          proxies_(std::move(proxies)),
          exItems_(std::move(exItems)) {}

    wxEvent* Clone() const override { return new ProxyListLoadedEvent(*this); }

    const std::string& getSubId() const { return subId_; }
    std::vector<db::models::Profileitem> takeProxies() { return std::move(proxies_); }
    std::vector<db::models::ProfileExItem> takeExItems() { return std::move(exItems_); }

private:
    std::string subId_;
    std::vector<db::models::Profileitem> proxies_;
    std::vector<db::models::ProfileExItem> exItems_;
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

#endif // UI_EVENTS_H
