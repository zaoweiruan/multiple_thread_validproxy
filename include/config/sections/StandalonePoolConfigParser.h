#ifndef CONFIG_SECTIONS_STANDALONE_POOL_H
#define CONFIG_SECTIONS_STANDALONE_POOL_H

#include <boost/json.hpp>
#include <string>
#include "ConfigReader.h"
#include "Logger.h"

namespace config {

class StandalonePoolConfigParser {
public:
    void parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir);
};

inline void StandalonePoolConfigParser::parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir) {
    (void)exeDir;
    if (!root.is_object()) return;
    const boost::json::object& obj = root.as_object();
    if (!obj.contains("standalone_pool") || !obj.at("standalone_pool").is_object()) {
        return; // 缺省值保持
    }
    const boost::json::object& sp = obj.at("standalone_pool").as_object();

    if (sp.contains("enabled") && sp.at("enabled").is_bool()) {
        config.standalone_pool.enabled = sp.at("enabled").as_bool();
    }
    if (sp.contains("mode") && sp.at("mode").is_string()) {
        std::string m = sp.at("mode").as_string().c_str();
        if (m == "pool" || m == "select") config.standalone_pool.mode = m;
    }
    if (sp.contains("socksPort") && sp.at("socksPort").is_int64()) {
        int v = static_cast<int>(sp.at("socksPort").as_int64());
        if (v > 0) config.standalone_pool.socksPort = v;
    }
    if (sp.contains("apiPort") && sp.at("apiPort").is_int64()) {
        int v = static_cast<int>(sp.at("apiPort").as_int64());
        if (v > 0) config.standalone_pool.apiPort = v;
    }
    if (sp.contains("balancerStrategy") && sp.at("balancerStrategy").is_string()) {
        std::string s = sp.at("balancerStrategy").as_string().c_str();
        if (s == "random" || s == "leastPing" || s == "leastLoad") config.standalone_pool.balancerStrategy = s;
    }
    if (sp.contains("observatory") && sp.at("observatory").is_object()) {
        const boost::json::object& ob = sp.at("observatory").as_object();
        if (ob.contains("type") && ob.at("type").is_string()) {
            std::string t = ob.at("type").as_string().c_str();
            if (t == "http" || t == "ping") config.standalone_pool.observatory.type = t;
        }
        if (ob.contains("destination") && ob.at("destination").is_string()) {
            config.standalone_pool.observatory.destination = ob.at("destination").as_string().c_str();
        }
        if (ob.contains("intervalSec") && ob.at("intervalSec").is_int64()) {
            int v = static_cast<int>(ob.at("intervalSec").as_int64());
            if (v > 0) config.standalone_pool.observatory.intervalSec = v;
        }
        if (ob.contains("samplingCount") && ob.at("samplingCount").is_int64()) {
            int v = static_cast<int>(ob.at("samplingCount").as_int64());
            if (v > 0) config.standalone_pool.observatory.samplingCount = v;
        }
        if (ob.contains("timeoutSec") && ob.at("timeoutSec").is_int64()) {
            int v = static_cast<int>(ob.at("timeoutSec").as_int64());
            if (v > 0) config.standalone_pool.observatory.timeoutSec = v;
        }
    }
    if (sp.contains("evaluate") && sp.at("evaluate").is_object()) {
        const boost::json::object& ev = sp.at("evaluate").as_object();
        if (ev.contains("intervalSec") && ev.at("intervalSec").is_int64()) {
            int v = static_cast<int>(ev.at("intervalSec").as_int64());
            if (v > 0) config.standalone_pool.evaluate.intervalSec = v;
        }
        if (ev.contains("reportHealth") && ev.at("reportHealth").is_bool()) {
            config.standalone_pool.evaluate.reportHealth = ev.at("reportHealth").as_bool();
        }
        if (ev.contains("autoPruneDead") && ev.at("autoPruneDead").is_bool()) {
            config.standalone_pool.evaluate.autoPruneDead = ev.at("autoPruneDead").as_bool();
        }
        if (ev.contains("pruneFailStreak") && ev.at("pruneFailStreak").is_int64()) {
            int v = static_cast<int>(ev.at("pruneFailStreak").as_int64());
            if (v > 0) config.standalone_pool.evaluate.pruneFailStreak = v;
        }
        if (ev.contains("autoOptimize") && ev.at("autoOptimize").is_bool()) {
            config.standalone_pool.evaluate.autoOptimize = ev.at("autoOptimize").as_bool();
        }
        if (ev.contains("probeWorkers") && ev.at("probeWorkers").is_int64()) {
            int v = static_cast<int>(ev.at("probeWorkers").as_int64());
            // 0 = 禁用常驻探针池（ProxyProbePool::start workerCount<=0 分支）；
            // 上限 64 防误配（spec PoolConfigDialogAdjust）。
            if (v >= 0 && v <= 64) config.standalone_pool.evaluate.probeWorkers = v;
        }
    }
}

} // namespace config

#endif // CONFIG_SECTIONS_STANDALONE_POOL_H
