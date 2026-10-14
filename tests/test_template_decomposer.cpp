// test_template amlateDecomposer.cpp - TemplateDecomposer unit tests
#include <gtest/gtest.h>
#include <sqlite3.h>
#include <string>
#include <vector>
#include <set>
#include <sstream>
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

    // 直接写模板的 bundleChain（用于构造「链引用了已删除包」等场景）
    void setTemplateChain(const std::string& id, const std::string& chainJson) {
        sqlite3_stmt* stmt = nullptr;
        ASSERT_EQ(SQLITE_OK, sqlite3_prepare_v2(
            db,
            "UPDATE FullConfigTemplateItem SET BundleChain = ? WHERE Id = ?",
            -1, &stmt, nullptr));
        ASSERT_EQ(SQLITE_OK, sqlite3_bind_text(stmt, 1, chainJson.c_str(), -1,
                                                SQLITE_TRANSIENT));
        ASSERT_EQ(SQLITE_OK, sqlite3_bind_text(stmt, 2, id.c_str(), -1,
                                                SQLITE_TRANSIENT));
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

    static std::string makePlaceholderConfig() {
        return R"({
            "api": {"tag": "api", "listen": "127.0.0.1:${apiPort}",
                    "services": ["HandlerService"]},
            "inbounds": [{"tag": "socks", "listen": "127.0.0.1",
                          "port": "${socksPort}", "protocol": "mixed"}]
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
    EXPECT_EQ(result.role, TemplateType::TEST);
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
    EXPECT_EQ(result.role, TemplateType::BASE);

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

    EXPECT_EQ(result.role, TemplateType::STANDALONE);

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
    EXPECT_EQ(result.role, TemplateType::POOL);

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


// ---------------------------------------------------------------------
// planMinimalBundles / rebuildBundlesAndChains:
// 组件包按「代理类型 × 组件类型」拆到最小粒度
// ---------------------------------------------------------------------

// 仅含 routing、规则数不同的配置（用于制造同类型内容不一致）
static std::string makeRoutingOnlyCfg(int ruleCount) {
    std::ostringstream os;
    os << R"({"routing": {"domainStrategy": "AsIs", "rules": [)";
    for (int i = 0; i < ruleCount; ++i) {
        if (i > 0) os << ",";
        os << R"({"type":"field","inboundTag":["api"],"outboundTag":"api"})";
    }
    os << "]}}";
    return os.str();
}

TEST_F(TemplateDecomposerTest, PlanMinimalBundles_SplitsByCoreAndType) {
    insertTemplate("xray_base_001", 2, makeXrayBaseConfig());
    insertTemplate("xray_test_001", 2, makeXrayTestConfig());

    const TemplateDecomposer::BundlePlan plan = composer->planMinimalBundles();

    std::set<std::string> ids;
    for (size_t i = 0; i < plan.bundles.size(); ++i) {
        ids.insert(plan.bundles[i].id);
        // 每个包只含一个组件
        EXPECT_EQ(1u, plan.bundles[i].components.size()) << plan.bundles[i].id;
    }

    // 每个组件类型仅一个角色使用 → 共享包，不带角色后缀
    EXPECT_EQ(1u, ids.count("xray_LOG"));
    EXPECT_EQ(1u, ids.count("xray_DNS"));
    EXPECT_EQ(1u, ids.count("xray_API"));
    EXPECT_EQ(1u, ids.count("xray_POLICY"));
    EXPECT_EQ(1u, ids.count("xray_INBOUND"));
    EXPECT_EQ(1u, ids.count("xray_STATS"));
    EXPECT_EQ(1u, ids.count("xray_ROUTING"));
    EXPECT_EQ(7u, plan.bundles.size());
}

TEST_F(TemplateDecomposerTest, PlanMinimalBundles_SharedBundleAcrossRoles) {
    // 两个角色的 INBOUND 内容完全一致 → 合并为同一个包
    const std::string cfg = R"({"inbounds":[{"tag":"socks","port":10808}]})";
    insertTemplate("xray_test_001", 2, cfg);
    insertTemplate("xray_default_001", 2, cfg);

    const TemplateDecomposer::BundlePlan plan = composer->planMinimalBundles();
    ASSERT_EQ(1u, plan.bundles.size());
    EXPECT_EQ("xray_INBOUND", plan.bundles[0].id);

    // 共享包只产出一次，不得按角色重复
    std::set<std::string> ids;
    for (size_t i = 0; i < plan.bundles.size(); ++i) {
        EXPECT_TRUE(ids.insert(plan.bundles[i].id).second) << plan.bundles[i].id;
    }

    // 两个模板都引用这同一个包
    ASSERT_EQ(1u, plan.chains.count("xray_test_001"));
    ASSERT_EQ(1u, plan.chains.count("xray_default_001"));
    EXPECT_EQ("xray_INBOUND", plan.chains.at("xray_test_001")[0]);
    EXPECT_EQ("xray_INBOUND", plan.chains.at("xray_default_001")[0]);
}

TEST_F(TemplateDecomposerTest, PlanMinimalBundles_RoutingSplitByRole) {
    insertTemplate("xray_pool_001", 2, makeRoutingOnlyCfg(6));
    insertTemplate("xray_default_001", 2, makeRoutingOnlyCfg(3));
    insertTemplate("xray_test_001", 2, makeRoutingOnlyCfg(1));

    const TemplateDecomposer::BundlePlan plan = composer->planMinimalBundles();

    // 同一 (Xray, ROUTING) 内容不一致 → 按角色拆成 3 个包
    ASSERT_EQ(3u, plan.bundles.size());
    std::set<std::string> ids;
    for (size_t i = 0; i < plan.bundles.size(); ++i) {
        ids.insert(plan.bundles[i].id);
    }
    EXPECT_EQ(1u, ids.count("xray_ROUTING_pool"));
    EXPECT_EQ(1u, ids.count("xray_ROUTING_standalone"));
    EXPECT_EQ(1u, ids.count("xray_ROUTING_test"));

    // 各模板只引用自己角色对应的包
    EXPECT_EQ("xray_ROUTING_pool", plan.chains.at("xray_pool_001")[0]);
    EXPECT_EQ("xray_ROUTING_standalone", plan.chains.at("xray_default_001")[0]);
    EXPECT_EQ("xray_ROUTING_test", plan.chains.at("xray_test_001")[0]);
}

TEST_F(TemplateDecomposerTest, PlanMinimalBundles_PoolChainIncludesLogDns) {
    insertTemplate("xray_base_001", 2, makeXrayBaseConfig());
    insertTemplate("pool_xray_001", 2, makeRoutingOnlyCfg(2));

    const TemplateDecomposer::BundlePlan plan = composer->planMinimalBundles();

    const std::vector<std::string>& ids = plan.chains.at("pool_xray_001");
    // LOG(1) DNS(3) ROUTING(4) → 按 ComponentType 枚举序
    ASSERT_EQ(3u, ids.size());
    EXPECT_EQ("xray_LOG", ids[0]);
    EXPECT_EQ("xray_DNS", ids[1]);
    EXPECT_EQ("xray_ROUTING", ids[2]);

    // base 模板的链只含自身组件，不含 ROUTING
    const std::vector<std::string>& baseIds = plan.chains.at("xray_base_001");
    ASSERT_EQ(2u, baseIds.size());
    EXPECT_EQ("xray_LOG", baseIds[0]);
    EXPECT_EQ("xray_DNS", baseIds[1]);
}

TEST_F(TemplateDecomposerTest, PlanMinimalBundles_NoDefaultInjection) {
    // test 模板不含 LOG/DNS → 链中不得凭空出现 base 的包
    insertTemplate("xray_base_001", 2, makeXrayBaseConfig());
    insertTemplate("xray_test_001", 2, makeXrayTestConfig());

    const TemplateDecomposer::BundlePlan plan = composer->planMinimalBundles();
    const std::vector<std::string>& ids = plan.chains.at("xray_test_001");
    for (size_t i = 0; i < ids.size(); ++i) {
        EXPECT_NE("xray_LOG", ids[i]);
        EXPECT_NE("xray_DNS", ids[i]);
    }
}

TEST_F(TemplateDecomposerTest, RebuildBundlesAndChains_WritesBundlesAndChains) {
    insertTemplate("xray_base_001", 2, makeXrayBaseConfig());
    insertTemplate("pool_xray_001", 2, makeRoutingOnlyCfg(2));

    const int saved = composer->rebuildBundlesAndChains();
    EXPECT_EQ(3, saved);

    ComponentBundleDAO dao(db);
    EXPECT_EQ(3u, dao.getAll().size());

    db::FullConfigTemplateDAO tdao(db);
    std::optional<db::FullConfigTemplateItem> tpl = tdao.getById("pool_xray_001");
    ASSERT_TRUE(tpl.has_value());
    EXPECT_EQ("bundleChain", tpl->templateType);

    boost::json::value v = boost::json::parse(tpl->bundleChain);
    ASSERT_TRUE(v.is_object());
    const boost::json::array& arr = v.as_object().at("bundleIds").as_array();
    ASSERT_EQ(3u, arr.size());
    EXPECT_EQ("xray_LOG", std::string(arr[0].as_string().c_str()));
    EXPECT_EQ("xray_DNS", std::string(arr[1].as_string().c_str()));
    EXPECT_EQ("xray_ROUTING", std::string(arr[2].as_string().c_str()));

    // 版本快照已写入
    EXPECT_FALSE(tpl->bundleVersion.empty());
}

TEST_F(TemplateDecomposerTest, RebuildBundlesAndChains_RemovesStaleBundles) {
    // 先按旧模型（模板名包）落库，再重建 → 旧包应被清除
    insertTemplate("xray_base_001", 2, makeXrayBaseConfig());

    EXPECT_EQ(1, composer->decomposeAndSave(false));
    ComponentBundleDAO dao(db);
    ASSERT_EQ(1u, dao.getAll().size());
    EXPECT_EQ("xray_base_001", dao.getAll()[0].id);

    const int saved = composer->rebuildBundlesAndChains();
    EXPECT_EQ(2, saved);  // xray_LOG + xray_DNS

    const std::vector<ComponentBundle> after = dao.getAll();
    EXPECT_EQ(2u, after.size());
    EXPECT_FALSE(dao.exists("xray_base_001"));
}

TEST_F(TemplateDecomposerTest, SyncConfigFromChain_MaterializesConfigFromBundles) {
    // seed base(LOG+DNS) 与 test(API/POLICY/INBOUND/STATS/ROUTING)
    insertTemplate("xray_base_001", 2, makeXrayBaseConfig());
    insertTemplate("xray_test_001", 2, makeXrayTestConfig());
    ASSERT_EQ(7, composer->rebuildBundlesAndChains());

    // Config 列应被物化为链解析结果
    db::FullConfigTemplateDAO dao(db);
    std::optional<db::FullConfigTemplateItem> base = dao.getById("xray_base_001");
    ASSERT_TRUE(base.has_value());
    const boost::json::value baseCfg = boost::json::parse(base->config);
    ASSERT_TRUE(baseCfg.is_object());
    EXPECT_EQ(1u, baseCfg.as_object().count("log"));
    EXPECT_EQ(1u, baseCfg.as_object().count("dns"));
    EXPECT_EQ(0u, baseCfg.as_object().count("routing"));
}

TEST_F(TemplateDecomposerTest, SyncConfigFromChain_PropagatesBundleChangeToAllTemplates) {
    insertTemplate("xray_base_001", 2, makeXrayBaseConfig());
    insertTemplate("xray_test_001", 2, makeXrayTestConfig());
    ASSERT_EQ(7, composer->rebuildBundlesAndChains());

    // 修改被两个模板共同引用的 xray_LOG（loglevel warning -> error）
    ComponentBundleDAO bdao(db);
    std::optional<ComponentBundle> log = bdao.getById("xray_LOG");
    ASSERT_TRUE(log.has_value());
    ASSERT_EQ(1u, log->components.size());
    ASSERT_EQ(1u, log->components[0].params.count("loglevel"));
    log->components[0].params["loglevel"] = boost::json::value(std::string("error"));
    ASSERT_TRUE(bdao.save(*log));

    // 只同步引用了 xray_LOG 的模板
    EXPECT_EQ(1, composer->syncConfigsFromChains("xray_LOG"));

    db::FullConfigTemplateDAO dao(db);
    std::optional<db::FullConfigTemplateItem> base = dao.getById("xray_base_001");
    ASSERT_TRUE(base.has_value());
    const boost::json::value cfg = boost::json::parse(base->config);
    ASSERT_TRUE(cfg.is_object());
    ASSERT_EQ(1u, cfg.as_object().count("log"));
    const boost::json::value& logObj = cfg.as_object().at("log");
    ASSERT_TRUE(logObj.is_object());
    EXPECT_EQ("error", std::string(logObj.as_object().at("loglevel").as_string().c_str()));
}

TEST_F(TemplateDecomposerTest, SyncConfigFromChain_NoChainLeavesConfigUntouched) {
    // 无链模板不应被物化（Config 由人工维护）
    insertTemplate("manual_001", 2, R"({"log":{"loglevel":"debug"}})");

    EXPECT_EQ(0, composer->syncConfigsFromChains());

    db::FullConfigTemplateDAO dao(db);
    std::optional<db::FullConfigTemplateItem> item = dao.getById("manual_001");
    ASSERT_TRUE(item.has_value());
    EXPECT_EQ(R"({"log":{"loglevel":"debug"}})", item->config);
}

// 回归测试：物化不得把 ${...} 替换为 0。
// Config 是待运行期填充的模板，实际取值由 ConfigBuilder::replacePlaceholders
// 在生成时用真实 vars 注入。物化阶段若替换，占位符会被永久破坏。
TEST_F(TemplateDecomposerTest, SyncConfigFromChain_PreservesPlaceholders) {
    insertTemplate("ph_001", 2, makePlaceholderConfig());
    composer->rebuildBundlesAndChains();

    db::FullConfigTemplateDAO dao(db);
    std::optional<db::FullConfigTemplateItem> item = dao.getById("ph_001");
    ASSERT_TRUE(item.has_value());

    const boost::json::value cfg = boost::json::parse(item->config);
    ASSERT_TRUE(cfg.is_object());

    // api.listen 应保留 ${apiPort}，而非被替换成 127.0.0.1:0
    ASSERT_EQ(1u, cfg.as_object().count("api"));
    const boost::json::value& api = cfg.as_object().at("api");
    ASSERT_TRUE(api.is_object());
    ASSERT_EQ(1u, api.as_object().count("listen"));
    EXPECT_EQ("127.0.0.1:${apiPort}",
              std::string(api.as_object().at("listen").as_string().c_str()));

    // inbounds[].port 应保持字符串 "${socksPort}"。
    // 旧实现用空 vars 替换后还会尝试 stoi，把字符串端口变成数字 0。
    ASSERT_EQ(1u, cfg.as_object().count("inbounds"));
    const boost::json::value& inbounds = cfg.as_object().at("inbounds");
    ASSERT_TRUE(inbounds.is_array());
    ASSERT_FALSE(inbounds.as_array().empty());
    const boost::json::value& first = inbounds.as_array().at(0);
    ASSERT_TRUE(first.is_object());
    ASSERT_EQ(1u, first.as_object().count("port"));
    EXPECT_EQ("${socksPort}",
              std::string(first.as_object().at("port").as_string().c_str()));
}

// 回归测试：routing 合并只处理 rules/balancers 会静默丢弃 domainStrategy。
TEST_F(TemplateDecomposerTest, SyncConfigFromChain_KeepsRoutingDomainStrategy) {
    insertTemplate("xray_base_001", 2, makeXrayBaseConfig());
    insertTemplate("xray_test_001", 2, makeXrayTestConfig());
    ASSERT_EQ(7, composer->rebuildBundlesAndChains());

    db::FullConfigTemplateDAO dao(db);
    std::optional<db::FullConfigTemplateItem> test = dao.getById("xray_test_001");
    ASSERT_TRUE(test.has_value());

    const boost::json::value cfg = boost::json::parse(test->config);
    ASSERT_TRUE(cfg.is_object());
    ASSERT_EQ(1u, cfg.as_object().count("routing"));
    const boost::json::value& routing = cfg.as_object().at("routing");
    ASSERT_TRUE(routing.is_object());
    ASSERT_EQ(1u, routing.as_object().count("domainStrategy"));
    EXPECT_EQ("AsIs", std::string(routing.as_object().at("domainStrategy").as_string().c_str()));
}

// 回归测试：sing-box 的路由节段名是 route，Xray 是 routing。
TEST_F(TemplateDecomposerTest, SyncConfigFromChain_UsesRouteForSingBox) {
    insertTemplate("singbox_base_001", 24, makeXrayBaseConfig());
    insertTemplate("singbox_test_001", 24, makeXrayTestConfig());
    composer->rebuildBundlesAndChains();

    db::FullConfigTemplateDAO dao(db);
    std::optional<db::FullConfigTemplateItem> test = dao.getById("singbox_test_001");
    ASSERT_TRUE(test.has_value());

    const boost::json::value cfg = boost::json::parse(test->config);
    ASSERT_TRUE(cfg.is_object());
    // sing-box 必须产出 route，且不得残留 routing
    EXPECT_EQ(1u, cfg.as_object().count("route"));
    EXPECT_EQ(0u, cfg.as_object().count("routing"));
}

// 夹具 makePlaceholderConfig（api + inbounds）拆出的包名
    static const char* kBundleA() { return "xray_API"; }
    static const char* kBundleB() { return "xray_INBOUND"; }

    // 回归（2026-10-10）：syncConfigsFromChains 早前用 bundleChain.find(id) 做子串匹配，
    // 前缀相同的 ID 会互相误命中。改为解析 bundleIds 逐项精确相等比较。
    // getByBundleId：组件包变更时定位依赖模板（供 UI 同步 + 改名/删除时提示）。
TEST_F(TemplateDecomposerTest, GetByBundleId_FindsExactReferencingTemplates) {
    insertTemplate("t_log", 2, makePlaceholderConfig());
    insertTemplate("t_in", 2, makePlaceholderConfig());
    insertTemplate("t_none", 2, makePlaceholderConfig());
    ASSERT_EQ(2, composer->rebuildBundlesAndChains());

    db::FullConfigTemplateDAO dao(db);
    // 该夹具拆出 xray_API 与 xray_INBOUND
    setTemplateChain("t_log", R"({"bundleIds":["xray_API"]})");
    setTemplateChain("t_in", R"({"bundleIds":["xray_API","xray_INBOUND"]})");
    setTemplateChain("t_none", R"({"bundleIds":["xray_LOG2"]})");

    const std::vector<db::FullConfigTemplateItem> hits = dao.getByBundleId("xray_API");
    EXPECT_EQ(2u, hits.size());

    // 前缀不得误命中：xray_API 不应命中只含 xray_INBOUND 的 t_none
    EXPECT_EQ(1u, dao.getByBundleId("xray_INBOUND").size());
    EXPECT_TRUE(dao.getByBundleId("xray_INBOU").empty()) << "前缀不应误命中";
    EXPECT_EQ(1u, dao.getByBundleId("xray_LOG2").size());
    EXPECT_TRUE(dao.getByBundleId("no_such").empty());
    EXPECT_TRUE(dao.getByBundleId("").empty());
}

TEST_F(TemplateDecomposerTest, SyncConfigsFromChains_MatchesBundleIdExactly) {
        insertTemplate("ph_base", 2, makePlaceholderConfig());
        ASSERT_EQ(2, composer->rebuildBundlesAndChains());

        // ph_base 只引用 xray_INBOUND
        setTemplateChain("ph_base", std::string("{\"bundleIds\":[\"") + kBundleB() + "\"]}");

        // 精确匹配：应选中 ph_base 并成功物化
        EXPECT_EQ(1, composer->syncConfigsFromChains(kBundleB()));

        // 前缀 "xray_INBOU" 不等于 "xray_INBOUND"：不得选中任何模板
        EXPECT_EQ(0, composer->syncConfigsFromChains("xray_INBOU"));
    }

// 链引用了不存在的组件包时，物化必须失败（不得静默产出空/错误 Config）
TEST_F(TemplateDecomposerTest, SyncConfigFromChain_FailsWhenBundleMissing) {
    insertTemplate("dangling", 2, makePlaceholderConfig());
    ASSERT_EQ(2, composer->rebuildBundlesAndChains());

    // 先记录重建后的 Config（此时链有效，物化成功）
    db::FullConfigTemplateDAO dao(db);
    std::optional<db::FullConfigTemplateItem> before = dao.getById("dangling");
    ASSERT_TRUE(before.has_value());

    // 改成引用不存在的包
    setTemplateChain("dangling", R"({"bundleIds":["no_such_bundle"]})");

    // 引用缺失 → resolve 失败 → 返回 false
    EXPECT_FALSE(composer->syncConfigFromChain("dangling"));

    // Config 必须与失败前一致（未被清空/覆盖）——按语义比对：
    // 重建阶段已把原始多行文本规范化为压缩单行，故不能直接比字符串。
    std::optional<db::FullConfigTemplateItem> after = dao.getById("dangling");
    ASSERT_TRUE(after.has_value());
    const boost::json::value lhs = boost::json::parse(before->config);
    const boost::json::value rhs = boost::json::parse(after->config);
    EXPECT_EQ(boost::json::serialize(lhs), boost::json::serialize(rhs));
    // 占位符仍在（未被半途写坏）
    EXPECT_NE(std::string::npos, after->config.find("${socksPort}"));
}

// 链 JSON 非法（缺 bundleIds / 非对象 / 非 JSON）时同样必须失败而非崩溃
TEST_F(TemplateDecomposerTest, SyncConfigFromChain_FailsOnMalformedChain) {
    insertTemplate("badchain", 2, makePlaceholderConfig());
    ASSERT_EQ(2, composer->rebuildBundlesAndChains());

    setTemplateChain("badchain", R"({"notBundleIds":[]})");
    EXPECT_FALSE(composer->syncConfigFromChain("badchain"));

    setTemplateChain("badchain", R"(not-json)");
    EXPECT_FALSE(composer->syncConfigFromChain("badchain"));

    setTemplateChain("badchain", R"([1,2,3])");
    EXPECT_FALSE(composer->syncConfigFromChain("badchain"));
}

} // namespace
