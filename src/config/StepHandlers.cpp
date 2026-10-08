#include "config/StepHandler.h"
#include "config/StepRegistry.h"
#include "config/ConfigBuilder.h"
#include "Logger.h"
#include <stdexcept>
#include <string>
#include <utility>

namespace config {

// ============================================================
// replace_variables Step
// ============================================================
class ReplaceVariablesHandler : public StepHandler {
public:
    std::string type() const override { return "replace_variables"; }

    void execute(
        boost::json::object& config,
        const boost::json::object& params,
        const PipelineContext& ctx) const override {
        (void)params;
        boost::json::value configVal(config);
        boost::json::value replaced = ConfigBuilder::replaceVariables(configVal, ctx.vars);
        config = replaced.as_object();
        Logger::write("[Pipeline] replace_variables: OK", LogLevel::DEBUG);
    }
};

// ============================================================
// add_outbound Step
// ============================================================
class AddOutboundHandler : public StepHandler {
public:
    std::string type() const override { return "add_outbound"; }

    void execute(
        boost::json::object& config,
        const boost::json::object& params,
        const PipelineContext& ctx) const override {
        std::string tag = params.contains("tag") && params.at("tag").is_string()
            ? std::string(params.at("tag").get_string()) : std::string("proxy");
        std::string source = params.contains("source") && params.at("source").is_string()
            ? std::string(params.at("source").get_string()) : std::string("");

        boost::json::object outbound;

        if (source == "profile") {
            if (!ctx.hasProfile) {
                Logger::write("[Pipeline] add_outbound: source=profile but no valid profile, skipped", LogLevel::WARN);
                return;
            }
            outbound = ConfigBuilder::buildOutboundFromProfile(ctx.profile, ctx.proxyType, tag);
        } else {
            outbound = boost::json::object{
                {"tag", tag},
                {"protocol", params.contains("protocol") ? params.at("protocol") : boost::json::value(std::string("freedom"))}
            };
            if (params.contains("settings")) {
                outbound["settings"] = params.at("settings");
            }
        }

        if (!config.contains("outbounds")) {
            config["outbounds"] = boost::json::array{};
        }
        boost::json::array& outbounds = config["outbounds"].as_array();

        for (boost::json::value& existing : outbounds) {
            if (existing.is_object()) {
                const boost::json::object& obj = existing.as_object();
                if (obj.contains("tag") && obj.at("tag").is_string() &&
                    obj.at("tag").get_string() == tag) {
                    if (params.contains("settings")) {
                        existing.as_object()["settings"] = params.at("settings");
                    }
                    Logger::write("[Pipeline] add_outbound: updated existing tag=" + tag, LogLevel::DEBUG);
                    return;
                }
            }
        }

        outbounds.push_back(outbound);
        Logger::write("[Pipeline] add_outbound: added tag=" + tag, LogLevel::DEBUG);
    }
};

// ============================================================
// merge_routing Step
// ============================================================
class MergeRoutingHandler : public StepHandler {
public:
    std::string type() const override { return "merge_routing"; }

    void execute(
        boost::json::object& config,
        const boost::json::object& params,
        const PipelineContext& ctx) const override {
        (void)ctx;
        std::string inboundTag = params.contains("inboundTag") && params.at("inboundTag").is_string()
            ? std::string(params.at("inboundTag").get_string()) : std::string("socks");
        std::string outboundTag = params.contains("outboundTag") && params.at("outboundTag").is_string()
            ? std::string(params.at("outboundTag").get_string()) : std::string("");
        std::string balancerTag = params.contains("balancerTag") && params.at("balancerTag").is_string()
            ? std::string(params.at("balancerTag").get_string()) : std::string("");

        ConfigBuilder::mergeSocksInboundRule(config, inboundTag, outboundTag, balancerTag);
        Logger::write("[Pipeline] merge_routing: inbound=" + inboundTag +
            " outbound=" + outboundTag + " balancer=" + balancerTag, LogLevel::DEBUG);
    }
};

// ============================================================
// add_inbound Step
// ============================================================
class AddInboundHandler : public StepHandler {
public:
    std::string type() const override { return "add_inbound"; }

    void execute(
        boost::json::object& config,
        const boost::json::object& params,
        const PipelineContext& ctx) const override {
        (void)ctx;
        std::string tag = params.contains("tag") && params.at("tag").is_string()
            ? std::string(params.at("tag").get_string()) : std::string("socks");
        std::string protocol = params.contains("protocol") && params.at("protocol").is_string()
            ? std::string(params.at("protocol").get_string()) : std::string("socks");
        int port = params.contains("port") && params.at("port").is_int64()
            ? static_cast<int>(params.at("port").as_int64()) : 10808;

        if (!config.contains("inbounds")) {
            config["inbounds"] = boost::json::array{};
        }
        boost::json::array& inbounds = config["inbounds"].as_array();

        boost::json::object inbound;
        inbound["tag"] = tag;
        inbound["port"] = port;
        inbound["protocol"] = protocol;
        inbound["listen"] = "127.0.0.1";
        if (params.contains("settings")) {
            inbound["settings"] = params.at("settings");
        }

        for (boost::json::value& existing : inbounds) {
            if (existing.is_object()) {
                const boost::json::object& obj = existing.as_object();
                if (obj.contains("tag") && obj.at("tag").is_string() &&
                    obj.at("tag").get_string() == tag) {
                    if (params.contains("settings")) {
                        existing.as_object()["settings"] = params.at("settings");
                    }
                    if (params.contains("port") && params.at("port").is_int64()) {
                        existing.as_object()["port"] = params.at("port").as_int64();
                    }
                    if (params.contains("protocol") && params.at("protocol").is_string()) {
                        existing.as_object()["protocol"] = params.at("protocol").get_string();
                    }
                    Logger::write("[Pipeline] add_inbound: updated existing tag=" + tag, LogLevel::DEBUG);
                    return;
                }
            }
        }

        inbounds.push_back(inbound);
        Logger::write("[Pipeline] add_inbound: added tag=" + tag + " port=" + std::to_string(port), LogLevel::DEBUG);
    }
};

// ============================================================
// add_observatory Step
// ============================================================
class AddObservatoryHandler : public StepHandler {
public:
    std::string type() const override { return "add_observatory"; }

    void execute(
        boost::json::object& config,
        const boost::json::object& params,
        const PipelineContext& ctx) const override {
        (void)ctx;
        boost::json::object observatory;
        if (params.contains("subjectSelector")) {
            observatory["subjectSelector"] = params.at("subjectSelector");
        }
        if (params.contains("probeURL")) {
            observatory["probeURL"] = params.at("probeURL");
        }
        if (params.contains("probeInterval")) {
            observatory["probeInterval"] = params.at("probeInterval");
        }
        config["observatory"] = observatory;
        Logger::write("[Pipeline] add_observatory: OK", LogLevel::DEBUG);
    }
};

// ============================================================
// add_balancer Step
// ============================================================
class AddBalancerHandler : public StepHandler {
public:
    std::string type() const override { return "add_balancer"; }

    void execute(
        boost::json::object& config,
        const boost::json::object& params,
        const PipelineContext& ctx) const override {
        (void)ctx;
        std::string tag = params.contains("tag") && params.at("tag").is_string()
            ? std::string(params.at("tag").get_string()) : std::string("balancer-out");

        boost::json::object balancer;
        balancer["tag"] = tag;
        if (params.contains("selector")) {
            balancer["selector"] = params.at("selector");
        }
        if (params.contains("strategy")) {
            boost::json::object strategy;
            strategy["type"] = params.at("strategy");
            balancer["strategy"] = strategy;
        }
        if (params.contains("observation")) {
            balancer["observation"] = params.at("observation");
        }

        if (!config.contains("routing")) {
            config["routing"] = boost::json::object{{"domainStrategy", "AsIs"}};
        }
        boost::json::object& routing = config["routing"].as_object();
        if (!routing.contains("balancers")) {
            routing["balancers"] = boost::json::array{};
        }
        boost::json::array& balancers = routing["balancers"].as_array();
        balancers.push_back(balancer);
        Logger::write("[Pipeline] add_balancer: added tag=" + tag, LogLevel::DEBUG);
    }
};

// ============================================================
// add_stats Step
// ============================================================
class AddStatsHandler : public StepHandler {
public:
    std::string type() const override { return "add_stats"; }

    void execute(
        boost::json::object& config,
        const boost::json::object& params,
        const PipelineContext& ctx) const override {
        (void)ctx;
        (void)params;
        config["stats"] = boost::json::object{};
        Logger::write("[Pipeline] add_stats: OK", LogLevel::DEBUG);
    }
};

// ============================================================
// add_policy Step
// ============================================================
class AddPolicyHandler : public StepHandler {
public:
    std::string type() const override { return "add_policy"; }

    void execute(
        boost::json::object& config,
        const boost::json::object& params,
        const PipelineContext& ctx) const override {
        (void)ctx;
        boost::json::object policy;
        if (params.contains("levels")) {
            policy["levels"] = params.at("levels");
        }
        if (params.contains("system")) {
            policy["system"] = params.at("system");
        }
        config["policy"] = policy;
        Logger::write("[Pipeline] add_policy: OK", LogLevel::DEBUG);
    }
};

// ============================================================
// add_api Step
// ============================================================
class AddApiHandler : public StepHandler {
public:
    std::string type() const override { return "add_api"; }

    void execute(
        boost::json::object& config,
        const boost::json::object& params,
        const PipelineContext& ctx) const override {
        (void)ctx;
        boost::json::object api;
        if (params.contains("tag")) {
            api["tag"] = params.at("tag");
        } else {
            api["tag"] = "api";
        }
        if (params.contains("listen")) {
            api["listen"] = params.at("listen");
        }
        if (params.contains("services")) {
            api["services"] = params.at("services");
        }
        config["api"] = api;
        Logger::write("[Pipeline] add_api: OK", LogLevel::DEBUG);
    }
};

// ============================================================
// modify_field Step
// ============================================================
class ModifyFieldHandler : public StepHandler {
public:
    std::string type() const override { return "modify_field"; }

    void execute(
        boost::json::object& config,
        const boost::json::object& params,
        const PipelineContext& ctx) const override {
        (void)ctx;
        std::string path = params.contains("path") && params.at("path").is_string()
            ? std::string(params.at("path").get_string()) : std::string("");
        if (path.empty()) {
            throw std::runtime_error("modify_field: path is required");
        }
        const boost::json::value& val = params.at("value");

        // Parse path into steps: "routing.rules[0].inboundTag" →
        //   [{key:"routing"}, {key:"rules"}, {index:0}, {key:"inboundTag"}]
        struct Step {
            bool isArrayIndex;
            std::string key;
            int index;
        };
        std::vector<Step> steps;
        std::string current;
        for (size_t i = 0; i < path.size(); ++i) {
            char c = path[i];
            if (c == '.') {
                if (!current.empty()) {
                    steps.push_back({false, current, -1});
                    current.clear();
                }
            } else if (c == '[') {
                if (!current.empty()) {
                    steps.push_back({false, current, -1});
                    current.clear();
                }
                size_t end = path.find(']', i);
                if (end == std::string::npos) {
                    throw std::runtime_error("modify_field: unmatched '[' in path");
                }
                int idx = std::stoi(path.substr(i + 1, end - i - 1));
                steps.push_back({true, "", idx});
                i = end;
            } else {
                current.push_back(c);
            }
        }
        if (!current.empty()) {
            steps.push_back({false, current, -1});
        }

        if (steps.empty()) {
            throw std::runtime_error("modify_field: empty path after parsing");
        }

        // Work with a copy of the config, navigate, then assign back
        boost::json::value configVal(config);
        boost::json::value* cur = &configVal;

        for (size_t i = 0; i < steps.size(); ++i) {
            const Step& step = steps[i];
            bool isLast = (i == steps.size() - 1);

            if (step.isArrayIndex) {
                if (!cur->is_array()) {
                    throw std::runtime_error("modify_field: expected array at index step");
                }
                boost::json::array& arr = cur->as_array();
                while (static_cast<int>(arr.size()) <= step.index) {
                    arr.push_back(boost::json::value(boost::json::object{}));
                }
                if (isLast) {
                    arr[static_cast<size_t>(step.index)] = val;
                } else {
                    cur = &arr[static_cast<size_t>(step.index)];
                }
            } else {
                if (!cur->is_object()) {
                    throw std::runtime_error("modify_field: expected object at key step '" + step.key + "'");
                }
                boost::json::object& obj = cur->as_object();
                if (isLast) {
                    obj[step.key] = val;
                } else {
                    if (!obj.contains(step.key)) {
                        obj[step.key] = boost::json::value(boost::json::object{});
                    }
                    cur = &obj[step.key];
                }
            }
        }

        config = configVal.as_object();
        Logger::write("[Pipeline] modify_field: set " + path, LogLevel::DEBUG);
    }
};

// ============================================================
// Registration
// ============================================================
void registerAllStepHandlers() {
    auto& registry = StepRegistry::instance();
    registry.registerHandler("replace_variables", std::make_unique<ReplaceVariablesHandler>());
    registry.registerHandler("add_outbound", std::make_unique<AddOutboundHandler>());
    registry.registerHandler("merge_routing", std::make_unique<MergeRoutingHandler>());
    registry.registerHandler("add_inbound", std::make_unique<AddInboundHandler>());
    registry.registerHandler("add_observatory", std::make_unique<AddObservatoryHandler>());
    registry.registerHandler("add_balancer", std::make_unique<AddBalancerHandler>());
    registry.registerHandler("add_stats", std::make_unique<AddStatsHandler>());
    registry.registerHandler("add_policy", std::make_unique<AddPolicyHandler>());
    registry.registerHandler("add_api", std::make_unique<AddApiHandler>());
    registry.registerHandler("modify_field", std::make_unique<ModifyFieldHandler>());
}

} // namespace config
