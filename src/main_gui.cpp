// src/main_gui.cpp — Pure GUI entry point (WIN32 subsystem)
// No CLI command handling. No console output (stdout/stderr are discarded by WIN32 subsystem).

#include "UIApp.h"
#include "Utils.h"
#include "Logger.h"
#include "ConfigReader.h"
#include "config/ActiveConfigPath.h"
#include "i18n/I18n.h"
#include "i18n/LogBanner.h"
#include "CrashHandler.h"
#include "XrayManager.h"
#include "version.h"
#include "service/DatabaseConnectionService.h"
#include "service/DatabaseBootstrap.h"
#include "service/CloudSyncService.h"
#include "service/UploadGate.h"
#include "net/WebDavClient.h"
#include "net/WebDavCredentials.h"
#include "config/ConfigFileStore.h"
#include "config/ConfigJsonParser.h"
#include "config/sections/LogConfigParser.h"
#include "FullConfigTemplateDAO.h"
#include "SqlQuery.h"
#include "config/StepRegistry.h"

#include <sqlite3.h>
#include <windows.h>
#include <curl/curl.h>
#include <string>
#include <vector>
#include <filesystem>
#include <iostream>

// RAII guard for curl_global_init/cleanup — required before any libcurl function.
class CurlGlobalGuard {
public:
    CurlGlobalGuard() { curl_global_init(CURL_GLOBAL_ALL); }
    ~CurlGlobalGuard() { curl_global_cleanup(); }
    CurlGlobalGuard(const CurlGlobalGuard&) = delete;
    CurlGlobalGuard& operator=(const CurlGlobalGuard&) = delete;
};


namespace {

// ---------------------------------------------------------------------------
// Startup database bootstrap.
//
// Everything here runs BEFORE wxEntry(): there is no wxApp yet, so dialogs are
// plain Win32 MessageBoxW and any visible progress must be a raw Win32 window.
// ---------------------------------------------------------------------------

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    const int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                        static_cast<int>(text.size()), nullptr, 0);
    if (len <= 0) {
        return std::wstring();
    }
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], len);
    return out;
}

bool envFlagSet(const char* name) {
    char buf[8] = {0};
    const DWORD n = GetEnvironmentVariableA(name, buf, static_cast<DWORD>(sizeof(buf)));
    return n > 0 && n < sizeof(buf) && buf[0] == '1';
}

// Yes/No, always foreground. `defaultNo` selects the safe button.
bool askYesNo(const std::wstring& text, const wchar_t* caption, bool defaultNo) {
    UINT flags = MB_YESNO | MB_ICONWARNING | MB_SETFOREGROUND;
    if (defaultNo) {
        flags |= MB_DEFBUTTON2;
    }
    return MessageBoxW(nullptr, text.c_str(), caption, flags) == IDYES;
}

void showInfo(const std::wstring& text, const wchar_t* caption) {
    MessageBoxW(nullptr, text.c_str(), caption, MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
}

void showError(const std::wstring& text, const wchar_t* caption) {
    MessageBoxW(nullptr, text.c_str(), caption, MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

// Minimal visible progress window for the long operations (162 MB quick_check,
// cross-volume backup). Best effort: if window creation fails we still hold the
// wait cursor, so the app never looks completely dead.
class ProgressWindow {
public:
    void show(const std::wstring& stage) {
        if (hwnd_ == nullptr) {
            const wchar_t* kClass = L"ValidproxyDbBootstrapProgress";
            WNDCLASSW wc = {};
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = kClass;
            wc.hCursor = LoadCursorW(nullptr, IDC_WAIT);
            RegisterClassW(&wc);
            hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClass, L"数据库初始化",
                                    WS_POPUP | WS_BORDER, 200, 200, 320, 70, nullptr,
                                    nullptr, GetModuleHandleW(nullptr), nullptr);
            if (hwnd_ != nullptr) {
                ShowWindow(hwnd_, SW_SHOWNA);
            }
        }
        if (hwnd_ != nullptr) {
            RECT rc = {};
            GetClientRect(hwnd_, &rc);
            HDC dc = GetDC(hwnd_);
            if (dc != nullptr) {
                FillRect(dc, &rc, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
                SetBkMode(dc, TRANSPARENT);
                RECT textRc = rc;
                textRc.left += 12;
                DrawTextW(dc, stage.c_str(), -1, &textRc,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                ReleaseDC(hwnd_, dc);
            }
            UpdateWindow(hwnd_);
            MSG msg;
            while (PeekMessageW(&msg, hwnd_, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        if (cursorSet_ == false) {
            SetCursor(LoadCursorW(nullptr, IDC_WAIT));
            cursorSet_ = true;
        }
    }

    ~ProgressWindow() {
        if (hwnd_ != nullptr) {
            DestroyWindow(hwnd_);
        }
        if (cursorSet_) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        }
    }

private:
    HWND hwnd_ = nullptr;
    bool cursorSet_ = false;
};

// Patch config.json in place: only `database.path`, plus `sync.target_db` when it
// mirrored the old value. Never re-serialises AppConfig (unknown keys survive).
bool rewriteConfigPaths(const std::string& configPath, const std::string& oldPath,
                        const std::string& newPath, std::string& error) {
    try {
        config::ConfigFileStore store;
        const std::string raw = store.read(configPath);

        boost::system::error_code ec;
        boost::json::value root = boost::json::parse(raw, ec);
        if (ec || !root.is_object()) {
            error = ec ? ec.message() : "config.json is not a JSON object";
            return false;
        }

        boost::json::object& obj = root.as_object();

        // database.path (object form) — the parser also accepts a bare string; if
        // the user used that form we rewrite it in place as a string.
        boost::json::value* dbVal = obj.if_contains("database");
        if (dbVal == nullptr) {
            error = "config.json has no database section";
            return false;
        }
        bool pathWritten = false;
        if (dbVal->is_object()) {
            boost::json::object& db = dbVal->as_object();
            if (db.if_contains("path") != nullptr) {
                db["path"] = newPath;
                pathWritten = true;
            }
        }
        if (!pathWritten && dbVal->is_string()) {
            *dbVal = newPath;
            pathWritten = true;
        }
        if (!pathWritten) {
            error = "config.json has no database.path";
            return false;
        }

        // sync.target_db: only when it pointed at the old database.
        boost::json::value* syncVal = obj.if_contains("sync");
        if (syncVal != nullptr && syncVal->is_object()) {
            boost::json::object& sync = syncVal->as_object();
            boost::json::value* target = sync.if_contains("target_db");
            if (target != nullptr && target->is_string() &&
                std::string(target->as_string().c_str()) == oldPath) {
                *target = newPath;
            }
        }

        store.write(configPath, boost::json::serialize(root));
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

// Runs the probe -> prompt -> create -> rewrite -> reopen state machine.
// Returns true when a usable database path has been established.
bool bootstrapDatabase(const std::string& exeDir, const std::string& configPath,
                       config::AppConfig& appConfig) {
    const bool autoCreate = envFlagSet("VALIDPROXY_DB_AUTOCREATE");
    const bool autoRebuild = envFlagSet("VALIDPROXY_DB_AUTOREBUILD");

    for (int pass = 0; pass < 4; ++pass) {
        std::string dbPath = appConfig.database_path;
        const service::DbInspection insp = service::DatabaseBootstrap::inspect(dbPath);
        const service::DbStartupAction action = service::dbStartupAction(insp.probe);

        switch (action) {
            case service::DbStartupAction::Continue:
                return true;

            case service::DbStartupAction::Abort: {
                std::wstring text;
                if (insp.probe == service::DbProbe::BusyOrLocked) {
                    text = L"数据库正被其他实例占用：\n" + utf8ToWide(dbPath) +
                           L"\n\n请关闭其他正在运行的实例后重新启动。";
                } else if (insp.probe == service::DbProbe::WALReadOnlyBlocked) {
                    text = L"数据库文件有效，但所在目录不可写：\n" + utf8ToWide(dbPath) +
                           L"\n\nSQLite 的 WAL 模式需要在该目录创建 -shm/-wal 文件。"
                           L"请授予该目录写权限后重新启动。";
                } else {
                    text = L"无法访问数据库路径：\n" + utf8ToWide(dbPath) + L"\n\n" +
                           utf8ToWide(insp.detail);
                }
                showError(text, L"数据库不可用");
                Logger::write("database bootstrap aborted: probe=" +
                                      std::to_string(static_cast<int>(insp.probe)) +
                                      " path=" + utils::toWindowsPath(dbPath),
                              LogLevel::WARN);
                return false;
            }

            case service::DbStartupAction::PromptRecover: {
                const std::wstring text =
                        L"上次数据库重建未完成：\n" + utf8ToWide(dbPath) + L"\n\n发现备份：\n" +
                        utf8ToWide(insp.backupPath) + L"\n\n是否用该备份恢复？";
                if (!askYesNo(text, L"恢复数据库备份", /*defaultNo=*/true)) {
                    // Rename to .orphan so it can never be mistaken for recovered data.
                    const std::string orphan =
                            insp.backupPath.substr(0, insp.backupPath.find(".bak.")) +
                            ".orphan." + service::DatabaseBootstrap::backupTimestamp();
                    std::error_code ec;
                    std::filesystem::rename(insp.backupPath, orphan, ec);
                    Logger::write("database bootstrap: backup kept as " +
                                          utils::toWindowsPath(orphan),
                                  LogLevel::INFO);
                    appConfig.database_path = dbPath;
                    continue;  // re-probe: now MissingFile
                }
                std::string err;
                if (!service::DatabaseBootstrap::restoreFromBackup(insp.backupPath, dbPath,
                                                                   err)) {
                    showError(L"无法从备份恢复：\n" + utf8ToWide(err), L"恢复数据库备份");
                    return false;
                }
                showInfo(L"已从备份恢复：\n" + utf8ToWide(insp.backupPath) +
                                 L"\n\n注意：其中的数据可能早于你记忆中的状态。",
                         L"恢复数据库备份");
                appConfig.database_path = dbPath;
                continue;
            }

            case service::DbStartupAction::PromptCreate: {
                std::string target;
                std::string gateError;
                if (!service::DatabaseBootstrap::mayCreateAt(exeDir, dbPath, target,
                                                             gateError)) {
                    showError(L"无法在该位置创建数据库：\n" + utf8ToWide(gateError),
                              L"创建新数据库");
                    Logger::write("database bootstrap: mayCreateAt refused: " + gateError,
                                  LogLevel::ERR);
                    return false;
                }

                const bool samePath = (target == dbPath);
                std::wstring text;
                if (samePath) {
                    text = L"数据库不可用：" + utf8ToWide(insp.detail) +
                           L"\n当前配置路径：" + utf8ToWide(dbPath) +
                           L"\n\n说明：\n  · 将在原路径创建一个空数据库，不含任何订阅或代理"
                           L"\n  · 不会改写 config.json，路径保持不变"
                           L"\n\n是否在原路径创建新数据库？";
                } else {
                    text = L"数据库不可用：" + utf8ToWide(insp.detail) +
                           L"\n当前配置路径：" + utf8ToWide(dbPath) +
                           L"\n将创建到：" + utf8ToWide(target) +
                           L"\n\n说明：\n  · 将创建一个空数据库，不含任何订阅或代理"
                           L"\n  · 确认后将改写 config.json 的 database.path"
                           L"\n  · 原配置路径不会被删除，仍保持不可用"
                           L"\n\n是否创建新数据库？";
                }

                if (!autoCreate && !askYesNo(text, L"创建新数据库", /*defaultNo=*/false)) {
                    showInfo(L"请修正 config.json 的 database.path 后重新启动。",
                             L"创建新数据库");
                    return false;
                }

                std::string createError;
                bool created = false;
                {
                    ProgressWindow progress;
                    progress.show(L"正在创建数据库…");
                    created = service::DatabaseBootstrap::create(target, createError);
                }
                if (!created) {
                    showError(L"创建数据库失败：\n" + utf8ToWide(target) + L"\n\n" +
                                      utf8ToWide(createError),
                              L"创建新数据库");
                    Logger::write("database bootstrap: create failed: " + createError,
                                  LogLevel::ERR);
                    return false;
                }

                if (!samePath) {
                    std::string rewriteError;
                    if (!rewriteConfigPaths(configPath, dbPath, target, rewriteError)) {
                        Logger::write("database bootstrap: config rewrite failed: " +
                                              rewriteError,
                                      LogLevel::WARN);
                        showInfo(L"数据库已创建，但 config.json 未能更新：\n\n" +
                                         utf8ToWide(rewriteError) +
                                         L"\n\n请手工把 database.path 改为：\n" +
                                         utf8ToWide(target),
                                 L"创建新数据库");
                    }
                }
                appConfig.database_path = target;
                Logger::write("database bootstrap: created " + utils::toWindowsPath(target) +
                                      " (probe=" +
                                      std::to_string(static_cast<int>(insp.probe)) + ")",
                              LogLevel::REPORT);
                showInfo(L"已就绪数据库：\n" + utf8ToWide(target) +
                                 L"\n\n新建的库是空的（无订阅、无代理），请导入订阅。",
                         L"创建新数据库");
                continue;
            }

            case service::DbStartupAction::PromptRebuild: {
                const std::string victim = dbPath;

                std::wstring head;
                if (insp.probe == service::DbProbe::MissingCoreTables) {
                    head = L"数据库缺少必需的表/列，结构不可用";
                } else if (insp.probe == service::DbProbe::WalPending) {
                    head = L"数据库主文件为空，但存在未合并的 -wal（SQLite 无法回放）";
                } else {
                    head = L"文件不是有效的 SQLite 数据库";
                }

                std::string target = service::DatabaseBootstrap::defaultTargetPath(
                        exeDir, dbPath);
                const std::wstring text =
                        head + L"：" + utf8ToWide(insp.detail) + L"\n将备份的文件：" +
                        utf8ToWide(victim) + L"\n\n说明：\n  · 备份后会在 " +
                        utf8ToWide(target) +
                        L" 重建一个空数据库（原文件完整保留在备份里，可手工恢复）"
                        L"\n  · 新数据库不含任何订阅或代理\n\n是否备份并重建？（默认：否）";

                if (!autoRebuild && !askYesNo(text, L"备份后重建数据库", /*defaultNo=*/true)) {
                    if (insp.probe == service::DbProbe::WalPending) {
                        showInfo(L"该数据库的 -wal 未合并，无法自动恢复。\n"
                                 L"请保留原文件并联系支持，或手工处理后再启动。",
                                 L"备份后重建数据库");
                    }
                    return false;
                }

                std::string bak;
                {
                    ProgressWindow progress;
                    progress.show(L"正在备份数据库…");
                    if (!service::DatabaseBootstrap::backupFile(victim, bak)) {
                        showError(L"备份失败，原文件未改动：\n" + utf8ToWide(victim),
                                  L"备份后重建数据库");
                        return false;
                    }

                    progress.show(L"正在重建数据库…");
                    std::string createError;
                    if (!service::DatabaseBootstrap::create(target, createError)) {
                        std::string restoreError;
                        const bool restored = service::DatabaseBootstrap::restoreFromBackup(
                                bak, victim, restoreError);
                        showError(L"重建失败：\n" + utf8ToWide(createError) +
                                          (restored ? L"\n\n已把备份还原回原路径。"
                                                    : L"\n\n备份未能还原，请手工从以下文件恢复：\n" +
                                                              utf8ToWide(bak)),
                                  L"备份后重建数据库");
                        return false;
                    }
                }

                service::DatabaseBootstrap::removeBackup(bak);

                if (target != dbPath) {
                    std::string rewriteError;
                    if (!rewriteConfigPaths(configPath, dbPath, target, rewriteError)) {
                        Logger::write("database bootstrap: config rewrite failed: " +
                                              rewriteError,
                                      LogLevel::WARN);
                    }
                }
                appConfig.database_path = target;
                Logger::write("database bootstrap: rebuilt " + utils::toWindowsPath(target) +
                                      " from " + utils::toWindowsPath(bak),
                              LogLevel::REPORT);
                showInfo(L"已重建数据库：\n" + utf8ToWide(target) +
                                 L"\n\n旧库已备份并保留在：\n" + utf8ToWide(bak),
                         L"备份后重建数据库");
                continue;
            }
        }
    }

    showError(L"数据库初始化陷入重复状态，已中止。", L"数据库不可用");
    return false;
}

} // namespace

int main(int argc, char* argv[]) {
    crash::installHandler();
    std::string exeDir = utils::getExecutableDir();
    std::string configPath = (std::filesystem::path(exeDir) / "config.json").string();

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "-c" || arg == "--config") {
            if (++i < argc) {
                std::filesystem::path fp(argv[i]);
                if (fp.is_relative()) {
                    fp = std::filesystem::path(exeDir) / fp;
                }
                configPath = fp.lexically_normal().string();
            }
        }
    }

    CurlGlobalGuard curlGuard;  // RAII guard — curl_global_init before any libcurl call

    // Create log directory
    std::filesystem::path logDir = std::filesystem::path(exeDir) / "log";
    if (!std::filesystem::exists(logDir)) {
        std::filesystem::create_directory(logDir);
    }

    // Pre-parse the "log" section BEFORE Logger::init so the file level from
    // config.json takes effect from the very first log write. ConfigReader::load()
    // itself writes DEBUG SQL diagnostics, so the level must already be applied
    // before load() runs (otherwise INFO/DEBUG lines below the configured
    // file_level would be written to the log file).
    try {
        config::ConfigFileStore fileStore;
        std::string jsonStr = fileStore.read(configPath);
        config::ConfigJsonParser jsonParser;
        boost::json::value root = jsonParser.parse(jsonStr);
        if (!root.is_null() && root.is_object()) {
            config::AppConfig preConfig;
            config::LogConfigParser logParser;
            logParser.parse(root, preConfig, exeDir);
            Logger::init(logDir.string(), "ui",
                         Logger::stringToLevel(preConfig.log_file_level),
                         Logger::stringToLevel(preConfig.log_console_level));
            Logger::setFileEnabled(preConfig.log_enabled);
        } else {
            // Empty/invalid JSON: fall back to default levels (file DEBUG / console INFO)
            Logger::init(logDir.string(), "ui");
        }
    } catch (...) {
        // Config file missing or unreadable: fall back to default levels
        Logger::init(logDir.string(), "ui");
    }

    // Initialize Logger FIRST (before load / DB open), so ConfigReader::load()
    // can log diagnostics and any early failure is visible in the log file.
    Logger::write("gui entry: Logger::init completed", LogLevel::INFO);
    Logger::setConsoleEnabled(false);

    // Load configuration
    // R1: config.json missing -> pull it from WebDAV. Credentials are embedded at
    // build time (scripts/provision_webdav_secret.py); when they are absent this
    // is a no-op and the existing "configuration missing" error path still runs.
    {
        std::error_code cfgEc;
        if (!std::filesystem::exists(configPath, cfgEc)) {
            net::WebDavCredentials credentials;
            std::string credError;
            if (!net::WebDavCredentialsStore::load(credentials, credError)) {
                Logger::write("startup: config.json missing; " + credError, LogLevel::WARN);
            } else {
                net::WebDavClient webdavClient(credentials, "https://dav.jianguoyun.com/dav/");
                const service::CloudSyncResult pulled =
                        service::CloudSyncService::pullConfigOnly(webdavClient, "validproxy",
                                                                  configPath);
                Logger::write("startup: " + (pulled.ok ? pulled.detail : pulled.error),
                              pulled.ok ? LogLevel::REPORT : LogLevel::WARN);
            }
        }
    }

    std::optional<config::AppConfig> appConfig = config::ConfigReader::load(configPath);
    if (!appConfig) {
        Logger::write("Failed to load configuration file: " + utils::toWindowsPath(configPath), LogLevel::ERR);
        Logger::close();
        return 1;
    }

    // 发布进程级生效配置路径（-c/--config），保证 saveConfig / ConfigDialog 落到同一文件。
    config::setActiveConfigPath(configPath);

    // 启动语言：仅 config.ui.language → 内置 zh-CN；无环境变量覆盖层。
    {
        i18n::Language startupLang = i18n::Language::ZhCN;
        if (!i18n::parseLanguage(appConfig->ui.language, startupLang)) {
            startupLang = i18n::Language::ZhCN;
        }
        i18n::setLanguage(startupLang);
    }

    // 会话语言横幅 + 跨会话语言变更分隔线（语言中立，写入本日志文件开头）。
    i18n::LogBanner::writeSessionBanner(logDir.string(), Logger::getFilePath(),
                                       i18n::languageCode(i18n::language()));

    // Apply config-specified log levels
    Logger::setFileEnabled(appConfig->log_enabled);
    Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
    Logger::setConsoleLevel(Logger::stringToLevel(appConfig->log_console_level));

    // Log version info
    Logger::write(std::string(APP_NAME) + " v" + APP_VERSION + " (" + APP_GIT_TAG + ")", LogLevel::REPORT);
    Logger::write(std::string("Build: ") + APP_BUILD_TYPE + " | " + APP_BUILD_TIME, LogLevel::REPORT);
    Logger::write(std::string("Compiler: ") + __VERSION__, LogLevel::REPORT);
    // Evidence for the upload gate: whether VALIDPROXY_UPLOAD_TOKEN matched the
    // compiled-in value decides if the "upload to cloud" menu item is enabled.
    Logger::write(std::string("Upload gate: ") +
                          (service::isUploadEnabled() ? "enabled" : "disabled "
                           "(VALIDPROXY_UPLOAD_TOKEN absent or mismatched)"),
                  LogLevel::REPORT);

    // Bootstrap the database: read-only probe -> prompt -> create/rebuild -> rewrite.
    // This replaces the old behaviour where SQLITE_OPEN_CREATE silently produced a
    // 0-byte database (and later blew up with "no such table" everywhere).
    if (!bootstrapDatabase(exeDir, configPath, *appConfig)) {
        Logger::close();
        return 1;
    }

    service::DatabaseConnectionService dbService;
    sqlite3* db = dbService.open(appConfig->database_path,
                                 service::OpenMode::ReadWriteExisting);
    if (!db) {
        std::wstring errMsg = L"Failed to open database.\n\nDatabase path:\n" +
                              utf8ToWide(utils::toWindowsPath(appConfig->database_path));
        showError(errMsg, L"数据库不可用");
        Logger::close();
        return 1;
    }

    // Template seeds come from schema_init.sql now. Re-running the C++ seeder would
    // write rows whose BundleChain is empty, so only the missing-table DDL runs.
    {
        db::FullConfigTemplateDAO configTemplateDao(db);
        configTemplateDao.createTableIfNotExists();
    }

    // Seed default SQL query library entries if table is empty
    {
        db::SqlQueryDAO sqlQueryDao(db);
        sqlQueryDao.createTableIfNotExists();
        sqlQueryDao.seedDefaults(appConfig->sql_query, appConfig->sql_by_subid);
    }

    // Register all ConfigPipeline step handlers
    config::registerAllStepHandlers();

    // Strip non-wxWidgets flags from argv before passing to wxEntry
    // (-ui, --ui, --gui are CLI-legacy flags from the old hybrid binary)
    std::vector<char*> wxArgv;
    wxArgv.reserve(argc);
    wxArgv.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if (a != "-ui" && a != "--ui" && a != "--gui") {
            wxArgv.push_back(argv[i]);
        }
    }
    int wxArgc = static_cast<int>(wxArgv.size());

    // Enter wxWidgets event loop (blocks until GUI exits)
    wxApp::SetInstance(new UIApp(*appConfig, db));
    int ret = wxEntry(wxArgc, wxArgv.data());

    // Cleanup after GUI exits
    XrayManager::release();
    // The sqlite3 handle is owned by the AppController (it closes the current
    // handle and every handle a database switch replaced, see
    // AppController::~AppController). Closing it here would close an
    // already-closed handle after a database switch.
    Logger::close();

    return ret;
}
