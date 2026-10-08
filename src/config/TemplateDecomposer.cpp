#include "config/TemplateDecomposer.h"
#include "config/BundleChainResolver.h"
#include "Logger.h"
#include <sstream>
#include <algorithm>

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
    bundle.templateType = inferTemplateType(templateId);
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

int TemplateDecomposer::generateChains() const {
    db::FullConfigTemplateDAO tmplDao(db_);
    ComponentBundleDAO bundleDao(db_);

    const std::vector<db::FullConfigTemplateItem> templates = tmplDao.getAll();
    const std::vector<ComponentBundle> bundles = bundleDao.getAll();

    // bundle id → bundle
    std::map<std::string, ComponentBundle> bundlesById;
    for (std::vector<ComponentBundle>::const_iterator it = bundles.begin();
         it != bundles.end(); ++it) {
        bundlesById[it->id] = *it;
    }

    // 每个 CoreType 的 BASE bundle（同一 core 取第一个）
    std::map<int, std::string> baseByCore;
    for (std::map<std::string, ComponentBundle>::const_iterator it = bundlesById.begin();
         it != bundlesById.end(); ++it) {
        if (it->second.templateType != TemplateType::BASE) {
            continue;
        }
        const int core = (it->second.coreType == CoreType::XRAY) ? 2 : 24;
        if (baseByCore.count(core) == 0) {
            baseByCore[core] = it->second.id;
        }
    }

    BundleChainResolver resolver(db_);
    int updated = 0;
    for (std::vector<db::FullConfigTemplateItem>::const_iterator it = templates.begin();
         it != templates.end(); ++it) {
        const std::string templateId = it->id;

        // 本模板自身的 bundle 必须已拆解
        std::map<std::string, ComponentBundle>::const_iterator selfIt =
            bundlesById.find(templateId);
        if (selfIt == bundlesById.end()) {
            Logger::write("[TemplateDecomposer] generateChains: bundle missing, skip "
                              + templateId,
                          LogLevel::WARN);
            continue;
        }

        // 组装 bundleIds：base 打底 + 自身
        std::vector<std::string> bundleIds;
        const int core = (selfIt->second.coreType == CoreType::XRAY) ? 2 : 24;
        std::map<int, std::string>::const_iterator baseIt = baseByCore.find(core);
        if (selfIt->second.templateType != TemplateType::BASE) {
            if (baseIt == baseByCore.end()) {
                Logger::write("[TemplateDecomposer] generateChains: no BASE bundle for core "
                                  + std::to_string(core) + ", skip " + templateId,
                              LogLevel::WARN);
                continue;
            }
            bundleIds.push_back(baseIt->second);
        }
        bundleIds.push_back(templateId);

        // 链 JSON
        boost::json::object root;
        boost::json::array idsArr;
        for (std::vector<std::string>::const_iterator bi = bundleIds.begin();
             bi != bundleIds.end(); ++bi) {
            idsArr.push_back(boost::json::value(*bi));
        }
        root["bundleIds"] = idsArr;
        root["params"] = boost::json::object();

        // 版本快照
        BundleChain chain;
        chain.bundleIds = bundleIds;
        const std::vector<BundleVersionInfo> versions = resolver.getCurrentVersions(chain);
        boost::json::array verArr;
        for (std::vector<BundleVersionInfo>::const_iterator vi = versions.begin();
             vi != versions.end(); ++vi) {
            boost::json::object verObj;
            verObj["bundleId"] = vi->bundleId;
            verObj["version"] = vi->version;
            verArr.push_back(verObj);
        }

        db::FullConfigTemplateItem updatedItem = *it;
        updatedItem.bundleChain = boost::json::serialize(root);
        updatedItem.bundleVersion = boost::json::serialize(verArr);
        updatedItem.templateType = "bundleChain";

        if (!tmplDao.save(updatedItem)) {
            Logger::write("[TemplateDecomposer] generateChains: save failed " + templateId,
                          LogLevel::WARN);
            continue;
        }
        ++updated;
        Logger::write("[TemplateDecomposer] generateChains: " + templateId + " -> "
                          + updatedItem.bundleChain,
                      LogLevel::INFO);
    }
    return updated;
}
