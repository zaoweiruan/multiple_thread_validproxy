#include "config/TemplateDecomposer.h"
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
    // 按 ID 后缀推断 TemplateType
    if (templateId.find("_base_") != std::string::npos)
        return TemplateType::BASE;
    if (templateId.find("_test_") != std::string::npos)
        return TemplateType::TEST;
    if (templateId.find("_pool_") != std::string::npos)
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
