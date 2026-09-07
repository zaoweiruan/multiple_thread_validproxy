#include "config/ConfigJsonSerializer.h"

namespace config {

boost::json::object ConfigJsonSerializer::serialize(const AppConfig& config) const {
    boost::json::object root;

    // database
    boost::json::object dbObj;
    dbObj["path"] = config.database_path;
    if (!config.sql_query.empty()) dbObj["sql"] = config.sql_query;
    if (!config.sql_by_subid.empty()) dbObj["sql_by_subid"] = config.sql_by_subid;
    root["database"] = dbObj;

    // xray (worker thread config - executable moved to proxy section)
    boost::json::object xrayObj;
    xrayObj["workers"] = config.xray_workers;
    xrayObj["start_port"] = config.xray_start_port;
    xrayObj["api_port"] = config.xray_api_port;
    root["xray"] = xrayObj;

    // test
    boost::json::object testObj;
    testObj["url"] = config.test_url;
    testObj["timeout_ms"] = config.test_timeout_ms;
    root["test"] = testObj;

    // log
    boost::json::object logObj;
    logObj["enabled"] = config.log_enabled;
    logObj["network_failures"] = config.log_network_failures;
    logObj["console_level"] = config.log_console_level;
    logObj["file_level"] = config.log_file_level;
    root["log"] = logObj;

    // subscription
    boost::json::object subObj;
    if (!config.accelerator_url.empty()) {
        subObj["accelerator_url"] = config.accelerator_url;
    }
    {
        boost::json::array methodsArr;
        for (const std::string& m : config.update_methods) {
            methodsArr.emplace_back(m);
        }
        subObj["update_methods"] = methodsArr;
    }
    subObj["check_auto_update_interval"] = config.check_auto_update_interval;
    subObj["connect_timeout_ms"] = config.subscription_connect_timeout_ms;
    subObj["timeout_ms"] = config.subscription_timeout_ms;
    {
        // priority_subids: join with comma
        std::string joined;
        for (size_t i = 0; i < config.priority_subids.size(); ++i) {
            if (i > 0) joined += ",";
            joined += config.priority_subids[i];
        }
        subObj["priority_subids"] = joined;
    }
    root["subscription"] = subObj;

    // dedup
    boost::json::object dedupObj;
    dedupObj["enabled"] = config.dedup_enabled;
    dedupObj["dedup_after_update"] = config.dedup_after_update;
    dedupObj["blacklist_threshold"] = config.blacklist_threshold;
    dedupObj["blacklist_enabled"] = config.blacklist_enabled;
    dedupObj["blacklist_subid"] = config.blacklist_subid;
    {
        boost::json::array subidsArr;
        for (const std::string& sid : config.dedup_subids) {
            subidsArr.emplace_back(sid);
        }
        dedupObj["subids"] = subidsArr;
    }
    root["dedup"] = dedupObj;

    // notification
    boost::json::object notifObj;
    notifObj["enabled"] = config.notification_enabled;
    notifObj["on_update"] = config.notification_on_update;
    notifObj["on_test"] = config.notification_on_test;
    root["notification"] = notifObj;

    // sync
    boost::json::object syncObj;
    syncObj["source_db"] = config.sync.source_db;
    syncObj["target_db"] = config.sync.target_db;
    syncObj["sync_skip_subids"] = config.sync.sync_skip_subids;
    root["sync"] = syncObj;

    // auto_task
    boost::json::object autoTaskObj;
    {
        boost::json::array stepsArr;
        for (const std::string& s : config.auto_task.steps) {
            stepsArr.emplace_back(s);
        }
        autoTaskObj["steps"] = stepsArr;
    }
    autoTaskObj["notify_on_complete"] = config.auto_task.notify_on_complete;
    if (!config.auto_task.state_file.empty()) {
        autoTaskObj["state_file"] = config.auto_task.state_file;
    }
    root["auto_task"] = autoTaskObj;

    // proxy
    boost::json::object proxyObj;
    proxyObj["socks_base_port"] = config.proxy.socks_base_port;
    if (!config.proxy.xray_executable.empty()) {
        proxyObj["xray_executable"] = config.proxy.xray_executable;
    }
    proxyObj["use_singbox"] = config.proxy.use_singbox;
    if (!config.proxy.xray_asset_dir.empty()) {
        proxyObj["xray_asset_dir"] = config.proxy.xray_asset_dir;
    }
    if (!config.proxy.template_config_path.empty()) {
        proxyObj["template_config_path"] = config.proxy.template_config_path;
    }
    if (!config.proxy.singbox_executable.empty()) {
        proxyObj["singbox_executable"] = config.proxy.singbox_executable;
    }
    if (!config.proxy.singbox_asset_dir.empty()) {
        proxyObj["singbox_asset_dir"] = config.proxy.singbox_asset_dir;
    }
    if (!config.proxy.singbox_template_config_path.empty()) {
        proxyObj["singbox_template_config_path"] = config.proxy.singbox_template_config_path;
    }
    root["proxy"] = proxyObj;

    // network_monitor
    boost::json::object nmObj;
    nmObj["enabled"] = config.network_monitor.enabled;
    {
        boost::json::array urlsArr;
        for (const std::string& u : config.network_monitor.checkUrls) {
            urlsArr.emplace_back(u);
        }
        nmObj["check_urls"] = urlsArr;
    }
    nmObj["check_interval_ms"] = config.network_monitor.checkIntervalMs;
    nmObj["check_timeout_ms"] = config.network_monitor.checkTimeoutMs;
    {
        boost::json::object pdObj;
        pdObj["max_probes"] = config.network_monitor.maxProbes;
        nmObj["probe_on_disconnect"] = pdObj;
    }
    root["network_monitor"] = nmObj;

    // proxy_process_monitor
    boost::json::object pmObj;
    pmObj["enabled"] = config.proxy_process_monitor.enabled;
    pmObj["check_interval_ms"] = config.proxy_process_monitor.checkIntervalMs;
    root["proxy_process_monitor"] = pmObj;

    // standalone_pool
    {
        boost::json::object spObj;
        spObj["enabled"] = config.standalone_pool.enabled;
        spObj["mode"] = config.standalone_pool.mode;
        spObj["socksPort"] = config.standalone_pool.socksPort;
        spObj["apiPort"] = config.standalone_pool.apiPort;
        spObj["balancerStrategy"] = config.standalone_pool.balancerStrategy;
        boost::json::object obObj;
        obObj["type"] = config.standalone_pool.observatory.type;
        obObj["destination"] = config.standalone_pool.observatory.destination;
        obObj["intervalSec"] = config.standalone_pool.observatory.intervalSec;
        obObj["samplingCount"] = config.standalone_pool.observatory.samplingCount;
        obObj["timeoutSec"] = config.standalone_pool.observatory.timeoutSec;
        spObj["observatory"] = obObj;
        boost::json::object evObj;
        evObj["intervalSec"] = config.standalone_pool.evaluate.intervalSec;
        evObj["reportHealth"] = config.standalone_pool.evaluate.reportHealth;
        evObj["autoPruneDead"] = config.standalone_pool.evaluate.autoPruneDead;
        evObj["pruneFailStreak"] = config.standalone_pool.evaluate.pruneFailStreak;
        evObj["autoOptimize"] = config.standalone_pool.evaluate.autoOptimize;
        spObj["evaluate"] = evObj;
        root["standalone_pool"] = spObj;
    }

    return root;
}

} // namespace config
