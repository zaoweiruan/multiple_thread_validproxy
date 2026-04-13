# AGENTS.md - multiple_thread_validproxy

## 1. 项目概述

- **语言**: C++ 20/17 (使用 C++17 features like std::optional)
- **类型**: 代理验证工具
- **目标平台**: Windows
- **构建系统**: CMake (待实现)
- **项目状态**: 初期开发

## 2. 目录结构

```
multiple_thread_validproxy/
├── include/              # 公共头文件 (数据库模型定义)
├── bin/
│   └── main_config_4.json  # xray 配置文件模板
├── build/                # 构建输出 (gitignore)
└── AGENTS.md
```

## 3. 构建/测试命令 (Windows + GCC/MinGW)

```bash
mkdir build && cd build
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=debug
cmake --build . --parallel 8
./validproxy.exe
ctest -V
cmake --build . --target clean

# 运行单个测试
ctest -R TestName -V
./build/tests/test_name
```

## 4. 第三方依赖

| 库 | 版本 | 安装方式 | 路径 |
|---|---|---|---|
| Boost | 1.80+ | 手动 | D:\boost_1_88_0 |
| boost/json | - | Boost 已包含 | - |
| curl | latest | vcpkg | - |
| sqlite3 | latest | vcpkg | - |
| xray-core | latest | 手动 | ../Xray-core |

## 5. 代码规范

### 命名约定

- **类/结构体**: PascalCase (e.g., `Profileitem`, `ProfileitemDAO`)
- **函数/方法**: camelCase (e.g., `getAll()`, `fromStmt()`)
- **成员变量**: snake_case (e.g., `indexid`, `configtype`)
- **常量**: kPascalCase 或 UPPER_SNAKE_CASE (e.g., `kMaxSize`, `MAX_BUFFER_SIZE`)
- **宏**: UPPER_SNAKE_CASE (e.g., `LOG_ERROR`)

### 文件组织

- **头文件**: `.h` (当前使用) 或 `.hpp`
- **源文件**: `.cpp`
- **每个类一个头文件** (除非紧密相关)
- **使用 include guards**: `#ifndef`, `#define`, `#endif`

### 代码风格示例

```cpp
#ifndef DB_PROFILEITEM_H
#define DB_PROFILEITEM_H

#include <string>
#include <vector>
#include <optional>
#include <sqlite3.h>
#include <iostream>
#include <sstream>

namespace db {
namespace models {

struct Profileitem {
  std::string indexid;
  std::string configtype;
  int muxEnabled;
  
  static Profileitem fromStmt(sqlite3_stmt* stmt);
  std::string toString() const;
};

class ProfileitemDAO {
private:
  sqlite3* db_;
  
public:
  explicit ProfileitemDAO(sqlite3* db) : db_(db) {}
  std::vector<Profileitem> getAll();
};

} // namespace models
} // namespace db

#endif // DB_PROFILEITEM_H
```

### 类型使用

- 使用 `std::string` 而非 `char*`
- 使用 `std::vector<T>` 而非原始数组
- 使用 `std::optional<T>` 表示可选值
- 使用 `sqlite3*` 处理数据库连接
- 避免使用 C 风格类型

### 错误处理

- 使用 `std::cerr` 输出错误信息
- 返回空容器表示错误 (如 `std::vector<T>{}`)
- 检查 SQLite 返回值 (`SQLITE_OK`, `SQLITE_ROW` 等)

### 导入顺序

1. 系统头文件 (`<string>`, `<vector>` 等)
2. 第三方库头文件 (`<sqlite3.h>`, `<iostream>`)
3. 项目内部头文件

## 6. 开发工作流

### 新建模型类

```cpp
// include/{模块}/{类名}.h
#ifndef DB_MODULENAME_H
#define DB_MODULENAME_H

#include <string>
#include <vector>

namespace db {
namespace models {

struct Modulename {
  std::string id;
  std::string name;
  
  static Modulename fromStmt(sqlite3_stmt* stmt);
};

class ModulenameDAO {
private:
  sqlite3* db_;
  
public:
  explicit ModulenameDAO(sqlite3* db) : db_(db) {}
  std::vector<Modulename> getAll();
};

} // namespace models
} // namespace db

#endif // DB_MODULENAME_H
```

### 调试配置

- IDE: Eclipse + CMake Tools + Ninja
- 调试器: GDB 或 LLDB

## 7. 测试框架 (待配置)

建议使用 Google Test:

```cmake
FetchContent_Declare(googletest URL https://github.com/google/googletest/archive/refs/tags/v1.13.0.tar.gz)
FetchContent_MakeAvailable(googletest)

add_executable(test_model tests/test_model.cpp)
target_link_libraries(test_model PRIVATE gtest_main ModelLib)
add_test(NAME ModelTest COMMAND test_model)
```

## 8. 功能待实现

1. 扫描项目 ../validproxy 获取功能列表
2. 读取 sqlite3 数据库 guiNDB.db，表 Profileitem、Profileexitem
3. 根据 configType 生成 JSON 配置 (3=SS, 1=VMess, 5=VLESS, 6=Trojan)
4. xray API 方式启动 (端口 10086，stdin 注入配置)
5. curl 测试代理连通性 (https://www.google.com/generate_204)
6. 多线程并发测试多个代理

## 9. 常见问题

**Q: 如何添加新依赖?**
A: 编辑 CMakeLists.txt，使用 find_package 或 FetchContent

**Q: 内存泄漏检测?**
A: 使用 AddressSanitizer: `-fsanitize=address` (Linux/Mac)