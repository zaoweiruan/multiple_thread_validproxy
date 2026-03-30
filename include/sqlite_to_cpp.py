#!/usr/bin/env python3
"""
SQLite 表转 C++ 结构体代码生成器
用法: python sqlite_to_cpp.py database.db
"""

import sqlite3
import sys
import os
from typing import Dict, List, Tuple
import re

def sqlite_type_to_cpp_type(sqlite_type: str, is_nullable: bool = False) -> str:
    """将 SQLite 类型映射到 C++ 类型"""
    sqlite_type = (sqlite_type or '').lower()
    
    type_mapping = {
        'integer': 'int',
        'int': 'int',
        'tinyint': 'int8_t',
        'smallint': 'int16_t',
        'mediumint': 'int32_t',
        'bigint': 'int64_t',
        'unsigned big int': 'uint64_t',
        'int2': 'int16_t',
        'int8': 'int64_t',
        
        'real': 'double',
        'double': 'double',
        'float': 'float',
        
        'text': 'std::string',
        'varchar': 'std::string',
        'char': 'std::string',
        'clob': 'std::string',
        
        'blob': 'std::vector<uint8_t>',
        'boolean': 'bool',
        'date': 'std::string',  # 可以使用 std::chrono 但这里简化
        'datetime': 'std::string',
        'timestamp': 'std::string',
    }
    
    # 处理可空类型
    cpp_type = type_mapping.get(sqlite_type.split('(')[0], 'std::string')
    
    if is_nullable and cpp_type not in ['std::string', 'std::vector<uint8_t>']:
        return f'std::optional<{cpp_type}>'
    
    return cpp_type

def to_camel_case(name: str, first_upper: bool = True) -> str:
    """将 snake_case 或 kebab-case 转换为 CamelCase"""
    # 替换下划线和横线为空格
    parts = re.split(r'[_-]', name)
    # 每个部分首字母大写
    camel = ''.join(part.capitalize() for part in parts)
    
    if not first_upper and camel:
        camel = camel[0].lower() + camel[1:]
    
    return camel

def generate_table_class(db_path: str, table_name: str) -> str:
    """为指定表生成 C++ 类代码"""
    
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    
    # 获取表结构
    cursor.execute(f"PRAGMA table_info({table_name})")
    columns = cursor.fetchall()
    
    if not columns:
        return f"// 表 {table_name} 不存在或没有列"
    
    # 获取主键
    cursor.execute(f"PRAGMA table_info({table_name})")
    pk_columns = [col[1] for col in cursor.fetchall() if col[5] == 1]
    
    # 类名
    class_name = to_camel_case(table_name, True)
    
    # 生成代码
    code = []
    
    # 1. 头文件保护
    header_guard = f"DB_{table_name.upper()}_H"
    code.append(f'#ifndef {header_guard}')
    code.append(f'#define {header_guard}')
    code.append('')
    
    # 2. 包含的头文件
    code.append('#include <string>')
    code.append('#include <vector>')
    code.append('#include <optional>')
    code.append('#include <memory>')
    code.append('#include <sqlite3.h>')
    code.append('#include <iostream>')
    code.append('')
    
    # 3. 命名空间
    code.append('namespace db {')
    code.append('namespace models {')
    code.append('')
    
    # 4. 结构体定义
    code.append(f'struct {class_name} {{')
    
    # 成员变量
    for col in columns:
        col_name = col[1]
        col_type = col[2]
        not_null = col[3] == 1
        default_val = col[4]
        is_pk = col[5] == 1
        
        cpp_type = sqlite_type_to_cpp_type(col_type, not not_null)
        var_name = to_camel_case(col_name, False)
        
        # 添加注释
        comment = f'  // {col_name}'
        if is_pk:
            comment += ' [PRIMARY KEY]'
        if default_val:
            comment += f' DEFAULT: {default_val}'
        
        code.append(comment)
        code.append(f'  {cpp_type} {var_name};')
        code.append('')
    
    # 构造函数
    code.append(f'  {class_name}() = default;')
    code.append('')
    
    # 从 sqlite3_stmt 构造
    code.append(f'  static {class_name} fromStmt(sqlite3_stmt* stmt) {{')
    code.append(f'    {class_name} obj;')
    code.append('    int index = 0;')
    
    for i, col in enumerate(columns):
        col_name = col[1]
        col_type = col[2]
        not_null = col[3] == 1
        cpp_type = sqlite_type_to_cpp_type(col_type, not not_null)
        var_name = to_camel_case(col_name, False)
        
        code.append(f'    // {col_name}')
        if 'std::string' in cpp_type:
            code.append(f'    if (sqlite3_column_type(stmt, {i}) != SQLITE_NULL) {{')
            code.append(f'      const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, {i}));')
            code.append(f'      obj.{var_name} = text ? std::string(text) : "";')
            code.append('    }')
        elif 'int' in cpp_type or 'int8_t' in cpp_type or 'int16_t' in cpp_type or 'int32_t' in cpp_type:
            code.append(f'    obj.{var_name} = sqlite3_column_int(stmt, {i});')
        elif 'int64_t' in cpp_type:
            code.append(f'    obj.{var_name} = sqlite3_column_int64(stmt, {i});')
        elif 'double' in cpp_type or 'float' in cpp_type:
            code.append(f'    obj.{var_name} = sqlite3_column_double(stmt, {i});')
        elif 'bool' in cpp_type:
            code.append(f'    obj.{var_name} = sqlite3_column_int(stmt, {i}) != 0;')
        elif 'std::vector<uint8_t>' in cpp_type:
            code.append(f'    if (sqlite3_column_type(stmt, {i}) != SQLITE_NULL) {{')
            code.append(f'      const void* blob = sqlite3_column_blob(stmt, {i});')
            code.append(f'      int size = sqlite3_column_bytes(stmt, {i});')
            code.append(f'      obj.{var_name}.resize(size);')
            code.append(f'      memcpy(obj.{var_name}.data(), blob, size);')
            code.append('    }')
        elif 'std::optional<' in cpp_type:
            inner_type = cpp_type.replace('std::optional<', '').replace('>', '')
            code.append(f'    if (sqlite3_column_type(stmt, {i}) != SQLITE_NULL) {{')
            if 'int' in inner_type:
                code.append(f'      obj.{var_name} = sqlite3_column_int(stmt, {i});')
            elif 'int64_t' in inner_type:
                code.append(f'      obj.{var_name} = sqlite3_column_int64(stmt, {i});')
            elif 'double' in inner_type:
                code.append(f'      obj.{var_name} = sqlite3_column_double(stmt, {i});')
            elif 'float' in inner_type:
                code.append(f'      obj.{var_name} = static_cast<float>(sqlite3_column_double(stmt, {i}));')
            elif 'bool' in inner_type:
                code.append(f'      obj.{var_name} = sqlite3_column_int(stmt, {i}) != 0;')
            code.append('    }')
        code.append('')
    
    code.append('    return obj;')
    code.append('  }')
    code.append('')
    
    # 转换为 SQL 值的函数
    code.append('  std::string toInsertValues() const {')
    code.append('    std::string result = "("')
    for i, col in enumerate(columns):
        col_name = col[1]
        if i > 0:
            code.append('    result += ", "')
        
        var_name = to_camel_case(col_name, False)
        cpp_type = sqlite_type_to_cpp_type(col[2], col[3] == 0)
        
        if 'std::string' in cpp_type:
            code.append(f'    result += "\'" + {var_name} + "\'"')
        elif 'std::optional<' in cpp_type:
            code.append(f'    if ({var_name}.has_value()) {{')
            inner_type = cpp_type.replace('std::optional<', '').replace('>', '')
            if 'std::string' in inner_type:
                code.append(f'      result += "\'" + {var_name}.value() + "\'"')
            else:
                code.append(f'      result += std::to_string({var_name}.value())')
            code.append('    } else {')
            code.append('      result += "NULL"')
            code.append('    }')
        elif 'int' in cpp_type or 'int8_t' in cpp_type or 'int16_t' in cpp_type or 'int32_t' in cpp_type or 'int64_t' in cpp_type:
            code.append(f'    result += std::to_string({var_name})')
        elif 'double' in cpp_type or 'float' in cpp_type:
            code.append(f'    result += std::to_string({var_name})')
        elif 'bool' in cpp_type:
            code.append(f'    result += {var_name} ? "1" : "0"')
        elif 'std::vector<uint8_t>' in cpp_type:
            code.append(f'    // BLOB 类型需要在预处理语句中处理')
            code.append(f'    result += "?"')
        else:
            code.append(f'    result += "\'" + {var_name} + "\'"')
    
    code.append('    result += ")"')
    code.append('    return result;')
    code.append('  }')
    code.append('};')
    code.append('')
    
    # 5. DAO 类
    dao_class_name = f'{class_name}DAO'
    code.append(f'class {dao_class_name} {{')
    code.append('private:')
    code.append('  sqlite3* db_;')
    code.append('')
    code.append('public:')
    code.append(f'  explicit {dao_class_name}(sqlite3* db) : db_(db) {{}}')
    code.append('')
    
    # 插入方法
    code.append(f'  bool insert(const {class_name}& obj) {{')
    code.append('    std::string sql = "INSERT INTO ' + table_name + ' ('')
    
    col_names = []
    placeholders = []
    for i, col in enumerate(columns):
        col_names.append(col[1])
        placeholders.append('?')
    
    code.append(f'    sql += "{", ".join(col_names)}"')
    code.append(f'    sql += ") VALUES ("')
    code.append(f'    sql += "{", ".join(placeholders)}"')
    code.append('    sql += ");"')
    code.append('')
    code.append('    sqlite3_stmt* stmt;')
    code.append('    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {')
    code.append('      std::cerr << "准备插入语句失败: " << sqlite3_errmsg(db_) << std::endl;')
    code.append('      return false;')
    code.append('    }')
    code.append('')
    
    # 绑定参数
    for i, col in enumerate(columns):
        col_name = col[1]
        col_type = col[2]
        var_name = to_camel_case(col_name, False)
        cpp_type = sqlite_type_to_cpp_type(col_type, col[3] == 0)
        
        code.append(f'    // 绑定 {col_name}')
        if 'std::string' in cpp_type:
            code.append(f'    sqlite3_bind_text(stmt, {i+1}, obj.{var_name}.c_str(), -1, SQLITE_TRANSIENT);')
        elif 'std::optional<' in cpp_type:
            inner_type = cpp_type.replace('std::optional<', '').replace('>', '')
            code.append(f'    if (obj.{var_name}.has_value()) {{')
            if 'std::string' in inner_type:
                code.append(f'      sqlite3_bind_text(stmt, {i+1}, obj.{var_name}.value().c_str(), -1, SQLITE_TRANSIENT);')
            elif 'int' in inner_type or 'int8_t' in inner_type or 'int16_t' in inner_type or 'int32_t' in inner_type:
                code.append(f'      sqlite3_bind_int(stmt, {i+1}, obj.{var_name}.value());')
            elif 'int64_t' in inner_type:
                code.append(f'      sqlite3_bind_int64(stmt, {i+1}, obj.{var_name}.value());')
            elif 'double' in inner_type:
                code.append(f'      sqlite3_bind_double(stmt, {i+1}, obj.{var_name}.value());')
            elif 'float' in inner_type:
                code.append(f'      sqlite3_bind_double(stmt, {i+1}, static_cast<double>(obj.{var_name}.value()));')
            elif 'bool' in inner_type:
                code.append(f'      sqlite3_bind_int(stmt, {i+1}, obj.{var_name}.value() ? 1 : 0);')
            code.append('    } else {')
            code.append(f'      sqlite3_bind_null(stmt, {i+1});')
            code.append('    }')
        elif 'int' in cpp_type or 'int8_t' in cpp_type or 'int16_t' in cpp_type or 'int32_t' in cpp_type:
            code.append(f'    sqlite3_bind_int(stmt, {i+1}, obj.{var_name});')
        elif 'int64_t' in cpp_type:
            code.append(f'    sqlite3_bind_int64(stmt, {i+1}, obj.{var_name});')
        elif 'double' in cpp_type:
            code.append(f'    sqlite3_bind_double(stmt, {i+1}, obj.{var_name});')
        elif 'float' in cpp_type:
            code.append(f'    sqlite3_bind_double(stmt, {i+1}, static_cast<double>(obj.{var_name}));')
        elif 'bool' in cpp_type:
            code.append(f'    sqlite3_bind_int(stmt, {i+1}, obj.{var_name} ? 1 : 0);')
        elif 'std::vector<uint8_t>' in cpp_type:
            code.append(f'    sqlite3_bind_blob(stmt, {i+1}, obj.{var_name}.data(), obj.{var_name}.size(), SQLITE_TRANSIENT);')
        code.append('')
    
    code.append('    bool result = sqlite3_step(stmt) == SQLITE_DONE;')
    code.append('    sqlite3_finalize(stmt);')
    code.append('    return result;')
    code.append('  }')
    code.append('')
    
    # 查询所有
    code.append(f'  std::vector<{class_name}> getAll() {{')
    code.append(f'    std::vector<{class_name}> result;')
    code.append(f'    std::string sql = "SELECT * FROM {table_name};";')
    code.append('')
    code.append('    sqlite3_stmt* stmt;')
    code.append('    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {')
    code.append('      std::cerr << "准备查询语句失败: " << sqlite3_errmsg(db_) << std::endl;')
    code.append('      return result;')
    code.append('    }')
    code.append('')
    code.append('    while (sqlite3_step(stmt) == SQLITE_ROW) {')
    code.append(f'      result.push_back({class_name}::fromStmt(stmt));')
    code.append('    }')
    code.append('')
    code.append('    sqlite3_finalize(stmt);')
    code.append('    return result;')
    code.append('  }')
    code.append('')
    
    # 按主键查询
    if pk_columns:
        pk_where = ' AND '.join([f'{col} = ?' for col in pk_columns])
        code.append(f'  std::optional<{class_name}> getByPrimaryKey(')
        pk_params = []
        for pk_col in pk_columns:
            col_info = next(col for col in columns if col[1] == pk_col)
            cpp_type = sqlite_type_to_cpp_type(col_info[2], col_info[3] == 0)
            param_name = to_camel_case(pk_col, False)
            pk_params.append(f'{cpp_type} {param_name}')
        
        code.append(f'      {", ".join(pk_params)}) {{')
        code.append(f'    std::string sql = "SELECT * FROM {table_name} WHERE {pk_where};";')
        code.append('')
        code.append('    sqlite3_stmt* stmt;')
        code.append('    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {')
        code.append('      std::cerr << "准备查询语句失败: " << sqlite3_errmsg(db_) << std::endl;')
        code.append('      return std::nullopt;')
        code.append('    }')
        code.append('')
        
        for i, pk_col in enumerate(pk_columns):
            col_info = next(col for col in columns if col[1] == pk_col)
            cpp_type = sqlite_type_to_cpp_type(col_info[2], col_info[3] == 0)
            param_name = to_camel_case(pk_col, False)
            
            if 'std::string' in cpp_type:
                code.append(f'    sqlite3_bind_text(stmt, {i+1}, {param_name}.c_str(), -1, SQLITE_TRANSIENT);')
            elif 'int' in cpp_type or 'int8_t' in cpp_type or 'int16_t' in cpp_type or 'int32_t' in cpp_type:
                code.append(f'    sqlite3_bind_int(stmt, {i+1}, {param_name});')
            elif 'int64_t' in cpp_type:
                code.append(f'    sqlite3_bind_int64(stmt, {i+1}, {param_name});')
            elif 'double' in cpp_type or 'float' in cpp_type:
                code.append(f'    sqlite3_bind_double(stmt, {i+1}, {param_name});')
            elif 'bool' in cpp_type:
                code.append(f'    sqlite3_bind_int(stmt, {i+1}, {param_name} ? 1 : 0);')
        
        code.append('')
        code.append('    if (sqlite3_step(stmt) == SQLITE_ROW) {')
        code.append(f'      {class_name} result = {class_name}::fromStmt(stmt);')
        code.append('      sqlite3_finalize(stmt);')
        code.append('      return result;')
        code.append('    }')
        code.append('')
        code.append('    sqlite3_finalize(stmt);')
        code.append('    return std::nullopt;')
        code.append('  }')
        code.append('')
    
    # 更新方法
    code.append(f'  bool update(const {class_name}& obj) {{')
    if pk_columns:
        set_clause = ', '.join([f'{col} = ?' for col in col_names if col not in pk_columns])
        where_clause = ' AND '.join([f'{col} = ?' for col in pk_columns])
        code.append(f'    std::string sql = "UPDATE {table_name} SET {set_clause} WHERE {where_clause};";')
        code.append('')
        code.append('    sqlite3_stmt* stmt;')
        code.append('    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {')
        code.append('      std::cerr << "准备更新语句失败: " << sqlite3_errmsg(db_) << std::endl;')
        code.append('      return false;')
        code.append('    }')
        code.append('')
        
        # 绑定 SET 参数
        param_index = 1
        for i, col in enumerate(columns):
            if col[1] not in pk_columns:
                var_name = to_camel_case(col[1], False)
                cpp_type = sqlite_type_to_cpp_type(col[2], col[3] == 0)
                
                code.append(f'    // 绑定 {col[1]}')
                if 'std::string' in cpp_type:
                    code.append(f'    sqlite3_bind_text(stmt, {param_index}, obj.{var_name}.c_str(), -1, SQLITE_TRANSIENT);')
                elif 'std::optional<' in cpp_type:
                    inner_type = cpp_type.replace('std::optional<', '').replace('>', '')
                    code.append(f'    if (obj.{var_name}.has_value()) {{')
                    if 'std::string' in inner_type:
                        code.append(f'      sqlite3_bind_text(stmt, {param_index}, obj.{var_name}.value().c_str(), -1, SQLITE_TRANSIENT);')
                    elif 'int' in inner_type or 'int8_t' in inner_type or 'int16_t' in inner_type or 'int32_t' in inner_type:
                        code.append(f'      sqlite3_bind_int(stmt, {param_index}, obj.{var_name}.value());')
                    elif 'int64_t' in inner_type:
                        code.append(f'      sqlite3_bind_int64(stmt, {param_index}, obj.{var_name}.value());')
                    elif 'double' in inner_type or 'float' in inner_type:
                        code.append(f'      sqlite3_bind_double(stmt, {param_index}, obj.{var_name}.value());')
                    elif 'bool' in inner_type:
                        code.append(f'      sqlite3_bind_int(stmt, {param_index}, obj.{var_name}.value() ? 1 : 0);')
                    code.append('    } else {')
                    code.append(f'      sqlite3_bind_null(stmt, {param_index});')
                    code.append('    }')
                elif 'int' in cpp_type or 'int8_t' in cpp_type or 'int16_t' in cpp_type or 'int32_t' in cpp_type:
                    code.append(f'    sqlite3_bind_int(stmt, {param_index}, obj.{var_name});')
                elif 'int64_t' in cpp_type:
                    code.append(f'    sqlite3_bind_int64(stmt, {param_index}, obj.{var_name});')
                elif 'double' in cpp_type or 'float' in cpp_type:
                    code.append(f'    sqlite3_bind_double(stmt, {param_index}, obj.{var_name});')
                elif 'bool' in cpp_type:
                    code.append(f'    sqlite3_bind_int(stmt, {param_index}, obj.{var_name} ? 1 : 0);')
                elif 'std::vector<uint8_t>' in cpp_type:
                    code.append(f'    sqlite3_bind_blob(stmt, {param_index}, obj.{var_name}.data(), obj.{var_name}.size(), SQLITE_TRANSIENT);')
                param_index += 1
                code.append('')
        
        # 绑定 WHERE 参数
        for pk_col in pk_columns:
            var_name = to_camel_case(pk_col, False)
            col_info = next(col for col in columns if col[1] == pk_col)
            cpp_type = sqlite_type_to_cpp_type(col_info[2], col_info[3] == 0)
            
            code.append(f'    // 绑定主键 {pk_col}')
            if 'std::string' in cpp_type:
                code.append(f'    sqlite3_bind_text(stmt, {param_index}, obj.{var_name}.c_str(), -1, SQLITE_TRANSIENT);')
            elif 'int' in cpp_type or 'int8_t' in cpp_type or 'int16_t' in cpp_type or 'int32_t' in cpp_type:
                code.append(f'    sqlite3_bind_int(stmt, {param_index}, obj.{var_name});')
            elif 'int64_t' in cpp_type:
                code.append(f'    sqlite3_bind_int64(stmt, {param_index}, obj.{var_name});')
            elif 'double' in cpp_type or 'float' in cpp_type:
                code.append(f'    sqlite3_bind_double(stmt, {param_index}, obj.{var_name});')
            elif 'bool' in cpp_type:
                code.append(f'    sqlite3_bind_int(stmt, {param_index}, obj.{var_name} ? 1 : 0);')
            param_index += 1
            code.append('')
    else:
        code.append('    // 没有主键，无法更新')
        code.append('    return false;')
    
    code.append('')
    code.append('    bool result = sqlite3_step(stmt) == SQLITE_DONE;')
    code.append('    sqlite3_finalize(stmt);')
    code.append('    return result;')
    code.append('  }')
    code.append('')
    
    # 删除方法
    if pk_columns:
        code.append(f'  bool deleteByPrimaryKey(')
        pk_params = []
        for pk_col in pk_columns:
            col_info = next(col for col in columns if col[1] == pk_col)
            cpp_type = sqlite_type_to_cpp_type(col_info[2], col_info[3] == 0)
            param_name = to_camel_case(pk_col, False)
            pk_params.append(f'{cpp_type} {param_name}')
        
        code.append(f'      {", ".join(pk_params)}) {{')
        code.append(f'    std::string sql = "DELETE FROM {table_name} WHERE {pk_where};";')
        code.append('')
        code.append('    sqlite3_stmt* stmt;')
        code.append('    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {')
        code.append('      std::cerr << "准备删除语句失败: " << sqlite3_errmsg(db_) << std::endl;')
        code.append('      return false;')
        code.append('    }')
        code.append('')
        
        for i, pk_col in enumerate(pk_columns):
            col_info = next(col for col in columns if col[1] == pk_col)
            cpp_type = sqlite_type_to_cpp_type(col_info[2], col_info[3] == 0)
            param_name = to_camel_case(pk_col, False)
            
            if 'std::string' in cpp_type:
                code.append(f'    sqlite3_bind_text(stmt, {i+1}, {param_name}.c_str(), -1, SQLITE_TRANSIENT);')
            elif 'int' in cpp_type or 'int8_t' in cpp_type or 'int16_t' in cpp_type or 'int32_t' in cpp_type:
                code.append(f'    sqlite3_bind_int(stmt, {i+1}, {param_name});')
            elif 'int64_t' in cpp_type:
                code.append(f'    sqlite3_bind_int64(stmt, {i+1}, {param_name});')
            elif 'double' in cpp_type or 'float' in cpp_type:
                code.append(f'    sqlite3_bind_double(stmt, {i+1}, {param_name});')
            elif 'bool' in cpp_type:
                code.append(f'    sqlite3_bind_int(stmt, {i+1}, {param_name} ? 1 : 0);')
        
        code.append('')
        code.append('    bool result = sqlite3_step(stmt) == SQLITE_DONE;')
        code.append('    sqlite3_finalize(stmt);')
        code.append('    return result;')
        code.append('  }')
    
    code.append('};')
    code.append('')
    code.append('} // namespace models')
    code.append('} // namespace db')
    code.append('')
    code.append(f'#endif // {header_guard}')
    
    conn.close()
    return '\n'.join(code)

def generate_database_helper(db_path: str) -> str:
    """生成数据库辅助类"""
    
    code = []
    code.append('#ifndef DB_HELPER_H')
    code.append('#define DB_HELPER_H')
    code.append('')
    code.append('#include <string>')
    code.append('#include <memory>')
    code.append('#include <sqlite3.h>')
    code.append('')
    code.append('namespace db {')
    code.append('')
    code.append('class Database {')
    code.append('private:')
    code.append('  sqlite3* db_;')
    code.append('  std::string db_path_;')
    code.append('')
    code.append('  // 禁止拷贝')
    code.append('  Database(const Database&) = delete;')
    code.append('  Database& operator=(const Database&) = delete;')
    code.append('')
    code.append('public:')
    code.append('  explicit Database(const std::string& db_path) : db_path_(db_path), db_(nullptr) {}')
    code.append('')
    code.append('  ~Database() {')
    code.append('    if (db_) {')
    code.append('      sqlite3_close(db_);')
    code.append('    }')
    code.append('  }')
    code.append('')
    code.append('  bool open() {')
    code.append('    if (sqlite3_open(db_path_.c_str(), &db_) != SQLITE_OK) {')
    code.append('      return false;')
    code.append('    }')
    code.append('    return true;')
    code.append('  }')
    code.append('')
    code.append('  bool execute(const std::string& sql) {')
    code.append('    char* err_msg = nullptr;')
    code.append('    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK) {')
    code.append('      if (err_msg) {')
    code.append('        sqlite3_free(err_msg);')
    code.append('      }')
    code.append('      return false;')
    code.append('    }')
    code.append('    return true;')
    code.append('  }')
    code.append('')
    code.append('  sqlite3* get() const { return db_; }')
    code.append('')
    code.append('  void close() {')
    code.append('    if (db_) {')
    code.append('      sqlite3_close(db_);')
    code.append('      db_ = nullptr;')
    code.append('    }')
    code.append('  }')
    code.append('};')
    code.append('')
    code.append('} // namespace db')
    code.append('')
    code.append('#endif // DB_HELPER_H')
    
    return '\n'.join(code)

def main():
    if len(sys.argv) < 2:
        print("用法: python sqlite_to_cpp.py <database.db> [table_name]")
        print("如果未指定表名，则为所有表生成代码")
        return
    
    db_path = sys.argv[1]
    
    if not os.path.exists(db_path):
        print(f"错误: 数据库文件 {db_path} 不存在")
        return
    
    # 连接到数据库
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    
    # 获取所有表名
    cursor.execute("SELECT name FROM sqlite_master WHERE type='table';")
    tables = [row[0] for row in cursor.fetchall()]
    
    conn.close()
    
    if len(sys.argv) >= 3:
        table_name = sys.argv[2]
        if table_name in tables:
            tables = [table_name]
        else:
            print(f"错误: 表 {table_name} 不存在")
            return
    
    # 生成数据库辅助类
    helper_code = generate_database_helper(db_path)
    with open('DatabaseHelper.h', 'w', encoding='utf-8') as f:
        f.write(helper_code)
    print(f"已生成: DatabaseHelper.h")
    
    # 为每个表生成代码
    for table in tables:
        print(f"为表 {table} 生成代码...")
        code = generate_table_class(db_path, table)
        
        # 保存到文件
        filename = f"{to_camel_case(table, True)}.h"
        with open(filename, 'w', encoding='utf-8') as f:
            f.write(code)
        
        print(f"已生成: {filename}")
    
    # 生成主头文件
    generate_main_header(tables)
    
    print(f"\n完成! 为 {len(tables)} 个表生成了C++代码。")

def generate_main_header(tables):
    """生成主头文件，包含所有生成的头文件"""
    
    code = []
    code.append('#ifndef DB_ALL_MODELS_H')
    code.append('#define DB_ALL_MODELS_H')
    code.append('')
    code.append('#include "DatabaseHelper.h"')
    code.append('')
    
    for table in tables:
        class_name = to_camel_case(table, True)
        code.append(f'#include "{class_name}.h"')
    
    code.append('')
    code.append('#endif // DB_ALL_MODELS_H')
    
    with open('AllModels.h', 'w', encoding='utf-8') as f:
        f.write('\n'.join(code))
    
    print(f"已生成: AllModels.h")

if __name__ == '__main__':
    main()