@echo off
rem scripts\prepare-ui-sandbox.bat - reset test/ui-sandbox to a clean state.
rem Isolation red line: ONLY test\ui-sandbox\ is ever touched here. The
rem production DBs (bin\worker\guindb.db, test\guindb.db) are never written.
setlocal
set "ROOT=%~dp0.."
set "SBX=%ROOT%\test\ui-sandbox"
if not exist "%SBX%" mkdir "%SBX%"

copy /y "%ROOT%\test\guiNDB_empty.db" "%SBX%\ui-test.db" >nul || (echo [FAIL] copy db & exit /b 1)

> "%SBX%\config.json" (
echo {
echo   "database": { "path": "%ROOT:\=/%/test/ui-sandbox/ui-test.db" },
echo   "xray": { "workers": 2, "start_port": 11080, "api_port": 10081 },
echo   "test": { "url": "https://www.google.com/generate_204", "timeout_ms": 5000 },
echo   "log": { "enabled": true, "console_level": "WARN", "file_level": "INFO" },
echo   "subscription": { "priority_mode": "proxy_first", "check_auto_update_interval": false },
echo   "dedup": { "enabled": false, "dedup_after_update": false, "blacklist_threshold": 5 },
echo   "sync": { "source_db": "%ROOT:\=/%/test/ui-sandbox/ui-test.db", "target_db": "%ROOT:\=/%/test/ui-sandbox/ui-test.db" },
echo   "notification": { "enabled": false, "on_update": false, "on_test": false },
echo   "network_monitor": { "enabled": false, "check_urls": ["https://www.baidu.com"], "check_interval_ms": 60000, "check_timeout_ms": 5000,
echo     "probe_on_disconnect": { "max_probes": 1 } },
echo   "proxy_process_monitor": { "enabled": true, "check_interval_ms": 5000 },
echo   "standalone_pool": {
echo     "enabled": true, "selection": "high_priority", "auto_start": false,
echo     "min_members": 1, "max_members": 8, "eval_interval_ms": 5000,
echo     "health_check": { "enabled": true, "timeout_ms": 5000, "url": "https://www.google.com/generate_204" },
echo     "evaluate": { "reportHealth": false, "autoPruneDead": false, "autoOptimize": false }
echo }
)
)
if not exist "E:\v2rayN-windows-64\bin\xray\xray.exe" (echo [FAIL] xray path invalid - popup guard D8 & exit /b 1)
if not exist "%ROOT%\bin\validproxy.exe" (echo [FAIL] bin\validproxy.exe missing - build first & exit /b 1)
echo [PASS] sandbox ready: %SBX%
exit /b 0
