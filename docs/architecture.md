# Architecture

## Core Modules

- XrayManager (singleton)
  - Responsibility: global coordination of proxy and config workflow
  - Depends on: ProxyFinder, ConfigGenerator, DatabaseHelper

- ProxyFinder
  - Finds available proxies from sources

- ConfigGenerator
  - Generates Xray configs from proxy data

- ConfigReader
  - Parses existing configurations

- SubitemUpdater
  - Updates subscription items

- ProxyBatchTester
  - Tests proxies in batch

---

## Data Layer

- Database: SQLite
- Access via: DatabaseHelper

### Models:
- Profileitem
- Subitem
- Profileexitem

### Pattern:
- DAO-based access

---

## Data Flow

1. ProxyFinder → fetch proxies  
2. SubitemUpdater → normalize data  
3. DatabaseHelper → persist  
4. ConfigGenerator → generate configs  
5. ProxyBatchTester → validate  

---

## Build System

- CMake
- C++17
- Dependencies:
  - Boost
  - SQLite
  - Curl
  - Threads

---

## Testing

- Framework: Google Test
- Types:
  - Unit tests
  - Integration tests

---

## CLI Modes

### CLI Commands (when arguments provided):
- generator - Generate outbound JSON for a profile
- show-sub - Show all subscriptions  
- find-proxy - Find first working proxy
- findminproxy - Find working proxy sorted by delay
- test-sub - Test proxies from subscription
- update - Update subscription(s)
- dedup - Remove duplicate proxies
- tourl - Export proxies to share links
- sync - Sync valid proxies between databases
- import-sub - Batch import subscriptions

### GUI Mode (default):
- No arguments → launches desktop GUI automatically
- `-ui`/`--ui` explicit flag also launches GUI (backward compatible)

---