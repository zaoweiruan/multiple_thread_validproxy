# 代码重构 Phase 1 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 清理代码库中的死代码、冗余头文件，消除重复逻辑

**Architecture:** 三阶段低风险 → 高风险: (1) 清理死代码、冗余头文件, (2) 合并重复函数, (3) 拆分大型函数

**Tech Stack:** C++17, CMake, Google Test

---

## Task 1: 删除死代码文件

**Files:**
- Delete: `src/ProxyFinder_temp.cpp`
- Delete: `src/ProxyFinder_part1.cpp`

- [ ] **Step 1: 移除 ProxyFinder_temp.cpp**
- [ ] **Step 2: 移除 ProxyFinder_part1.cpp**
- [ ] **Step 3: 从 CMakeLists.txt 移除对应源引用**
- [ ] **Step 4: 构建验证**
- [ ] **Step 5: 提交**

## Task 2: 清理冗余头文件

**Files:**
- Modify: `include/ProxyFinder.h:5` - 移除 `#include <curl/curl.h>`
- Modify: `include/CurlEasyHandle.h:8` - 移除 `#include <utility>`

- [ ] **Step 1: 编辑 ProxyFinder.h 移除冗余 curl/curl.h 引用**
- [ ] **Step 2: 编辑 CurlEasyHandle.h 移除未使用的 utility 引用**
- [ ] **Step 3: 构建验证**
- [ ] **Step 4: 提交**

## Task 3: 合并 isValidNetwork() 重复逻辑

**Files:**
- Create: `include/NetworkUtils.h` - 声明共享函数
- Create: `src/NetworkUtils.cpp` - 实现共享函数
- Modify: `src/ConfigGenerator.cpp` - 使用共享函数
- Modify: `src/SubitemUpdaterV2.cpp` - 使用共享函数

- [ ] **Step 1: 创建 NetworkUtils.h 声明**
- [ ] **Step 2: 创建 NetworkUtils.cpp 实现**
- [ ] **Step 3: 更新 ConfigGenerator.cpp 使用共享函数**
- [ ] **Step 4: 更新 SubitemUpdaterV2.cpp 使用共享函数**
- [ ] **Step 5: 构建验证**
- [ ] **Step 6: 提交**

## Task 4: 合并代理类型字符串映射

**Files:**
- Modify: `include/ProxyFinder.h` - 添加公共映射函数声明
- Modify: `src/ProxyFinder.cpp:23-34` - 提升作用域
- Modify: `src/ShareLink.cpp:485-499` - 使用共享映射

- [ ] **Step 1: 在 ProxyFinder.h 添加 configTypeToProtocol() 声明**
- [ ] **Step 2: 在 ProxyFinder.cpp 将映射提升为公共静态函数**
- [ ] **Step 3: 在 ShareLink.cpp 包含 ProxyFinder.h 并调用映射**
- [ ] **Step 4: 构建验证**
- [ ] **Step 5: 提交**