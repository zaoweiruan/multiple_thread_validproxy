# Code Refactoring Phase 2 Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use executing-plans or subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor codebase by removing dead code, cleaning redundant includes, and merging duplicated logic.

**Architecture:** Four independent cleanup tasks executed sequentially with verification between each.

**Tech Stack:** C++17, CMake, MinGW/GCC, GoogleTest

**Status:** ✅ **COMPLETED** (2026-06-17)

---

## Task 1: Parameterize Hardcoded Paths in CMakeLists.txt

**Status:** ✅ Complete

- [x] All 21 paths converted to CMake cache variables (BOOST_ROOT, VCPKG_ROOT, WX64DEVKIT_ROOT, GTEST_ROOT)
- [x] Build works with `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug`
- [x] All targets compile successfully

## Task 2: Decompose SubitemUpdaterV2

**Status:** ✅ Complete

- [x] **SubscriptionParser** created in `include/update/SubscriptionParser.h` and `src/update/SubscriptionParser.cpp`
- [x] Parses vmess://, vless://, ss://, trojan://, hysteria2:///hy2:// links
- [x] Base64 decode, URL decode, address/port parsing implemented

## Task 3: Update CMakeLists.txt for New Structure

**Status:** ✅ Complete

- [x] Added `src/update/SubscriptionParser.cpp` to CORE_SOURCES
- [x] Added test_subscription_parser target with proper dependencies

## Task 4: Create Unit Tests for Decomposed Classes

**Status:** ✅ Complete

- [x] Created `tests/test_subscription_parser.cpp`
- [x] Tests cover: VLESS, Trojan, Hysteria2, mixed protocols, invalid/empty content
- [x] All 9 tests pass: ✅

---

## Summary

| Task | Status | Details |
|------|--------|---------|
| 1 - CMakeLists 参数化 | ✅ | 21 环境变量路径 |
| 2 - SubscriptionParser 创建 | ✅ | vmess/vless/ss/trojan/hy2 解析 |
| 3 - CMakeLists 更新 | ✅ | 新增源文件和测试目标 |
| 4 - 单元测试 | ✅ | 9/9 通过 |