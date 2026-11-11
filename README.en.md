# validproxy — Proxy Validation & Network Forwarding Manager

> **Version**: v1.6.0 · **Git**: `84fdfae` · **Platform**: Windows (MinGW/GCC) · **Language**: C++17

`validproxy` is a proxy validation and network-forwarding manager built on **Xray-core**, offering both a **wxWidgets GUI** and a **command-line (CLI)** interface. It targets the v2rayN ecosystem and supports subscription management, batched proxy connectivity testing, deduplication, share-link import/export, standalone proxy / proxy-pool lifecycle and monitoring, plus real-time log and statistics panels.

---

## Contents

- [Features](#features)
- [Tech Stack](#tech-stack)
- [Requirements](#requirements)
- [Build & Test](#build--test)
- [CLI Usage](#cli-usage)
- [Configuration & Data](#configuration--data)
- [Project Structure](#project-structure)
- [Core Modules](#core-modules)
- [Debug Toolchain](#debug-toolchain)
- [Documentation Index](#documentation-index)
- [Development Conventions](#development-conventions)

---

## Features

- **Subscription management**: fetch/parse multiple subscription sources (URL/local file), supporting VMess/VLESS/Trojan/Shadowsocks/Hysteria2/Sing-Box and more, with high-efficiency deduplication, 404 gone-marking, and update-interval control.
- **Batched connectivity testing**: multi-threaded concurrent scheduler (`ProxyBatchTester`) with dynamic Xray instance injection + cURL probing, network-disconnect pause/resume, and automatic instance relaunch.
- **Proxy lookup**: `-F` quick-find first working proxy; `-FMIN` returns the lowest-latency premium proxy.
- **Standalone proxy / proxy pool**: single-proxy start/stop/monitoring, pool concurrency management, dynamic port allocation (`PortManager`) and reclamation.
- **Share links**: standard share-link import/export (`ShareLink`); `-TU` exports all valid proxies.
- **Deduplication**: `-D` triggers the dedup algorithm to remove highly duplicated/invalid proxies (including config-error-phase cleanup).
- **GUI panels**: subscription panel, proxy list (delay/health/starts/runtime sortable), log panel (six levels + statistics dialog), and task-progress monitor.
- **Cross-session memory & doc-driven**: Caura cross-session memory + `docs/INDEX.md` document index + role map (see [Development Conventions](#development-conventions)).

---

## Tech Stack

| Item | Description |
|------|-------------|
| Language | C++17 (**hard constraint: no `auto` type deduction**) |
| Build | CMake + Ninja (Debug) |
| UI | wxWidgets 3.2+ (wxMSW) |
| Proxy core | Xray-core (gRPC API injection / CLI fallback) |
| Networking | cURL (HTTP/HTTPS, RAII wrapper `CurlEasyHandle`) |
| Storage | SQLite (DAO data-access layer `DatabaseHelper`) |
| Testing | Google Test + ctest + UI automation (`UITests`, Win32/UIA/COM) |
| Platform | Windows (MinGW/GCC) |

---

## Requirements

- **Windows** + MinGW GCC (bundled with CLion or w64devkit)
- **CMake ≥ 3.x** + **Ninja**
- **wxWidgets 3.2+** (MSW, vcpkg: `x64-mingw-dynamic`)
- **Xray-core** (`xray.exe`) and optionally **Sing-Box** (`sing-box.exe`)
- **geoip.dat / geosite.dat** (`xray_location_asset` path)
- **vcpkg** (default `D:/vcpkg`; dependencies: Boost, wxWidgets, SQLite, cURL, etc.)

---

## Build & Test

```powershell
# 1. Configure the Debug build
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug

# 2. Parallel build (8 threads)
cmake --build build --parallel 8

# 3. Run
.\build\validproxy.exe            # GUI entry
.\build\validproxy-cli.exe        # CLI entry (no args = default batched test)

# 4. Full integration tests
ctest -V

# 5. Single test by prefix
ctest -R DedupTest -V

# 6. One-click UI acceptance (MUST be all green before shipping UI changes)
.\scripts\build-and-test.bat
```

> **Build rule**: CMake outputs only to `bin/` and `tests/`; `bin/worker/` is the runtime working directory (maintained manually). Debug/fix verification must use the latest artifacts under `bin/` — **never** `bin/worker/validproxy.exe` (stale copy).

---

## CLI Usage

`validproxy-cli.exe` quick reference:

```text
(no args)                   Default batched test: run connectivity test on all proxies in DB
-c, --config <path>         Specify config path (default: bin/config.json)
-show-sub                   Print all subscriptions to console
-G, -generator <id>         Generate standard outbound Xray JSON by indexId
-F, -find-proxy             Quick-find first fully working proxy
-FMIN, -findminproxy        Test and return the lowest-latency premium proxy
-U, -update <id>            Force-update a single subscription
-UA, -update-all            Batch-update all enabled subscriptions
-T, -test-sub <id>          Run connectivity test on all proxies in a subscription
-TU, -tourl                 Export all valid (delay > 0) proxies as share links
-D, -dedup                  Run dedup to remove highly duplicated proxies
-S, -sync [src[:dst]]       Bidirectional/single-direction DB sync
-IS, -import-sub-config <f> Batch-import subscription nodes from local file or URL
-h, --help                  Print CLI help menu
```

---

## Configuration & Data

| Data/Config | Path | Description |
|-------------|------|-------------|
| Production runtime DB | `bin/worker/guindb.db` | Actual business storage, set by `database.path` in `bin/config.json` |
| Global production config | `bin/config.json` | Default timeout, concurrency, priority fields, etc. |
| Default test DB (full) | `test/guindb.db` | 53,837 profiles / 44 SubIDs, used by all tests except import/migration |
| Test DB (slim) | `test/guiNDB_empty.db` | 711 profiles / 8 SubIDs, dedicated to import/migration regression tests |
| Test-only config | `bin/test_config.json` | Points to `test/guiNDB.db`, prevents polluting production |

---

## Project Structure

```text
./src/            Sources — Xray core control & business logic
./include/        Headers
./src/ui/         UI layer — wxWidgets (AppController, MainFrame, 6 panels)
./tests/          Unit tests — Google Test (all compilable test sources here)
./test/           Test data — DB/config/mock scripts only, no sources
./bin/            Production runtime (config.json + worker/guindb.db)
./docs/           Project docs (docs/INDEX.md is the categorized index entry)
./skills/         Agent skills
./scripts/        Python/batch helper scripts
./temp/           Debug scratch files
```

---

## Core Modules

| Module | File | Responsibility |
|--------|------|----------------|
| `XrayManager` | `src/XrayManager.cpp` | Xray instance singleton management |
| `XrayInstance` | `src/XrayInstance.cpp` | Single Xray process lifecycle |
| `XrayApi` | `src/XrayApi.cpp` | gRPC API communication with Xray |
| `ProxyFinder` | `src/ProxyFinder.cpp` | Proxy lookup strategy (F/FMIN) |
| `ProxyBatchTester` | `src/ProxyBatchTester.cpp` | Multi-threaded test scheduler engine |
| `ProxyTester` | `src/ProxyTester.cpp` | Single-proxy test core (cURL + Xray) |
| `ConfigGenerator` | `src/ConfigGenerator.cpp` | Dynamic Xray JSON config generation |
| `ConfigReader` | `src/ConfigReader.cpp` | Global `config.json` parsing |
| `SubitemUpdaterV2` | `src/SubitemUpdaterV2.cpp` | Subscription update, parse, high-efficiency dedup |
| `DatabaseHelper` | `include/DatabaseHelper.h` | SQLite DAO data-access layer |
| `Logger` | `src/Logger.cpp` | Global log (TRACE < DEBUG < INFO < REPORT < WARN < ERR) |
| `ShareLink` | `src/ShareLink.cpp` | Share-link export & parse |
| `PortManager` | `src/PortManager.cpp` | Dynamic local port allocation/reclamation |
| `UrlFetcher` | `src/UrlFetcher.cpp` | Low-level cURL HTTP/HTTPS requester |
| `CurlEasyHandle` | `include/CurlEasyHandle.h` | cURL RAII safe wrapper (header-only) |

**Data models**: `Profileitem` (base config item), `Subitem` (subscription node), `ProfileExItem` (extended proxy model carrying test status/latency).

---

## Debug Toolchain

```powershell
# 1. Sanitizer build (leak/overflow/UB detection)
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build --parallel 8 && ctest -V

# 2. Coverage build
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
cmake --build build --parallel 8 && .\scripts\run_coverage.ps1

# 3. Static analysis (cppcheck)
.\scripts\run_static_analysis.ps1

# 4. Logger level control (bin/config.json)
#    log.file_level: "TRACE" | "DEBUG" | "INFO" | "REPORT" | "WARN" | "ERR"
```

---

## Documentation Index

- **`docs/INDEX.md`** — the single categorized index for all static docs, historical analysis and change records (AI search entry).
- `docs/plans/project-plans-tracker.md` — engineering progress tracker.
- `docs/specs/` — technical specs (Spec/SDD) for core refactor modules.
- `docs/bugfix/` — root-cause analysis & fix records.
- `docs/reviews/` — independent review records (three-perspective / 5-persona parallel).
- `docs/design/` — UI design, invalid-filtering and other baseline design specs.
- `CHANGELOG.md` — version history (currently v1.6.0).

---

## Development Conventions

- **C++17, no `auto` anywhere** (explicit types required).
- **Docs-first**: before any feature/refactor code change, create a design doc/spec under `docs/` named `[YYYY-MM-DD]-[DocType]-[Module]-[Version].md`, then register it in `docs/INDEX.md`.
- **TDD**: provide Google Test (`TEST_F`) cases in `tests/` alongside (or before) implementation; test sources in `tests/`, data in `test/`.
- **Role map** (`AGENTS.md` §6.4): planner → implementer → tester → reviewer → delivery; debugger can intervene at any stage; ui-analyst handles GUI screenshot/interface analysis.
- **CLion MCP first** (`AGENTS.md` §6.3.5): prefer CLion for debugging/static diagnostics; disable `apply_patch` for CJK-text edits — use the `edit` tool.
- **Doc/rule entry points**: `AGENTS.md` is the AI context injection point; `docs/INDEX.md` is the categorized document index.

---

## License

See the repository license file (this tool builds on the v2rayN / Xray-core open-source ecosystem).
