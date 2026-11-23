// src/service/CloudSyncService.cpp
#include "service/CloudSyncService.h"
#include "i18n/I18n.h"

#include <boost/json.hpp>

#include <algorithm>
#include <utility>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "AtomicFileWrite.h"
#include "Logger.h"
#include "Utils.h"

namespace fs = std::filesystem;

namespace service {

namespace {

const char* const kConfigFileName = "config.json";
const char* const kSubsFileName = "subscriptions.json";

std::string readWholeFile(const std::string& path, bool& ok) {
    ok = false;
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return std::string();
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    ok = true;
    return ss.str();
}

// Minimal HTTP-date parser. Accepts the three RFC 7231 forms; returns 0 on
// failure so callers can fall back to Conflict.
long long parseHttpDateImpl(const std::string& value) {
    // Hand-rolled IMF-fixdate parse: "Wed, 21 Oct 2026 07:28:00 GMT"
    static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    std::string s = value;
    const std::size_t comma = s.find(',');
    if (comma == std::string::npos) {
        return 0;
    }
    s = s.substr(comma + 1);
    int day = 0;
    char month[4] = {0, 0, 0, 0};
    int year = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (std::sscanf(s.c_str(), " %d %3s %d %d:%d:%d", &day, month, &year, &hour, &minute,
                    &second) != 6) {
        return 0;
    }
    int monthIndex = -1;
    for (int i = 0; i < 12; ++i) {
        if (std::strcmp(month, kMonths[i]) == 0) {
            monthIndex = i;
            break;
        }
    }
    if (monthIndex < 0 || year < 1970) {
        return 0;
    }
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = monthIndex;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
#ifdef _WIN32
    const std::time_t asTime = _mkgmtime(&tm);
#else
    const std::time_t asTime = timegm(&tm);
#endif
    return asTime > 0 ? static_cast<long long>(asTime) : 0;
}

std::string jsonString(const boost::json::object& obj, const char* key) {
    const boost::json::value* v = obj.if_contains(key);
    if (v == nullptr) {
        return std::string();
    }
    if (v->is_string()) {
        return std::string(v->as_string().c_str());
    }
    if (v->is_int64()) {
        return std::to_string(v->as_int64());
    }
    if (v->is_uint64()) {
        return std::to_string(v->as_uint64());
    }
    if (v->is_bool()) {
        return v->as_bool() ? "1" : "0";
    }
    return std::string();
}

void putString(boost::json::object& obj, const char* key, const std::string& value) {
    obj[key] = value;
}

} // namespace

std::string CloudSyncService::remotePathFor(const std::string& remoteDir,
                                            const std::string& name) {
    if (remoteDir.empty()) {
        return name;
    }
    std::string dir = remoteDir;
    while (!dir.empty() && dir.front() == '/') {
        dir.erase(dir.begin());
    }
    while (!dir.empty() && dir.back() == '/') {
        dir.pop_back();
    }
    return dir + "/" + name;
}

CloudSyncService::CloudSyncService(net::WebDavClient& client, std::string remoteDir,
                                   std::string localConfigPath,
                                   db::models::SubitemDAO& subDao)
    : client_(client),
      remoteDir_(std::move(remoteDir)),
      localConfigPath_(std::move(localConfigPath)),
      subDao_(subDao) {}


SyncDecision CloudSyncService::decide(long long localEpoch, long long remoteEpoch) {
    if (remoteEpoch <= 0) {
        return SyncDecision::Push;
    }
    if (localEpoch <= 0) {
        return SyncDecision::Pull;
    }
    const long long diff = remoteEpoch - localEpoch;
    if (diff > kClockToleranceSeconds) {
        return SyncDecision::Pull;
    }
    if (diff < -kClockToleranceSeconds) {
        return SyncDecision::Push;
    }
    return SyncDecision::Skip;
}

long long CloudSyncService::parseHttpDate(const std::string& httpDate) {
    if (httpDate.empty()) {
        return 0;
    }
    return parseHttpDateImpl(httpDate);
}

std::string CloudSyncService::encodeSubscriptions(
        const std::vector<SubscriptionRecord>& items) {
    boost::json::object root;
    root["version"] = 1;

    std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    char stamp[32] = {0};
    std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    root["exported_at"] = std::string(stamp);

    boost::json::array array;
    for (const SubscriptionRecord& item : items) {
        boost::json::object obj;
        putString(obj, "id", item.id);
        putString(obj, "remarks", item.remarks);
        putString(obj, "url", item.url);
        putString(obj, "more_url", item.moreUrl);
        putString(obj, "user_agent", item.userAgent);
        putString(obj, "filter", item.filter);
        putString(obj, "convert_target", item.convertTarget);
        putString(obj, "prev_profile", item.prevProfile);
        putString(obj, "next_profile", item.nextProfile);
        putString(obj, "memo", item.memo);
        obj["enabled"] = item.enabled;
        obj["sort"] = item.sort;
        obj["auto_update_interval"] = item.autoUpdateInterval;
        obj["update_time"] = item.updateTime;
        obj["pre_socks_port"] = item.preSocksPort;
        array.push_back(obj);
    }
    root["items"] = array;
    return boost::json::serialize(root);
}

bool CloudSyncService::decodeSubscriptions(const std::string& json,
                                           std::vector<SubscriptionRecord>& out,
                                           std::string& error) {
    out.clear();
    error.clear();

    boost::system::error_code ec;
    boost::json::value root = boost::json::parse(json, ec);
    if (ec) {
        error = i18n::u8(i18n::StrId::CloudSync_SubscriptionsJsonValidJSON) + ec.message();
        return false;
    }
    if (!root.is_object()) {
        error = i18n::u8(i18n::StrId::CloudSync_TopLevelSubscriptionsJson);
        return false;
    }
    const boost::json::value* itemsVal = root.as_object().if_contains("items");
    if (itemsVal == nullptr || !itemsVal->is_array()) {
        error = i18n::u8(i18n::StrId::CloudSync_SubscriptionsJsonMissingItems);
        return false;
    }

    for (const boost::json::value& entry : itemsVal->as_array()) {
        if (!entry.is_object()) {
            continue;
        }
        const boost::json::object& obj = entry.as_object();
        SubscriptionRecord rec;
        rec.id = jsonString(obj, "id");
        if (rec.id.empty()) {
            continue;  // an entry without a key cannot be upserted
        }
        rec.remarks = jsonString(obj, "remarks");
        rec.url = jsonString(obj, "url");
        rec.moreUrl = jsonString(obj, "more_url");
        rec.userAgent = jsonString(obj, "user_agent");
        rec.filter = jsonString(obj, "filter");
        rec.convertTarget = jsonString(obj, "convert_target");
        rec.prevProfile = jsonString(obj, "prev_profile");
        rec.nextProfile = jsonString(obj, "next_profile");
        rec.memo = jsonString(obj, "memo");

        const std::string enabled = jsonString(obj, "enabled");
        rec.enabled = enabled.empty() ? 1 : std::atoi(enabled.c_str());
        const std::string sort = jsonString(obj, "sort");
        rec.sort = sort.empty() ? 0 : std::atoi(sort.c_str());
        const std::string interval = jsonString(obj, "auto_update_interval");
        rec.autoUpdateInterval = interval.empty() ? 0 : std::atoi(interval.c_str());
        const std::string updateTime = jsonString(obj, "update_time");
        rec.updateTime = updateTime.empty() ? 0 : std::atoll(updateTime.c_str());
        const std::string socksPort = jsonString(obj, "pre_socks_port");
        rec.preSocksPort = socksPort.empty() ? 0 : std::atoi(socksPort.c_str());

        out.push_back(rec);
    }
    return true;
}

SubscriptionRecord CloudSyncService::fromSubitem(const db::models::Subitem& row) {
    SubscriptionRecord rec;
    rec.id = row.id;
    rec.remarks = row.remarks;
    rec.url = row.url;
    rec.moreUrl = row.moreurl;
    rec.userAgent = row.useragent;
    rec.filter = row.filter;
    rec.convertTarget = row.converttarget;
    rec.prevProfile = row.prevprofile;
    rec.nextProfile = row.nextprofile;
    rec.memo = row.memo;
    rec.enabled = row.enabled.empty() ? 1 : std::atoi(row.enabled.c_str());
    rec.sort = row.sort.empty() ? 0 : std::atoi(row.sort.c_str());
    rec.autoUpdateInterval =
            row.autoupdateinterval.empty() ? 0 : std::atoi(row.autoupdateinterval.c_str());
    rec.updateTime = row.updatetime.empty() ? 0 : std::atoll(row.updatetime.c_str());
    rec.preSocksPort = row.presocksport.empty() ? 0 : std::atoi(row.presocksport.c_str());
    return rec;
}

db::models::Subitem CloudSyncService::toSubitem(const SubscriptionRecord& rec) {
    db::models::Subitem row;
    row.id = rec.id;
    row.remarks = rec.remarks;
    row.url = rec.url;
    row.moreurl = rec.moreUrl;
    row.useragent = rec.userAgent;
    row.filter = rec.filter;
    row.converttarget = rec.convertTarget;
    row.prevprofile = rec.prevProfile;
    row.nextprofile = rec.nextProfile;
    row.memo = rec.memo;
    row.enabled = std::to_string(rec.enabled);
    row.sort = std::to_string(rec.sort);
    row.autoupdateinterval = std::to_string(rec.autoUpdateInterval);
    row.updatetime = std::to_string(rec.updateTime);
    row.presocksport = std::to_string(rec.preSocksPort);
    return row;
}

bool CloudSyncService::ensureRemoteDir(std::string& error) const {
    error.clear();
    if (remoteDir_.empty()) {
        return true;
    }
    const std::string dir = remoteDir_;
    net::DavResponse response = client_.propfind(dir, 0);
    if (response.ok()) {
        return true;
    }
    if (response.status == 404) {
        net::DavResponse created = client_.mkcol(dir);
        if (created.ok() || created.status == 405) {  // 405 = already exists
            return true;
        }
        error = i18n::u8f(i18n::StrId::CloudSync_CannotCreateCloudDirectory,
                          { std::to_string(created.status) });
        return false;
    }
    if (response.status == 401 || response.status == 403) {
        error = i18n::u8f(i18n::StrId::CloudSync_CredentialsRejected,
                          { std::to_string(response.status) });
        return false;
    }
    error = i18n::u8f(i18n::StrId::CloudSync_CannotAccessCloudDirectory,
                          { std::to_string(response.status) }) +
            (response.error.empty() ? "" : ": " + response.error);
    return false;
}

CloudSyncResult CloudSyncService::pullConfigOnly(net::WebDavClient& client,
                                               const std::string& remoteDir,
                                               const std::string& localConfigPath) {
    CloudSyncResult result;
    std::error_code ec;
    if (fs::exists(localConfigPath, ec) && !ec) {
        result.ok = true;
        result.detail = i18n::u8(i18n::StrId::CloudSync_LocalConfigJsonAlready);
        return result;
    }

    const std::string dir = remoteDir;
    if (!dir.empty()) {
        const net::DavResponse probe = client.propfind(dir, 0);
        if (!probe.ok() && probe.status == 404) {
            const net::DavResponse created = client.mkcol(dir);
            if (!created.ok() && created.status != 405) {
                result.error = i18n::u8f(i18n::StrId::CloudSync_CannotCreateCloudDirectory,
                                 { std::to_string(created.status) });
                return result;
            }
        } else if (!probe.ok() && (probe.status == 401 || probe.status == 403)) {
            result.error = i18n::u8f(i18n::StrId::CloudSync_CredentialsRejected,
                                 { std::to_string(probe.status) });
            return result;
        }
    }

    const net::DavResponse response = client.get(remotePathFor(remoteDir, kConfigFileName));
    if (response.status == 404) {
        result.error = i18n::u8(i18n::StrId::CloudSync_ThereNoConfigJson);
        return result;
    }
    if (!response.ok()) {
        result.error = i18n::u8f(i18n::StrId::CloudSync_FailedDownloadConfigJson,
                                 { std::to_string(response.status) });
        return result;
    }
    boost::system::error_code jsonEc;
    boost::json::value parsed = boost::json::parse(response.body, jsonEc);
    if (jsonEc || !parsed.is_object()) {
        result.error = i18n::u8(i18n::StrId::CloudSync_CloudConfigJsonValid);
        return result;
    }
    if (!utils::writeFileAtomically(localConfigPath, response.body)) {
        result.error = i18n::u8(i18n::StrId::CloudSync_CannotWriteLocalConfig);
        return result;
    }
    result.ok = true;
    result.detail = i18n::u8(i18n::StrId::CloudSync_PulledConfigJsonCloud);
    Logger::write("cloud sync: config.json pulled from remote", LogLevel::REPORT);
    return result;
}

CloudSyncResult CloudSyncService::ensureLocalConfig() {
    return pullConfigOnly(client_, remoteDir_, localConfigPath_);
}

CloudSyncResult CloudSyncService::pullSubscriptions() {
    CloudSyncResult result;
    std::string dirError;
    if (!ensureRemoteDir(dirError)) {
        result.error = dirError;
        return result;
    }

    const net::DavResponse response =
            client_.get(remotePathFor(remoteDir_, kSubsFileName));
    if (response.status == 404) {
        result.error = i18n::u8(i18n::StrId::CloudSync_ThereNoSubscriptionsJson);
        return result;
    }
    if (!response.ok()) {
        result.error = i18n::u8f(i18n::StrId::CloudSync_FailedDownloadSubscriptionsJson,
                                 { std::to_string(response.status) });
        return result;
    }

    std::vector<SubscriptionRecord> remoteItems;
    std::string decodeError;
    if (!decodeSubscriptions(response.body, remoteItems, decodeError)) {
        result.error = decodeError;
        return result;
    }

    const std::vector<db::models::Subitem> local = subDao_.getAll();
    std::vector<std::string> localIds;
    localIds.reserve(local.size());
    for (const db::models::Subitem& row : local) {
        localIds.push_back(row.id);
    }

    for (const SubscriptionRecord& rec : remoteItems) {
        const bool existed =
                std::find(localIds.begin(), localIds.end(), rec.id) != localIds.end();
        if (!subDao_.upsertSubitem(toSubitem(rec))) {
            result.error = i18n::u8f(i18n::StrId::CloudSync_FailedWriteSubscriptionId,
                                     { rec.id });
            return result;
        }
        if (existed) {
            ++result.updated;
        } else {
            ++result.added;
        }
    }

    result.ok = true;
    result.detail = i18n::u8f(
            i18n::StrId::CloudSync_SubscriptionSyncCompleteAdded,
            { std::to_string(result.added), std::to_string(result.updated) });
    Logger::write("cloud sync: " + result.detail, LogLevel::REPORT);
    return result;
}

CloudSyncResult CloudSyncService::pullSubscriptionsInto(sqlite3* db,
                                                        const std::string& remoteDir,
                                                        CredentialLoader loader) {
    CloudSyncResult result;
    net::WebDavCredentials credentials;
    std::string credError;
    if (!loader(credentials, credError)) {
        result.error = credError;
        Logger::write("fresh-db cloud sync skipped: " + credError, LogLevel::WARN);
        return result;
    }
    net::WebDavClient client(credentials, "https://dav.jianguoyun.com/dav/");
    db::models::SubitemDAO subDao(db);
    // localConfigPath is unused by pullSubscriptions(); pass empty.
    CloudSyncService sync(client, remoteDir, std::string(), subDao);
    return sync.pullSubscriptions();
}

CloudSyncResult CloudSyncService::pushConfig() {
    CloudSyncResult result;
    bool readOk = false;
    const std::string body = readWholeFile(localConfigPath_, readOk);
    if (!readOk) {
        result.error = i18n::u8(i18n::StrId::CloudSync_LocalConfigJsonMissing);
        return result;
    }
    std::string dirError;
    if (!ensureRemoteDir(dirError)) {
        result.error = dirError;
        return result;
    }
    const net::DavResponse response =
            client_.put(remotePathFor(remoteDir_, kConfigFileName), body);
    if (!response.ok()) {
        result.error = i18n::u8f(i18n::StrId::CloudSync_FailedUploadConfigJson,
                                 { std::to_string(response.status) });
        return result;
    }
    result.ok = true;
    result.detail = i18n::u8(i18n::StrId::CloudSync_ConfigJsonUploaded);
    Logger::write("cloud sync: config.json pushed", LogLevel::REPORT);
    return result;
}

CloudSyncResult CloudSyncService::pushSubscriptions() {
    CloudSyncResult result;
    const std::vector<db::models::Subitem> local = subDao_.getAll();
    std::vector<SubscriptionRecord> records;
    records.reserve(local.size());
    for (const db::models::Subitem& row : local) {
        records.push_back(fromSubitem(row));
    }
    const std::string body = encodeSubscriptions(records);

    std::string dirError;
    if (!ensureRemoteDir(dirError)) {
        result.error = dirError;
        return result;
    }
    const net::DavResponse response =
            client_.put(remotePathFor(remoteDir_, kSubsFileName), body);
    if (!response.ok()) {
        result.error = i18n::u8f(i18n::StrId::CloudSync_FailedUploadSubscriptionsJson,
                                 { std::to_string(response.status) });
        return result;
    }
    result.ok = true;
    result.added = static_cast<int>(records.size());
    result.detail = i18n::u8f(i18n::StrId::CloudSync_SubscriptionsUploaded,
                              { std::to_string(records.size()) });
    Logger::write("cloud sync: " + result.detail, LogLevel::REPORT);
    return result;
}

} // namespace service
