// tests/test_cloud_sync.cpp - TDD unit tests for WebDAV cloud sync.
//
// Pure logic only: no network, no sqlite. Networked paths are exercised manually
// (see docs/specs/2026-11-13-Spec-WebDavCloudSync-v1.0.md 10).
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Subitem.h"
#include "net/WebDavClient.h"
#include "net/WebDavCredentials.h"
#include "service/CloudSyncService.h"

namespace {

service::SubscriptionRecord makeRecord(const std::string& id, const std::string& url) {
    service::SubscriptionRecord rec;
    rec.id = id;
    rec.url = url;
    rec.remarks = "remarks-" + id;
    rec.enabled = 1;
    rec.sort = 3;
    rec.autoUpdateInterval = 60;
    rec.updateTime = 1700000000LL;
    rec.preSocksPort = 10808;
    rec.memo = "memo-" + id;
    return rec;
}

} // namespace

// ---------------------------------------------------------------- decide()

TEST(CloudSyncTest, DecideReturnsPushWhenRemoteMissing) {
    EXPECT_EQ(service::CloudSyncService::decide(1700000000LL, 0),
              service::SyncDecision::Push);
}

TEST(CloudSyncTest, DecideReturnsPullWhenLocalMissing) {
    EXPECT_EQ(service::CloudSyncService::decide(0, 1700000000LL),
              service::SyncDecision::Pull);
}

TEST(CloudSyncTest, DecideRemoteNewerPulls) {
    EXPECT_EQ(service::CloudSyncService::decide(1700000000LL, 1700000060LL),
              service::SyncDecision::Pull);
}

TEST(CloudSyncTest, DecideLocalNewerPushes) {
    EXPECT_EQ(service::CloudSyncService::decide(1700000060LL, 1700000000LL),
              service::SyncDecision::Push);
}

TEST(CloudSyncTest, DecideWithinToleranceSkips) {
    EXPECT_EQ(service::CloudSyncService::decide(1700000000LL, 1700000001LL),
              service::SyncDecision::Skip);
    EXPECT_EQ(service::CloudSyncService::decide(1700000001LL, 1700000000LL),
              service::SyncDecision::Skip);
    EXPECT_EQ(service::CloudSyncService::decide(1700000000LL, 1700000000LL),
              service::SyncDecision::Skip);
}

// CloudSyncService::decide has no Conflict branch by design (it degrades to
// Push/Pull); the Conflict state exists for callers that cannot parse a date.
TEST(CloudSyncTest, DecideNegativeTimestampsDegradeToPull) {
    EXPECT_EQ(service::CloudSyncService::decide(-5, 1700000000LL),
              service::SyncDecision::Pull);
}

// ------------------------------------------------------------ HTTP dates

TEST(CloudSyncTest, ParseHttpDateHandlesImfFixdate) {
    // Wed, 21 Oct 2026 07:28:00 GMT
    const long long epoch = service::CloudSyncService::parseHttpDate(
            "Wed, 21 Oct 2026 07:28:00 GMT");
    ASSERT_GT(epoch, 0);
    const long long expected = 1792567680LL;  // 2026-10-21T07:28:00Z
    EXPECT_EQ(epoch, expected);
}

TEST(CloudSyncTest, ParseHttpDateRejectsGarbage) {
    EXPECT_EQ(service::CloudSyncService::parseHttpDate(""), 0);
    EXPECT_EQ(service::CloudSyncService::parseHttpDate("not a date"), 0);
    EXPECT_EQ(service::CloudSyncService::parseHttpDate("Wed, 99 Xyz 2026 07:28:00 GMT"), 0);
}

// ------------------------------------------------------------ multistatus

TEST(CloudSyncTest, ParseMultiStatusExtractsHrefAndMtime) {
    const std::string xml =
            "<?xml version=\"1.0\"?>"
            "<D:multistatus xmlns:D=\"DAV:\">"
            "<D:response><D:href>/dav/validproxy/config.json</D:href>"
            "<D:propstat><D:prop>"
            "<D:getlastmodified>Wed, 21 Oct 2026 07:28:00 GMT</D:getlastmodified>"
            "<D:getcontentlength>1234</D:getcontentlength>"
            "</D:prop></D:propstat></D:response>"
            "<D:response><D:href>/dav/validproxy/subscriptions.json</D:href>"
            "<D:propstat><D:prop>"
            "<D:getlastmodified>Thu, 22 Oct 2026 08:00:00 GMT</D:getlastmodified>"
            "<D:getcontentlength>42</D:getcontentlength>"
            "</D:prop></D:propstat></D:response>"
            "</D:multistatus>";

    const std::vector<net::DavEntry> entries = net::WebDavClient::parseMultiStatus(xml);
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].href, "/dav/validproxy/config.json");
    EXPECT_EQ(entries[0].contentLength, 1234);
    EXPECT_EQ(entries[0].lastModified, "Wed, 21 Oct 2026 07:28:00 GMT");
    EXPECT_EQ(entries[1].href, "/dav/validproxy/subscriptions.json");
    EXPECT_EQ(entries[1].contentLength, 42);
}

TEST(CloudSyncTest, ParseMultiStatusIgnoresMalformedXml) {
    EXPECT_TRUE(net::WebDavClient::parseMultiStatus("").empty());
    EXPECT_TRUE(net::WebDavClient::parseMultiStatus("<D:multistatus>").empty());
    EXPECT_TRUE(net::WebDavClient::parseMultiStatus("<D:response><D:href>/x")
                        .empty());
}

TEST(CloudSyncTest, NormalizeHrefStripsPercentEncoding) {
    EXPECT_EQ(net::WebDavClient::normalizeHref("https://dav.example.com/dav/a%20b.json"),
              "/dav/a b.json");
    EXPECT_EQ(net::WebDavClient::normalizeHref("/dav/%2Froot/config.json"),
              "/dav//root/config.json");
    EXPECT_EQ(net::WebDavClient::normalizeHref("/dav/plain.json"), "/dav/plain.json");
}

// -------------------------------------------------------------- URL building

TEST(CloudSyncTest, BuildRemoteUrlJoinsPaths) {
    EXPECT_EQ(net::WebDavClient::buildUrl("https://dav.example.com/dav/", "config.json"),
              "https://dav.example.com/dav/config.json");
    EXPECT_EQ(net::WebDavClient::buildUrl("https://dav.example.com/dav", "config.json"),
              "https://dav.example.com/dav/config.json");
    EXPECT_EQ(net::WebDavClient::buildUrl("https://dav.example.com/dav/",
                                          "/validproxy/subscriptions.json"),
              "https://dav.example.com/dav/validproxy/subscriptions.json");
    // Spaces must be percent-encoded, slashes must not.
    EXPECT_EQ(net::WebDavClient::buildUrl("https://dav.example.com/dav/", "a b/c.json"),
              "https://dav.example.com/dav/a%20b/c.json");
}

TEST(CloudSyncTest, RemotePathForTrimsSlashes) {
    EXPECT_EQ(service::CloudSyncService::remotePathFor("", "config.json"), "config.json");
    EXPECT_EQ(service::CloudSyncService::remotePathFor("validproxy", "config.json"),
              "validproxy/config.json");
    EXPECT_EQ(service::CloudSyncService::remotePathFor("/validproxy/", "config.json"),
              "validproxy/config.json");
}

// ------------------------------------------------------- subscriptions JSON

TEST(CloudSyncTest, EncodeSubscriptionsRoundTrips) {
    const std::vector<service::SubscriptionRecord> source = {
            makeRecord("1001", "https://example.com/a"),
            makeRecord("1002", "https://example.com/b"),
    };
    const std::string json = service::CloudSyncService::encodeSubscriptions(source);
    ASSERT_FALSE(json.empty());

    std::vector<service::SubscriptionRecord> decoded;
    std::string error;
    ASSERT_TRUE(service::CloudSyncService::decodeSubscriptions(json, decoded, error))
            << error;

    ASSERT_EQ(decoded.size(), 2u);
    for (std::size_t i = 0; i < source.size(); ++i) {
        EXPECT_EQ(decoded[i].id, source[i].id);
        EXPECT_EQ(decoded[i].url, source[i].url);
        EXPECT_EQ(decoded[i].remarks, source[i].remarks);
        EXPECT_EQ(decoded[i].enabled, source[i].enabled);
        EXPECT_EQ(decoded[i].sort, source[i].sort);
        EXPECT_EQ(decoded[i].autoUpdateInterval, source[i].autoUpdateInterval);
        EXPECT_EQ(decoded[i].updateTime, source[i].updateTime);
        EXPECT_EQ(decoded[i].preSocksPort, source[i].preSocksPort);
        EXPECT_EQ(decoded[i].memo, source[i].memo);
    }
}

TEST(CloudSyncTest, EncodeSubscriptionsContainsMetadata) {
    const std::string json =
            service::CloudSyncService::encodeSubscriptions({makeRecord("7", "u")});
    EXPECT_NE(json.find("\"version\""), std::string::npos);
    EXPECT_NE(json.find("\"exported_at\""), std::string::npos);
    EXPECT_NE(json.find("\"items\""), std::string::npos);
}

TEST(CloudSyncTest, DecodeSubscriptionsRejectsCorruptJson) {
    std::vector<service::SubscriptionRecord> out;
    std::string error;
    EXPECT_FALSE(service::CloudSyncService::decodeSubscriptions("{not json", out, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(service::CloudSyncService::decodeSubscriptions("[]", out, error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(service::CloudSyncService::decodeSubscriptions("{\"items\":1}", out, error));
    EXPECT_FALSE(error.empty());
}

TEST(CloudSyncTest, DecodeSubscriptionsAllowsMissingOptionalFields) {
    const std::string json =
            "{\"version\":1,\"items\":[{\"id\":\"42\",\"url\":\"https://x\"}]}";
    std::vector<service::SubscriptionRecord> out;
    std::string error;
    ASSERT_TRUE(service::CloudSyncService::decodeSubscriptions(json, out, error))
            << error;
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].id, "42");
    EXPECT_EQ(out[0].url, "https://x");
    EXPECT_EQ(out[0].enabled, 1);
    EXPECT_EQ(out[0].sort, 0);
    EXPECT_EQ(out[0].updateTime, 0);
}

TEST(CloudSyncTest, DecodeSubscriptionsSkipsEntriesWithoutId) {
    const std::string json = "{\"items\":[{\"url\":\"https://no-id\"},{\"id\":\"9\"}]}";
    std::vector<service::SubscriptionRecord> out;
    std::string error;
    ASSERT_TRUE(service::CloudSyncService::decodeSubscriptions(json, out, error))
            << error;
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].id, "9");
}

TEST(CloudSyncTest, SubitemConversionRoundTrips) {
    const service::SubscriptionRecord source = makeRecord("55", "https://example.com/z");
    const db::models::Subitem row = service::CloudSyncService::toSubitem(source);
    const service::SubscriptionRecord back = service::CloudSyncService::fromSubitem(row);

    EXPECT_EQ(back.id, source.id);
    EXPECT_EQ(back.url, source.url);
    EXPECT_EQ(back.remarks, source.remarks);
    EXPECT_EQ(back.enabled, source.enabled);
    EXPECT_EQ(back.sort, source.sort);
    EXPECT_EQ(back.autoUpdateInterval, source.autoUpdateInterval);
    EXPECT_EQ(back.updateTime, source.updateTime);
    EXPECT_EQ(back.preSocksPort, source.preSocksPort);
    EXPECT_EQ(back.memo, source.memo);
}

// ------------------------------------------------------------- credentials

TEST(CloudSyncTest, CredentialsBlobRoundTrips) {
    const std::string key = "unit-test-key";
    const std::string blob =
            net::WebDavCredentialsStore::encodeBlob("user@example.com", "app-pass-123", key);

    std::string user;
    std::string password;
    ASSERT_TRUE(net::WebDavCredentialsStore::decodeBlob(blob, key, user, password));
    EXPECT_EQ(user, "user@example.com");
    EXPECT_EQ(password, "app-pass-123");
}

TEST(CloudSyncTest, CredentialsBlobRejectsWrongKey) {
    const std::string blob = net::WebDavCredentialsStore::encodeBlob(
            "user@example.com", "pw", "the-correct-provisioning-key");
    std::string user;
    std::string password;
    const bool ok = net::WebDavCredentialsStore::decodeBlob(
            blob, "a-completely-different-key", user, password);
    // A wrong key may either fail to split or yield garbage; it must never yield
    // the original password.
    EXPECT_TRUE(!ok || password != "pw");
    EXPECT_TRUE(!ok || user != "user@example.com");
}

TEST(CloudSyncTest, CredentialsBlobRejectsNonBase64) {
    std::string user;
    std::string password;
    EXPECT_FALSE(net::WebDavCredentialsStore::decodeBlob("!!!not-base64!!!", "k", user,
                                                         password));
}

TEST(CloudSyncTest, Base64RoundTripsBinarySafeInput) {
    // Explicit bytes: "\x02abc" would be parsed as one oversized hex escape.
    const char rawBytes[] = {'\x00', '\x01', '\x02', 'a', 'b', 'c', '\0', '\x7f'};
    const std::string raw(rawBytes, 8);
    const std::string encoded = net::WebDavCredentialsStore::base64Encode(raw);
    std::string decoded;
    ASSERT_TRUE(net::WebDavCredentialsStore::base64Decode(encoded, decoded));
    EXPECT_EQ(decoded, raw);
}

TEST(CloudSyncTest, CredentialsLoadFailsWhenNotProvisioned) {
    // In CI / a clean checkout resources/webdav_secret.inc is absent, so the
    // blob is empty and loading must fail with an actionable message.
    net::WebDavCredentials creds;
    std::string error;
    const bool ok = net::WebDavCredentialsStore::load(creds, error);
    if (!ok) {
        EXPECT_FALSE(error.empty());
        EXPECT_FALSE(creds.usable());
    } else {
        EXPECT_TRUE(creds.usable()) << "a provisioned blob must yield usable credentials";
    }
}
