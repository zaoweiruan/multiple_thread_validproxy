// test_template amlateDecomposer.cpp - TemplateDecomposer unit tests
#include <gtest/gtest.h>
#include <sqlite3.h>
#include <string>
#include <vector>
#include <memory>
#include <optional>

#include "config/TemplateDecomposer.h"
#include "config/ComponentBundle.h"
#include "config/ComponentBundleDAO.h"
#include "FullConfigTemplateDAO.h"

using namespace config;

namespace {

// Minimal in-memory DB with template data
class TemplateDecomposerTest : public testing::Test {
protected:
    sqlite3* db = nullptr;
    std::unique_ptr<TemplateDecomposer> composer;

    void SetUp() override {
        ASSERT_EQ(SQLITE_OK, sqlite3_open(":memory:", &db));
        composer.reset(new TemplateDecomposer(db));
        // Create FullConfigTemplateItem table — 必须包含 DAO SELECT/INSERT 使用的全部列
        const char* sql =
            "CREATE TABLE FullConfigTemplateItem ("
            "Id TEXT PRIMARY KEY, "
            "ParentId TEXT, "
            "Remarks TEXT, "
            "Enabled INTEGER DEFAULT 0, "
            "CoreType INTEGER DEFAULT 0, "
            "Config TEXT, "
            "TunConfig TEXT, "
            "AddProxyOnly INTEGER DEFAULT 0, "
            "ProxyDetour TEXT, "
            "BundleChain TEXT, "
            "TemplateType TEXT, "
            "BundleVersion TEXT);";
        char* err = nullptr;
        ASSERT_EQ(SQLITE_OK, sqlite3_exec(db, sql, nullptr, nullptr, &err));
        if (err) sqlite3_free(err);
    }

    void TearDown() override {
        composer.reset();
        sqlite3_close(db);
    }

    void insertTemplate(const std::string& id, int coreType,
                        const std::string& configJson) {
        sqlite3_stmt* stmt = nullptr;
        ASSERT_EQ(SQLITE_OK, sqlite3_prepare_v2(
            db,
            "INSERT INTO FullConfigTemplateItem "
            "(Id, CoreType, Config) VALUES (?, ?, ?)",
            -1, &stmt, nullptr));
        ASSERT_EQ(SQLITE_OK, sqlite3_bind_text(stmt, 1, id.c_str(), -1,
                                                SQLITE_TRANSIENT));
        ASSERT_EQ(SQLITE_OK, sqlite3_bind_int(stmt, 2, coreType));
        ASSERT_EQ(SQLITE_OK, sqlite3_bind_text(stmt, 3, configJson.c_str(),
                                                -1, SQLITE_TRANSIENT));
        ASSERT_EQ(SQLITE_DONE, sqlite3_step(stmt));
        sqlite3_finalize(stmt);
    }

    // Build a standard Xray template config JSON
    static std::string makeXrayTestConfig() {
        return R"({
            "api": {"tag": "api", "listen": "127.0.0.1:10085",
                     "services": ["HandlerService","LoggerService"]},
            "stats": {},
            "policy": {"levels": {"0": {"statsUserUplink": true}}},
            "inbounds": [{"tag": "socks", "listen": "127.0.0.1",
                          "port": 10808, "protocol": "mixed",
                          "settings": {"auth": "noauth","udp": true},
                          "sniffing": {"enabled": true}}],
            "routing": {"domainStrategy": "AsIs",
                        "rules": [{"type": "field","inboundTag": ["api"],
                                    "outboundTag": "api"}]}
        })";
    }

    static std::string makeXrayBaseConfig() {
        return R"({
            "log": {"loglevel": "warning"},
            "dns": {"tag": "dns", "servers": ["https://cloudflare-dns.com/dns-query"]}
        })";
    }

    static std::string makeXrayDefaultConfig() {
        return R"({
            "inbounds": [{"tag": "socks", "listen": "127.0.0.1",
                          "port": "${socksPort}", "protocol": "mixed"}],
            "routing": {"domainStrategy": "AsIs",
                        "rules": [{"type": "field", "port": "443",
                                    "network": "udp", "outboundTag": "block"}]}
        })";
    }
};

// ============================================================
// sectionToComponentType
// ============================================================

TEST_F(TemplateDecomposerTest, SectionToComponentType_Mapped) {
    EXPECT_EQ(TemplateDecomposer::sectionToComponentType("log"),
              ComponentType::LOG);
    EXPECT_EQ(TemplateDecomposer::sectionToComponentType("dns"),
              ComponentType::DNS);
    EXPECT_EQ(TemplateDecomposer::sectionToComponentType("api"),
              ComponentType::API);
    EXPECT_EQ(TemplateDecomposer::sectionToComponentType("stats"),
              ComponentType::STATS);
    EXPECT_EQ(TemplateDecomposer::sectionToComponentType("policy"),
              ComponentType::POLICY);
    EXPECT_EQ(TemplateDecomposer::sectionToComponentType("inbounds"),
              ComponentType::INBOUND);
    EXPECT_EQ(TemplateDecomposer::sectionToComponentType("outbounds"),
              ComponentType::OUTBOUND);
    EXPECT_EQ(TemplateDecomposer::sectionToComponentType("routing"),
              ComponentType::ROUTING);
    EXPECT_EQ(TemplateDecomposer::sectionToComponentType("observatory"),
              ComponentType::OBSERVATORY);
    EXPECT_EQ(TemplateDecomposer::sectionToComponentType("burstObservatory"),
              ComponentType::BURST_OBSERVATORY);
    EXPECT_EQ(TemplateDecomposer::sectionToComponentType("geodata"),
              ComponentType::GEODATA);
    EXPECT_EQ(TemplateDecomposer::sectionToComponentType("version"),
              ComponentType::VERSION);
    EXPECT_EQ(TemplateDecomposer::sectionToComponentType("fakedns"),
              ComponentType::FAKEDNS);
    EXPECT_EQ(TemplateDecomposer::sectionToComponentType("metrics"),
              ComponentType::METRICS);
    EXPECT_EQ(TemplateDecomposer::sectionToComponentType("env"),
              ComponentType::ENV);
}

TEST_F(TemplateDecomposerTest, SectionToComponentType_Unknown) {
    EXPECT_EQ(TemplateDecomposer::sectionToComponentType("unknown"),
              std::nullopt);
    EXPECT_EQ(TemplateDecomposer::sectionToComponentType(""),
              std::nullopt);
    EXPECT_EQ(TemplateDecomposer::sectionToComponentType("custom_section"),
              std::nullopt);
}

// ============================================================
// inferTemplateType
// ============================================================

TEST_F(TemplateDecomposerTest, InferTemplateType_Base) {
    EXPECT_EQ(TemplateDecomposer::inferTemplateType("xray_base_001"),
              TemplateType::BASE);
}

TEST_F(TemplateDecomposerTest, InferTemplateType_Test) {
    EXPECT_EQ(TemplateDecomposer::inferTemplateType("xray_test_001"),
              TemplateType::TEST);
}

TEST_F(TemplateDecomposerTest, InferTemplateType_Pool) {
    EXPECT_EQ(TemplateDecomposer::inferTemplateType("xray_pool_001"),
              TemplateType::POOL);
}

TEST_F(TemplateDecomposerTest, InferTemplateType_Default_Standalone) {
    EXPECT_EQ(TemplateDecomposer::inferTemplateType("xray_default_001"),
              TemplateType::STANDALONE);
}

TEST_F(TemplateDecomposerTest, InferTemplateType_Unknown_DefaultsToBase) {
    EXPECT_EQ(TemplateDecomposer::inferTemplateType("xray_unknown_001"),
              TemplateType::BASE);
}

// ============================================================
// decompose - xray_test_001 (api+stats+policy+inbounds+routing)
// ============================================================

TEST_F(TemplateDecomposerTest, Decompose_XrayTest_Components) {
    insertTemplate("xray_test_001", 2, makeXrayTestConfig());

    auto result = composer->decompose("xray_test_001");
    ASSERT_TRUE(result.success);
    ASSERT_FALSE(result.error.empty() == false); // no error

    EXPECT_EQ(result.bundle.id, "xray_test_001");
    EXPECT_EQ(result.bundle.coreType, CoreType::XRAY);
    EXPECT_EQ(result.bundle.templateType, TemplateType::TEST);
    EXPECT_EQ(result.bundle.version, 1);
    EXPECT_TRUE(result.bundle.enabled);

    // Should have: api, stats, policy, 1 inbound, routing = 5 components
    ASSERT_EQ(result.bundle.components.size(), 5u);

    // Verify each component type
    int apiCount = 0, statsCount = 0, policyCount = 0;
    int inboundCount = 0, routingCount = 0;
    for (const auto& comp : result.bundle.components) {
        switch (comp.type) {
            case ComponentType::API: apiCount++; break;
            case ComponentType::STATS: statsCount++; break;
            case ComponentType::POLICY: policyCount++; break;
            case ComponentType::INBOUND: inboundCount++; break;
            case ComponentType::ROUTING: routingCount++; break;
            default: break;
        }
    }
    EXPECT_EQ(apiCount, 1);
    EXPECT_EQ(statsCount, 1);
    EXPECT_EQ(policyCount, 1);
    EXPECT_EQ(inboundCount, 1);
    EXPECT_EQ(routingCount, 1);
}

// ============================================================
// decompose - xray_base_001 (log+dns)
// ============================================================

TEST_F(TemplateDecomposerTest, Decompose_XrayBase_Components) {
    insertTemplate("xray_base_001", 2, makeXrayBaseConfig());

    auto result = composer->decompose("xray_base_001");
    ASSERT_TRUE(result.success);

    EXPECT_EQ(result.bundle.id, "xray_base_001");
    EXPECT_EQ(result.bundle.coreType, CoreType::XRAY);
    EXPECT_EQ(result.bundle.templateType, TemplateType::BASE);

    // Should have: log + dns = 2 components
    ASSERT_EQ(result.bundle.components.size(), 2u);

    bool hasLog = false, hasDns = false;
    for (const auto& comp : result.bundle.components) {
        if (comp.type == ComponentType::LOG) hasLog = true;
        if (comp.type == ComponentType::DNS) hasDns = true;
    }
    EXPECT_TRUE(hasLog);
    EXPECT_TRUE(hasDns);
}

// ============================================================
// decompose - xray_default_001 (inbounds+routing)
// ============================================================

TEST_F(TemplateDecomposerTest, Decompose_XrayDefault_Components) {
    insertTemplate("xray_default_001", 2, makeXrayDefaultConfig());

    auto result = composer->decompose("xray_default_001");
    ASSERT_TRUE(result.success);

    EXPECT_EQ(result.bundle.templateType, TemplateType::STANDALONE);

    // Should have: 1 inbound + routing = 2 components
    ASSERT_EQ(result.bundle.components.size(), 2u);
}

// ============================================================
// decompose - SingBox template
// ============================================================

TEST_F(TemplateDecomposerTest, Decompose_SingBox_CoreType) {
    insertTemplate("singbox_test_001", 24, makeXrayTestConfig());

    auto result = composer->decompose("singbox_test_001");
    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.bundle.coreType, CoreType::SINGBOX);
}

// ============================================================
// decompose - template not found
// ============================================================

TEST_F(TemplateDecomposerTest, Decompose_NotFound) {
    auto result = composer->decompose("nonexistent_template");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

// ============================================================
// decompose - empty config
// ============================================================

TEST_F(TemplateDecomposerTest, Decompose_EmptyConfig) {
    insertTemplate("empty_tpl", 2, "");
    auto result = composer->decompose("empty_tpl");
    EXPECT_FALSE(result.success);
}

// ============================================================
// decompose - unknown sections are skipped
// ============================================================

TEST_F(TemplateDecomposerTest, Decompose_UnknownSectionsSkipped) {
    std::string json = R"({
        "log": {"loglevel": "info"},
        "custom_section": {"foo": "bar"},
        "dns": {"tag": "dns", "servers": ["1.1.1.1"]}
    })";
    insertTemplate("xray_mixed_001", 2, json);

    auto result = composer->decompose("xray_mixed_001");
    ASSERT_TRUE(result.success);

    // Should have: log + dns = 2 components (custom_section skipped)
    EXPECT_EQ(result.bundle.components.size(), 2u);
}

// ============================================================
// decompose - multiple inbounds
// ============================================================

TEST_F(TemplateDecomposerTest, Decompose_MultipleInbounds) {
    std::string json = R"({
        "inbounds": [
            {"tag": "socks", "listen": "127.0.0.1", "port": 10808,
             "protocol": "mixed"},
            {"tag": "http", "listen": "127.0.0.1", "port": 10809,
             "protocol": "http"}
        ],
        "routing": {"rules": []}
    })";
    insertTemplate("xray_multi_inbound_001", 2, json);

    auto result = composer->decompose("xray_multi_inbound_001");
    ASSERT_TRUE(result.success);

    // Should have: 2 inbounds + routing = 3 components
    EXPECT_EQ(result.bundle.components.size(), 3u);

    // Verify inbound component ids (tag)
    int inboundIdx = 0;
    for (const auto& comp : result.bundle.components) {
        if (comp.type == ComponentType::INBOUND) {
            EXPECT_EQ(comp.id, inboundIdx == 0 ? "socks" : "http");
            inboundIdx++;
        }
    }
}

// ============================================================
// decompose - outbounds array
// ============================================================

TEST_F(TemplateDecomposerTest, Decompose_Outbounds) {
    std::string json = R"({
        "inbounds": [{"tag": "socks", "listen": "127.0.0.1",
                       "port": 10808, "protocol": "mixed"}],
        "outbounds": [
            {"tag": "proxy", "protocol": "vmess",
             "settings": {"vnext": [{"address": "1.2.3.4", "port": 443,
                                     "users": [{"id": "abc", "alterId": 0,
                                                "security": "auto"}]}]}},
            {"tag": "direct", "protocol": "freedom"},
            {"tag": "block", "protocol": "blackhole"}
        ],
        "routing": {"rules": []}
    })";
    insertTemplate("xray_pool_001", 2, json);

    auto result = composer->decompose("xray_pool_001");
    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.bundle.templateType, TemplateType::POOL);

    // Should have: 1 inbound + 3 outbounds + routing = 5 components
    EXPECT_EQ(result.bundle.components.size(), 5u);
}

// ============================================================
// decompose - all 15 component types
// ============================================================

TEST_F(TemplateDecomposerTest, Decompose_AllComponentTypes) {
    std::string json = R"({
        "env": {"foo": "bar"},
        "log": {"loglevel": "info"},
        "api": {"tag": "api", "listen": "127.0.0.1:10085"},
        "dns": {"tag": "dns", "servers": ["1.1.1.1"]},
        "routing": {"rules": []},
        "policy": {"levels": {}},
        "inbounds": [{"tag": "socks", "listen": "127.0.0.1",
                       "port": 10808, "protocol": "mixed"}],
        "outbounds": [{"tag": "proxy", "protocol": "vmess"}],
        "stats": {},
        "fakedns": {"ip": "198.18.0.2", "poolSize": 100},
        "metrics": {"bindAddress": "127.0.0.1", "bindPort": 8090},
        "observatory": {"subject": [{"tag": "socks"}]},
        "burstObservatory": {"subject": []},
        "geodata": {"downloadUrl": "https://example.com"},
        "version": {"build": "test"}
    })";
    insertTemplate("xray_full_001", 2, json);

    auto result = composer->decompose("xray_full_001");
    ASSERT_TRUE(result.success);

    // Should have all 15 component types
    EXPECT_EQ(result.bundle.components.size(), 15u);

    // Verify each type appears exactly once
    for (int t = 0; t < 15; t++) {
        int count = 0;
        for (const auto& comp : result.bundle.components) {
            if (static_cast<int>(comp.type) == t) count++;
        }
        EXPECT_EQ(count, 1)
            << "ComponentType " << t << " should appear exactly once";
    }
}

// ============================================================
// decomposeAll - multiple templates
// ============================================================

TEST_F(TemplateDecomposerTest, DecomposeAll_MultipleTemplates) {
    insertTemplate("xray_base_001", 2, makeXrayBaseConfig());
    insertTemplate("xray_test_001", 2, makeXrayTestConfig());
    insertTemplate("singbox_base_001", 24, makeXrayBaseConfig());

    auto results = composer->decomposeAll();
    ASSERT_EQ(results.size(), 3u);

    int successCount = 0;
    for (const auto& r : results) {
        if (r.success) successCount++;
    }
    EXPECT_EQ(successCount, 3);
}

// ============================================================
// decomposeAndSave - save to ComponentBundle table
// ============================================================

TEST_F(TemplateDecomposerTest, DecomposeAndSave_CreatesBundles) {
    insertTemplate("xray_base_001", 2, makeXrayBaseConfig());
    insertTemplate("xray_test_001", 2, makeXrayTestConfig());

    // Ensure ComponentBundle table exists
    ComponentBundleDAO dao(db);
    ASSERT_TRUE(dao.createTableIfNotExists());

    int saved = composer->decomposeAndSave(false);
    EXPECT_EQ(saved, 2);

    // Verify bundles were saved
    EXPECT_TRUE(dao.exists("xray_base_001"));
    EXPECT_TRUE(dao.exists("xray_test_001"));

    // Verify bundle content
    auto bundleOpt = dao.getById("xray_base_001");
    ASSERT_TRUE(bundleOpt);
    EXPECT_EQ(bundleOpt->coreType, CoreType::XRAY);
    EXPECT_EQ(bundleOpt->templateType, TemplateType::BASE);
    EXPECT_EQ(bundleOpt->components.size(), 2u);
}

TEST_F(TemplateDecomposerTest, DecomposeAndSave_NoOverwriteSkips) {
    insertTemplate("xray_base_001", 2, makeXrayBaseConfig());

    ComponentBundleDAO dao(db);
    dao.createTableIfNotExists();

    // First save
    int saved1 = composer->decomposeAndSave(false);
    EXPECT_EQ(saved1, 1);

    // Second save without overwrite should skip
    int saved2 = composer->decomposeAndSave(false);
    EXPECT_EQ(saved2, 0);
}

TEST_F(TemplateDecomposerTest, DecomposeAndSave_Overwrite) {
    insertTemplate("xray_base_001", 2, makeXrayBaseConfig());

    ComponentBundleDAO dao(db);
    dao.createTableIfNotExists();

    // First save
    composer->decomposeAndSave(false);

    // Second save with overwrite should update
    int saved = composer->decomposeAndSave(true);
    EXPECT_EQ(saved, 1);

    // Verify bundle still exists
    EXPECT_TRUE(dao.exists("xray_base_001"));
}

// ============================================================
// JSON params preservation
// ============================================================

TEST_F(TemplateDecomposerTest, Decompose_ParamsPreserved) {
    std::string json = R"({
        "log": {"loglevel": "warning", "access": "/var/log/xray.log"},
        "dns": {"tag": "dns", "servers": ["https://cloudflare-dns.com/dns-query",
                                            "https://dns.google/dns-query"]}
    })";
    insertTemplate("xray_params_001", 2, json);

    auto result = composer->decompose("xray_params_001");
    ASSERT_TRUE(result.success);

    // Verify LOG component params
    for (const auto& comp : result.bundle.components) {
        if (comp.type == ComponentType::LOG) {
            auto it = comp.params.find("loglevel");
            ASSERT_NE(it, comp.params.end());
            auto lvl = it->second.as_string();
            EXPECT_STREQ(lvl.c_str(), "warning");

            it = comp.params.find("access");
            ASSERT_NE(it, comp.params.end());
            auto acc = it->second.as_string();
            EXPECT_STREQ(acc.c_str(), "/var/log/xray.log");
        }
        if (comp.type == ComponentType::DNS) {
            auto it = comp.params.find("servers");
            ASSERT_NE(it, comp.params.end());
            ASSERT_TRUE(it->second.is_array());
            EXPECT_EQ(it->second.as_array().size(), 2u);
        }
    }
}

TEST_F(TemplateDecomposerTest, Decompose_NestedParamsPreserved) {
    std::string json = R"({
        "inbounds": [{"tag": "socks", "listen": "127.0.0.1",
                       "port": 10808, "protocol": "mixed",
                       "settings": {"auth": "noauth", "udp": true,
                                     "allowTransparent": false},
                       "sniffing": {"enabled": true,
                                     "destOverride": ["http", "tls", "quic"]}}],
        "routing": {"domainStrategy": "AsIs",
                     "rules": [{"type": "field", "inboundTag": ["api"],
                                  "outboundTag": "api"}]}
    })";
    insertTemplate("xray_nested_001", 2, json);

    auto result = composer->decompose("xray_nested_001");
    ASSERT_TRUE(result.success);

    // Verify nested params in inbound
    for (const auto& comp : result.bundle.components) {
        if (comp.type == ComponentType::INBOUND) {
            auto it = comp.params.find("settings");
            ASSERT_NE(it, comp.params.end());
            ASSERT_TRUE(it->second.is_object());

            auto authIt = it->second.as_object().find("auth");
            ASSERT_NE(authIt, it->second.as_object().end());
            auto authStr = authIt->value().as_string();
            EXPECT_STREQ(authStr.c_str(), "noauth");

            auto udpIt = it->second.as_object().find("udp");
            ASSERT_NE(udpIt, it->second.as_object().end());
            EXPECT_EQ(udpIt->value().as_bool(), true);
        }
        if (comp.type == ComponentType::ROUTING) {
            auto it = comp.params.find("rules");
            ASSERT_NE(it, comp.params.end());
            ASSERT_TRUE(it->second.is_array());
            EXPECT_EQ(it->second.as_array().size(), 1u);
        }
    }
}

} // namespace
