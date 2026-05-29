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

- generator
- show-sub
- find-proxy
- test-sub
- update
- dedup
- tourl