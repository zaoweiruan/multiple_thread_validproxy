#include "config/TemplateDecomposer.h"
#include "config/BundleChainResolver.h"
#include "Logger.h"
#include <sstream>
#include <algorithm>
#include <set>
#include <functional>
#include <utility>

using namespace config;

namespace {

// JSON section → ComponentType 映射表
const std::vector<std::pair<std::string, ComponentType>>& sectionMap() {
    static const std::vector<std::pair<std::string, ComponentType>> m = {
        {"log", ComponentType::LOG},
        {"dns", ComponentType::DNS},
        {"api", ComponentType::API},
        {"stats", ComponentType::STATS},
        {"policy", ComponentType::POLICY},
        {"inbounds", ComponentType::INBOUND},
        {"outbounds", ComponentType::OUTBOUND},
        {"routing", ComponentType::ROUTING},
        {"route", ComponentType::ROUTING},  // sing-box 使用单数节名
        {"observatory", ComponentType::OBSERVATORY},
        {"burstObservatory", ComponentType::BURST_OBSERVATORY},
        {"geodata", ComponentType::GEODATA},
        {"version", ComponentType::VERSION},
        {"fakedns", ComponentType::FAKEDNS},
        {"metrics", ComponentType::METRICS},
        {"env", ComponentType::ENV},
    };
    return m;
}

// ComponentType → 短名（用于组件包 ID）
const char* componentTypeName(ComponentType ct) {
    switch (ct) {
        case ComponentType::ENV:               return "ENV";
        case ComponentType::LOG:               return "LOG";
        case ComponentType::API:               return "API";
        case ComponentType::DNS:               return "DNS";
        case ComponentType::ROUTING:           return "ROUTING";
        case ComponentType::POLICY:            return "POLICY";
        case ComponentType::INBOUND:           return "INBOUND";
        case ComponentType::OUTBOUND:          return "OUTBOUND";
        case ComponentType::STATS:             return "STATS";
        case ComponentType::FAKEDNS:           return "FAKEDNS";
        case ComponentType::METRICS:           return "METRICS";
        case ComponentType::OBSERVATORY:       return "OBSERVATORY";
        case ComponentType::BURST_OBSERVATORY: return "BURST_OBS";
        case ComponentType::GEODATA:           return "GEODATA";
        case ComponentType::VERSION:           return "VERSION";
    }
    return "UNKNOWN";
}

// TemplateType → 角色短名（内容不一致时用于区分同类型组件包）
const char* templateRoleName(TemplateType tt) {
    switch (tt) {
        case TemplateType::BASE:       return "base";
        case TemplateType::TEST:       return "test";
        case TemplateType::STANDALONE: return "standalone";
        case TemplateType::POOL:       return "pool";
    }
    return "unknown";
}

// 组件内容指纹：用于判定同 (代理类型, 组件类型) 下内容是否一致
std::string componentFingerprint(const ComponentConfig& comp) {
    boost::json::object obj;
    obj["type"] = static_cast<int>(comp.type);
    obj["id"] = comp.id;
    boost::json::object params;
    for (const auto& [k, v] : comp.params) params[k] = v;
    obj["params"] = std::move(params);
    return boost::json::serialize(boost::json::value(std::move(obj)));
}

} // namespace

TemplateDecomposer::TemplateDecomposer(sqlite3* db)
    : db_(db) {}

std::optional<ComponentType> TemplateDecomposer::sectionToComponentType(
    const std::string& section) {
    for (const auto& [name, ct] : sectionMap()) {
        if (name == section) return ct;
    }
    return std::nullopt;
}

TemplateType TemplateDecomposer::inferTemplateType(
    const std::string& templateId) {
    // 按 ID 后缀/前缀推断 TemplateType（pool_ 前缀与 _pool_ 中缀都识别）
    if (templateId.find("_base_") != std::string::npos)
        return TemplateType::BASE;
    if (templateId.find("_test_") != std::string::npos)
        return TemplateType::TEST;
    if (templateId.find("_pool_") != std::string::npos ||
        templateId.rfind("pool_", 0) == 0)
        return TemplateType::POOL;
    // default 映射为 STANDALONE
    if (templateId.find("_default_") != std::string::npos)
        return TemplateType::STANDALONE;
    // 默认 BASE
    return TemplateType::BASE;
}

std::map<std::string, boost::json::value>
TemplateDecomposer::objectToParams(const boost::json::object& obj) {
    std::map<std::string, boost::json::value> params;
    for (const auto& [key, value] : obj) {
        params[key] = value;
    }
    return params;
}

ComponentBundle TemplateDecomposer::buildBundle(
    const std::string& templateId,
    int coreType,
    const std::string& configJson) const {

    ComponentBundle bundle;
    bundle.id = templateId;
    bundle.name = templateId;
    bundle.description = "Decomposed from template: " + templateId;
    bundle.coreType = (coreType == 2) ? CoreType::XRAY : CoreType::SINGBOX;
    bundle.version = 1;
    bundle.enabled = true;

    // 解析 JSON
    auto parseResult = boost::json::parse(configJson);
    if (parseResult.is_null()) {
        return bundle;
    }

    auto& root = parseResult.as_object();

    for (const auto& [section, value] : root) {
        auto ctOpt = sectionToComponentType(section);
        if (!ctOpt) {
            Logger::write("[TemplateDecomposer] Unknown section: " +
                          std::string(section),
                          LogLevel::WARN);
            continue;
        }

        ComponentType ct = *ctOpt;

        // inbounds / outbounds 是数组，每个元素独立成为 ComponentConfig
        if ((ct == ComponentType::INBOUND || ct == ComponentType::OUTBOUND)
            && value.is_array()) {
            for (const auto& item : value.as_array()) {
                if (!item.is_object()) continue;

                ComponentConfig comp;
                comp.type = ct;
                comp.params = objectToParams(item.as_object());

                // 用 tag 作为组件 id
                auto tagIt = comp.params.find("tag");
                if (tagIt != comp.params.end() && tagIt->second.is_string()) {
                    comp.id = tagIt->second.as_string().c_str();
                }

                bundle.components.push_back(comp);
            }
        } else if (value.is_object()) {
            // 单例节段：整个对象作为 params
            ComponentConfig comp;
            comp.type = ct;
            comp.params = objectToParams(value.as_object());
            bundle.components.push_back(comp);
        } else {
            // 其他类型（数组标量等），尝试包装
            ComponentConfig comp;
            comp.type = ct;
            std::map<std::string, boost::json::value> params;
            params["value"] = value;
            comp.params = std::move(params);
            bundle.components.push_back(comp);
        }
    }

    return bundle;
}

TemplateDecomposer::DecomposeResult TemplateDecomposer::decompose(
    const std::string& templateId) const {

    DecomposeResult result;
    result.templateId = templateId;

    db::FullConfigTemplateDAO dao(db_);
    auto templateOpt = dao.getById(templateId);
    if (!templateOpt) {
        result.error = "Template not found: " + templateId;
        return result;
    }

    const auto& tpl = *templateOpt;

    // 选择配置：优先 config，其次 tunConfig
    const std::string& configJson = !tpl.config.empty() ? tpl.config
                                                        : tpl.tunConfig;
    if (configJson.empty()) {
        result.error = "Empty config for: " + templateId;
        return result;
    }

    result.role = inferTemplateType(templateId);
    result.bundle = buildBundle(templateId, tpl.coreType, configJson);
    result.success = true;
    return result;
}

std::vector<TemplateDecomposer::DecomposeResult> TemplateDecomposer::decomposeAll() const {
    std::vector<TemplateDecomposer::DecomposeResult> results;
    db::FullConfigTemplateDAO dao(db_);
    auto templates = dao.getAll();

    for (const auto& tpl : templates) {
        results.push_back(decompose(tpl.id));
    }
    return results;
}

int TemplateDecomposer::decomposeAndSave(bool overwrite) const {
    auto results = decomposeAll();
    ComponentBundleDAO dao(db_);

    // 确保表存在
    dao.createTableIfNotExists();

    int saved = 0;
    for (const auto& r : results) {
        if (!r.success) {
            Logger::write("[TemplateDecomposer] Failed to decompose " +
                          r.templateId + ": " + r.error,
                          LogLevel::WARN);
            continue;
        }

        if (!overwrite && dao.exists(r.bundle.id)) {
            Logger::write("[TemplateDecomposer] Bundle already exists, skip: " +
                          r.bundle.id,
                          LogLevel::INFO);
            continue;
        }

        if (dao.save(r.bundle)) {
            saved++;
            Logger::write("[TemplateDecomposer] Saved bundle: " + r.bundle.id +
                          " (" + std::to_string(r.bundle.components.size()) +
                          " components)",
                          LogLevel::INFO);
        } else {
            Logger::write("[TemplateDecomposer] Failed to save bundle: " +
                          r.bundle.id,
                          LogLevel::ERR);
        }
    }
    return saved;
}

TemplateDecomposer::BundlePlan TemplateDecomposer::planMinimalBundles() const {
    BundlePlan plan;
    const std::vector<DecomposeResult> results = decomposeAll();

    // (代理类型, 组件类型) -> 角色 -> {内容指纹, 组件}
    std::map<std::pair<int, int>,
             std::map<std::string, std::pair<std::string, ComponentConfig>>> groups;

    for (std::vector<DecomposeResult>::const_iterator it = results.begin();
         it != results.end(); ++it) {
        if (!it->success || it->bundle.components.empty()) continue;

        const int core = (it->bundle.coreType == CoreType::XRAY) ? 2 : 24;
        const std::string role = templateRoleName(it->role);

        for (std::vector<ComponentConfig>::const_iterator ci = it->bundle.components.begin();
             ci != it->bundle.components.end(); ++ci) {
            const int ct = static_cast<int>(ci->type);
            std::map<std::string, std::pair<std::string, ComponentConfig>>& byRole =
                groups[std::make_pair(core, ct)];
            const std::string fp = componentFingerprint(*ci);
            std::map<std::string, std::pair<std::string, ComponentConfig>>::iterator ri =
                byRole.find(role);
            if (ri == byRole.end()) {
                byRole[role] = std::make_pair(fp, *ci);
            } else if (ri->second.first != fp) {
                Logger::write("[TemplateDecomposer] Same role/type content conflict, "
                              "keep first: " + it->templateId,
                              LogLevel::WARN);
            }
        }
    }

    // 生成组件包，并记录 (core, type) -> role -> bundleId
    std::map<std::pair<int, int>, std::map<std::string, std::string>> idOf;

    for (std::map<std::pair<int, int>,
                  std::map<std::string, std::pair<std::string, ComponentConfig>>>::const_iterator
             g = groups.begin(); g != groups.end(); ++g) {
        const int core = g->first.first;
        // 内容指纹唯一 → 合并为共享包；否则按角色拆包
        std::set<std::string> distinct;
        for (std::map<std::string, std::pair<std::string, ComponentConfig>>::const_iterator
                 f = g->second.begin(); f != g->second.end(); ++f) {
            distinct.insert(f->second.first);
        }
        const bool shared = (distinct.size() == 1);
        const char* prefix = (core == 2) ? "xray_" : "singbox_";
        const char* tname = componentTypeName(static_cast<ComponentType>(g->first.second));

        ComponentBundle proto;
        proto.coreType = (core == 2) ? CoreType::XRAY : CoreType::SINGBOX;
        proto.description = std::string("Minimal bundle (") + prefix + tname + ")";
        proto.version = 1;
        proto.enabled = true;

        if (shared) {
            // 内容一致 → 只产出一个共享包，所有角色都指向它
            proto.id = std::string(prefix) + tname;
            proto.name = proto.id;
            proto.components.push_back(g->second.begin()->second.second);
            for (std::map<std::string, std::pair<std::string, ComponentConfig>>::const_iterator
                     r = g->second.begin(); r != g->second.end(); ++r) {
                idOf[g->first][r->first] = proto.id;
            }
            plan.bundles.push_back(proto);
        } else {
            for (std::map<std::string, std::pair<std::string, ComponentConfig>>::const_iterator
                     r = g->second.begin(); r != g->second.end(); ++r) {
                ComponentBundle b = proto;
                b.id = std::string(prefix) + tname + "_" + r->first;
                b.name = b.id;
                b.components.push_back(r->second.second);
                idOf[g->first][r->first] = b.id;
                plan.bundles.push_back(b);
            }
        }
    }

    // 建链：仅模板自身组件；POOL 额外显式含 LOG/DNS
    for (std::vector<DecomposeResult>::const_iterator it = results.begin();
         it != results.end(); ++it) {
        if (!it->success) continue;

        const int core = (it->bundle.coreType == CoreType::XRAY) ? 2 : 24;
        const std::string role = templateRoleName(it->role);

        std::map<int, std::string> typeToId;
        std::function<void(int)> pick = [&](int ct) {
            std::map<std::pair<int, int>, std::map<std::string, std::string>>::const_iterator g =
                idOf.find(std::make_pair(core, ct));
            if (g == idOf.end()) return;
            std::map<std::string, std::string>::const_iterator r = g->second.find(role);
            // 该角色无专属包时回退到唯一共享包
            if (r == g->second.end() && g->second.size() == 1) r = g->second.begin();
            if (r != g->second.end()) typeToId[ct] = r->second;
        };

        for (std::vector<ComponentConfig>::const_iterator ci = it->bundle.components.begin();
             ci != it->bundle.components.end(); ++ci) {
            pick(static_cast<int>(ci->type));
        }
        if (it->role == TemplateType::POOL) {
            pick(static_cast<int>(ComponentType::LOG));
            pick(static_cast<int>(ComponentType::DNS));
        }

        std::vector<std::string>& ids = plan.chains[it->templateId];
        for (std::map<int, std::string>::const_iterator t = typeToId.begin();
             t != typeToId.end(); ++t) {
            ids.push_back(t->second);
        }
    }

    return plan;
}

int TemplateDecomposer::rebuildBundlesAndChains() const {
    const BundlePlan plan = planMinimalBundles();

    ComponentBundleDAO dao(db_);
    dao.createTableIfNotExists();

    std::set<std::string> keep;
    for (std::vector<ComponentBundle>::const_iterator b = plan.bundles.begin();
         b != plan.bundles.end(); ++b) {
        keep.insert(b->id);
    }

    // 删除计划外的旧包，避免模板名包残留
    const std::vector<ComponentBundle> existing = dao.getAll();
    for (std::vector<ComponentBundle>::const_iterator b = existing.begin();
         b != existing.end(); ++b) {
        if (keep.count(b->id) == 0) {
            dao.deleteById(b->id);
            Logger::write("[TemplateDecomposer] Removed stale bundle: " + b->id,
                          LogLevel::INFO);
        }
    }

    int saved = 0;
    for (std::vector<ComponentBundle>::const_iterator b = plan.bundles.begin();
         b != plan.bundles.end(); ++b) {
        if (dao.save(*b)) {
            ++saved;
        } else {
            Logger::write("[TemplateDecomposer] Failed to save bundle: " + b->id,
                          LogLevel::ERR);
        }
    }

    // 回写各模板的组件链与版本快照
    db::FullConfigTemplateDAO tmplDao(db_);
    BundleChainResolver resolver(db_);

    for (std::map<std::string, std::vector<std::string>>::const_iterator c =
             plan.chains.begin(); c != plan.chains.end(); ++c) {
        std::optional<db::FullConfigTemplateItem> item = tmplDao.getById(c->first);
        if (!item) continue;

        boost::json::object root;
        boost::json::array idsArr;
        for (std::vector<std::string>::const_iterator b = c->second.begin();
             b != c->second.end(); ++b) {
            idsArr.push_back(boost::json::value(*b));
        }
        root["bundleIds"] = idsArr;

        BundleChain chain;
        chain.bundleIds = c->second;
        const std::vector<BundleVersionInfo> versions = resolver.getCurrentVersions(chain);
        boost::json::array verArr;
        for (std::vector<BundleVersionInfo>::const_iterator vi = versions.begin();
             vi != versions.end(); ++vi) {
            boost::json::object verObj;
            verObj["bundleId"] = vi->bundleId;
            verObj["version"] = vi->version;
            verArr.push_back(verObj);
        }

        db::FullConfigTemplateItem updated = *item;
        updated.bundleChain = boost::json::serialize(root);
        updated.bundleVersion = boost::json::serialize(verArr);
        updated.templateType = "bundleChain";

        if (!tmplDao.save(updated)) {
            Logger::write("[TemplateDecomposer] save chain failed: " + c->first,
                          LogLevel::WARN);
            continue;
        }
        Logger::write("[TemplateDecomposer] chain " + c->first + " -> "
                      + updated.bundleChain,
                      LogLevel::INFO);
    }

    // 链与组件包都变了 → 重新物化各模板的 Config（配置生成的唯一来源）
    const int synced = syncConfigsFromChains();
    Logger::write("[TemplateDecomposer] synced Config for " + std::to_string(synced)
                  + " templates",
                  LogLevel::REPORT);

    return saved;
}

bool TemplateDecomposer::syncConfigFromChain(const std::string& templateId) const {
    db::FullConfigTemplateDAO tmplDao(db_);
    std::optional<db::FullConfigTemplateItem> item = tmplDao.getById(templateId);
    if (!item || item->bundleChain.empty()) {
        return false;
    }

    BundleChain chain;
    boost::system::error_code ec;
    boost::json::value chainVal = boost::json::parse(item->bundleChain, ec);
    if (ec || !chainVal.is_object()) {
        Logger::write("[TemplateDecomposer] syncConfig: bad chain JSON " + templateId,
                      LogLevel::WARN);
        return false;
    }
    const boost::json::object& chainObj = chainVal.as_object();
    if (!chainObj.contains("bundleIds") || !chainObj.at("bundleIds").is_array()) {
        return false;
    }
    const boost::json::array& idsArr = chainObj.at("bundleIds").as_array();
    for (boost::json::array::const_iterator ai = idsArr.begin(); ai != idsArr.end(); ++ai) {
        if (ai->is_string()) {
            chain.bundleIds.push_back(std::string(ai->as_string().c_str()));
        }
    }
    if (chain.bundleIds.empty()) {
        return false;
    }

    PipelineContext ctx;
    ctx.proxyType = (item->coreType == 24) ? ProxyType::SingBox : ProxyType::Xray;
    ctx.hasProfile = false;
    ctx.vars = ConfigVariables();
    ctx.db = db_;
    ctx.clientName = "chain-materialize";
    // 物化只做「链 → 配置结构」，不替换占位符。
    // Config 是待运行期填充的模板：${socksPort}/${apiPort} 等占位符是契约的一部分，
    // 实际取值由 ConfigBuilder::replacePlaceholders 在生成时用真实 vars 注入
    // （XrayInstance 传入 socksPort_/apiPort_）。此处若替换，因 vars 为空，
    // 会把占位符固化成 0 并永久破坏运行期注入。
    ctx.substitutePlaceholders = false;

    BundleChainResolver resolver(db_);
    const BundleChainResolveResult rr = resolver.resolveToConfig(chain, ctx);
    if (!rr.success) {
        Logger::write("[TemplateDecomposer] syncConfig failed " + templateId + ": "
                      + rr.errorMessage,
                      LogLevel::WARN);
        return false;
    }

    db::FullConfigTemplateItem updated = *item;
    updated.config = boost::json::serialize(rr.config);

    // 同步版本快照，保持 isTemplateInSync 的判定依据有效
    boost::json::array verArr;
    const std::vector<BundleVersionInfo> versions = resolver.getCurrentVersions(chain);
    for (std::vector<BundleVersionInfo>::const_iterator vi = versions.begin();
         vi != versions.end(); ++vi) {
        boost::json::object verObj;
        verObj["bundleId"] = vi->bundleId;
        verObj["version"] = vi->version;
        verArr.push_back(verObj);
    }
    updated.bundleVersion = boost::json::serialize(verArr);

    if (!tmplDao.save(updated)) {
        Logger::write("[TemplateDecomposer] syncConfig save failed " + templateId,
                      LogLevel::WARN);
        return false;
    }
    Logger::write("[TemplateDecomposer] syncConfig: " + templateId + " config <- chain ("
                  + std::to_string(chain.bundleIds.size()) + " bundles)",
                  LogLevel::INFO);
    return true;
}

/// 判断模板的链是否引用了指定组件包。
/// 必须解析 bundleIds 逐项精确比较：早前用 bundleChain.find(id) 做子串匹配，
/// 会让「只含 xray_LOG2 的链」被 xray_LOG 的变更误判为受影响而白跑物化。
static bool chainReferencesBundle(const std::string& bundleChainJson,
                                  const std::string& bundleId) {
    boost::system::error_code ec;
    boost::json::value val = boost::json::parse(bundleChainJson, ec);
    if (ec || !val.is_object()) {
        return false;
    }
    const boost::json::object& obj = val.as_object();
    if (!obj.contains("bundleIds") || !obj.at("bundleIds").is_array()) {
        return false;
    }
    const boost::json::array& ids = obj.at("bundleIds").as_array();
    for (boost::json::array::const_iterator ai = ids.begin(); ai != ids.end(); ++ai) {
        if (ai->is_string()
            && std::string(ai->as_string().c_str()) == bundleId) {
            return true;
        }
    }
    return false;
}

int TemplateDecomposer::syncConfigsFromChains(const std::string& changedBundleId) const {
    db::FullConfigTemplateDAO tmplDao(db_);
    const std::vector<db::FullConfigTemplateItem> templates = tmplDao.getAll();
    int updated = 0;

    for (std::vector<db::FullConfigTemplateItem>::const_iterator it = templates.begin();
         it != templates.end(); ++it) {
        if (it->bundleChain.empty()) {
            continue;
        }
        // 只处理引用了变更组件包的模板
        if (!changedBundleId.empty() && !chainReferencesBundle(it->bundleChain, changedBundleId)) {
            continue;
        }
        if (syncConfigFromChain(it->id)) {
            ++updated;
        }
    }
    return updated;
}
