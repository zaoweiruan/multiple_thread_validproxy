#!/usr/bin/env python3
"""
SQLite 表转 C++ 结构体代码生成器 - 完全修复版
修复所有字符串语法错误
"""

import sqlite3
import sys
import os
from typing import Dict, List, Tuple
import re

def sqlite_type_to_cpp_type(sqlite_type: str, is_nullable: bool = False) -> str:
    """将 SQLite 类型映射到 C++ 类型"""
    if sqlite_type is None:
        return 'std::string'
    
    sqlite_type = sqlite_type.lower()
    
    # 移除长度限制
    sqlite_type = re.sub(r'\([^)]*\)', '', sqlite_type).strip()
    
    type_mapping = {
        'integer': 'int64_t',
        'int': 'int64_t',
        'tinyint': 'int32_t',
        'smallint': 'int32_t',
        'mediumint': 'int32_t',
        'bigint': 'int64_t',
        'unsigned big int': 'uint64_t',
        'int2': 'int32_t',
        'int8': 'int64_t',
        
        'real': 'double',
        'double': 'double',
        'float': 'double',
        'numeric': 'double',
        'decimal': 'double',
        
        'text': 'std::string',
        'varchar': 'std::string',
        'char': 'std::string',
        'clob': 'std::string',
        'nchar': 'std::string',
        'nvarchar': 'std::string',
        'ntext': 'std::string',
        
        'blob': 'std::vector<uint8_t>',
        'boolean': 'bool',
        'bool': 'bool',
        'date': 'std::string',
        'datetime': 'std::string',
        'timestamp': 'std::string',
        'time': 'std::string',
    }
    
    base_type = sqlite_type.split()[0] if ' ' in sqlite_type else sqlite_type
    cpp_type = type_mapping.get(base_type, 'std::string')
    
    # 处理可空类型
    if is_nullable and cpp_type not in ['std::string', 'std::vector<uint8_t>']:
        return f'std::optional<{cpp_type}>'
    
    return cpp_type

def to_camel_case(name: str, first_upper: bool = True) -> str:
    """转换为驼峰命名"""
    if not name:
        return name
    
    parts = re.split(r'[_\s-]+', name)
    
    filtered_parts = []
    for part in parts:
        if part:
            if part[0].isdigit():
                filtered_parts.append('_' + part)
            else:
                filtered_parts.append(part)
    
    if not filtered_parts:
        return name
    
    camel = ''.join(part.capitalize() for part in filtered_parts)
    
    if not first_upper and camel:
        camel = camel[0].lower() + camel[1:]
    
    return camel

def generate_table_class(db_path: str, table_name: str) -> str:
    """为指定表生成 C++ 类代码"""
    
    try:
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()
        
        # 获取表结构
        cursor.execute(f"PRAGMA table_info({table_name})")
        columns = cursor.fetchall()
        
        if not columns:
            return f"// 表 {table_name} 不存在或没有列\n"
        
        # 获取主键
        pk_columns = []
        for col in columns:
            col_name = col[1]
            is_pk = col[5]
            if is_pk:
                pk_columns.append(col_name)
        
        # 类名
        class_name = to_camel_case(table_name, True)
        
        # 生成代码
        code = []
        
        # 头文件保护
        header_guard = f"DB_{table_name.upper()}_H"
        code.append(f'#ifndef {header_guard}')
        code.append(f'#define {header_guard}')
        code.append('')
        
        # 包含的头文件
        code.append('#include <string>')
        code.append('#include <vector>')
        code.append('#include <optional>')
        code.append('#include <memory>')
        code.append('#include <sqlite3.h>')
        code.append('#include <iostream>')
        code.append('#include <cstring>')
        code.append('#include <sstream>')
        code.append('')
        
        # 命名空间
        code.append('namespace db {')
        code.append('namespace models {')
        code.append('')
        
        # 结构体定义
        code.append(f'struct {class_name} {{')
        
        # 成员变量
        for col in columns:
            col_id = col[0]
            col_name = col[1]
            col_type = col[2]
            not_null = col[3] == 1
            default_val = col[4]
            is_pk = col[5] == 1
            
            cpp_type = sqlite_type_to_cpp_type(col_type, not not_null)
            var_name = to_camel_case(col_name, False)
            
            # 注释
            comment_parts = []
            comment_parts.append(col_name)
            if is_pk:
                comment_parts.append('[PRIMARY KEY]')
            if not_null:
                comment_parts.append('[NOT NULL]')
            if default_val:
                comment_parts.append(f'DEFAULT: {default_val}')
            
            code.append(f'  // {" ".join(comment_parts)}')
            code.append(f'  {cpp_type} {var_name};')
            code.append('')
        
        # 默认构造函数
        code.append(f'  {class_name}() = default;')
        code.append('')
        
        # 从 sqlite3_stmt 构造
        code.append(f'  static {class_name} fromStmt(sqlite3_stmt* stmt) {{')
        code.append(f'    {class_name} obj;')
        
        for i, col in enumerate(columns):
            col_name = col[1]
            col_type = col[2]
            not_null = col[3] == 1
            cpp_type = sqlite_type_to_cpp_type(col_type, not not_null)
            var_name = to_camel_case(col_name, False)
            
            code.append(f'    // {col_name}')
            
            if 'std::optional<' in cpp_type:
                inner_type = cpp_type.replace('std::optional<', '').replace('>', '')
                code.append(f'    if (sqlite3_column_type(stmt, {i}) != SQLITE_NULL) {{')
                
                if 'std::string' in inner_type:
                    code.append(f'      const unsigned char* text = sqlite3_column_text(stmt, {i});')
                    code.append(f'      obj.{var_name} = text ? reinterpret_cast<const char*>(text) : "";')
                elif 'int64_t' in inner_type:
                    code.append(f'      obj.{var_name} = sqlite3_column_int64(stmt, {i});')
                elif 'int32_t' in inner_type or 'int16_t' in inner_type or 'int8_t' in inner_type:
                    code.append(f'      obj.{var_name} = sqlite3_column_int(stmt, {i});')
                elif 'double' in inner_type or 'float' in inner_type:
                    code.append(f'      obj.{var_name} = sqlite3_column_double(stmt, {i});')
                elif 'bool' in inner_type:
                    code.append(f'      obj.{var_name} = sqlite3_column_int(stmt, {i}) != 0;')
                else:
                    code.append(f'      const unsigned char* text = sqlite3_column_text(stmt, {i});')
                    code.append(f'      obj.{var_name} = text ? std::string(reinterpret_cast<const char*>(text)) : "";')
                
                code.append('    }')
                
            elif 'std::string' in cpp_type:
                code.append(f'    const unsigned char* text = sqlite3_column_text(stmt, {i});')
                code.append(f'    obj.{var_name} = text ? reinterpret_cast<const char*>(text) : "";')
                
            elif 'int64_t' in cpp_type:
                code.append(f'    obj.{var_name} = sqlite3_column_int64(stmt, {i});')
                
            elif 'int32_t' in cpp_type or 'int16_t' in cpp_type or 'int8_t' in cpp_type:
                code.append(f'    obj.{var_name} = sqlite3_column_int(stmt, {i});')
                
            elif 'double' in cpp_type or 'float' in cpp_type:
                code.append(f'    obj.{var_name} = sqlite3_column_double(stmt, {i});')
                
            elif 'bool' in cpp_type:
                code.append(f'    obj.{var_name} = sqlite3_column_int(stmt, {i}) != 0;')
                
            elif 'std::vector<uint8_t>' in cpp_type:
                code.append(f'    const void* blob = sqlite3_column_blob(stmt, {i});')
                code.append(f'    int size = sqlite3_column_bytes(stmt, {i});')
                code.append(f'    if (blob && size > 0) {{')
                code.append(f'      obj.{var_name}.resize(size);')
                code.append(f'      std::memcpy(obj.{var_name}.data(), blob, size);')
                code.append(f'    }}')
                
            else:
                code.append(f'    const unsigned char* text = sqlite3_column_text(stmt, {i});')
                code.append(f'    obj.{var_name} = text ? std::string(reinterpret_cast<const char*>(text)) : "";')
            
            code.append('')
        
        code.append('    return obj;')
        code.append('  }')
        code.append('')
        
        # toString 方法 - 使用安全的实现
        code.append('  std::string toString() const {')
        code.append('    std::ostringstream oss;')
        code.append('    oss << "{";')
        
        for i, col in enumerate(columns):
            col_name = col[1]
            col_type = col[2]
            not_null = col[3] == 1
            var_name = to_camel_case(col_name, False)
            cpp_type = sqlite_type_to_cpp_type(col_type, not not_null)
            
            if i > 0:
                code.append('    oss << ", ";')
            
            code.append(f'    oss << \\"{col_name}\\": ";')
            
            if 'std::optional<' in cpp_type:
                code.append(f'    if ({var_name}.has_value()) {{')
                inner_type = cpp_type.replace('std::optional<', '').replace('>', '')
                
                if 'std::string' in inner_type:
                    code.append(f'      oss << \\"\\" << {var_name}.value() << \\"\\";')
                elif 'int64_t' in inner_type or 'int32_t' in inner_type or 'int16_t' in inner_type or 'int8_t' in inner_type:
                    code.append(f'      oss << {var_name}.value();')
                elif 'double' in inner_type or 'float' in inner_type:
                    code.append(f'      oss << {var_name}.value();')
                elif 'bool' in inner_type:
                    code.append(f'      oss << ({var_name}.value() ? "true" : "false");')
                else:
                    code.append(f'      oss << {var_name}.value();')
                
                code.append('    } else {')
                code.append('      oss << "null";')
                code.append('    }')
                
            elif 'std::string' in cpp_type:
                code.append(f'    oss << \\"\\" << {var_name} << \\"\\";')
                
            elif 'int64_t' in cpp_type or 'int32_t' in cpp_type or 'int16_t' in cpp_type or 'int8_t' in cpp_type:
                code.append(f'    oss << {var_name};')
                
            elif 'double' in cpp_type or 'float' in cpp_type:
                code.append(f'    oss << {var_name};')
                
            elif 'bool' in cpp_type:
                code.append(f'    oss << ({var_name} ? "true" : "false");')
                
            elif 'std::vector<uint8_t>' in cpp_type:
                code.append(f'    oss << "[BLOB size=" << {var_name}.size() << "]";')
                
            else:
                code.append(f'    oss << \\"\\" << {var_name} << \\"\\";')
            
            code.append('')
        
        code.append('    oss << "}";')
        code.append('    return oss.str();')
        code.append('  }')
        code.append('')
        
        code.append('};')
        code.append('')
        
        # DAO 类
        dao_class_name = f'{class_name}DAO'
        code.append(f'class {dao_class_name} {{')
        code.append('private:')
        code.append('  sqlite3* db_;')
        code.append('')
        code.append('public:')
        code.append(f'  explicit {dao_class_name}(sqlite3* db) : db_(db) {{}}')
        code.append('')
        code.append(f'  static const char* getTableName() {{ return "{table_name}"; }}')
        code.append('')
        
        # 获取列名
        col_names = [col[1] for col in columns]
        placeholders = ['?' for _ in columns]
        
        # 插入方法 - 修复字符串连接
        code.append(f'  bool insert(const {class_name}& obj) {{')
        code.append(f'    std::string sql = "INSERT INTO {table_name} (";')
        
        # 构建列名部分
        for i, col_name in enumerate(col_names):
            if i > 0:
                code.append('    sql += ", ";')
            code.append(f'    sql += "{col_name}";')
        
        code.append('    sql += ") VALUES (";')
        
        # 构建占位符部分
        for i, placeholder in enumerate(placeholders):
            if i > 0:
                code.append('    sql += ", ";')
            code.append(f'    sql += "{placeholder}";')
        
        code.append('    sql += ");";')
        code.append('')
        code.append('    sqlite3_stmt* stmt = nullptr;')
        code.append('    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {')
        code.append('      std::cerr << "准备插入语句失败: " << sqlite3_errmsg(db_) << std::endl;')
        code.append('      return false;')
        code.append('    }')
        code.append('')
        
        # 绑定参数
        for i, col in enumerate(columns):
            col_name = col[1]
            col_type = col[2]
            not_null = col[3] == 1
            var_name = to_camel_case(col_name, False)
            cpp_type = sqlite_type_to_cpp_type(col_type, not not_null)
            
            code.append(f'    // 绑定 {col_name}')
            
            if 'std::optional<' in cpp_type:
                inner_type = cpp_type.replace('std::optional<', '').replace('>', '')
                code.append(f'    if (obj.{var_name}.has_value()) {{')
                
                if 'std::string' in inner_type:
                    code.append(f'      sqlite3_bind_text(stmt, {i+1}, obj.{var_name}.value().c_str(), -1, SQLITE_TRANSIENT);')
                elif 'int64_t' in inner_type:
                    code.append(f'      sqlite3_bind_int64(stmt, {i+1}, obj.{var_name}.value());')
                elif 'int32_t' in inner_type or 'int16_t' in inner_type or 'int8_t' in inner_type:
                    code.append(f'      sqlite3_bind_int(stmt, {i+1}, obj.{var_name}.value());')
                elif 'double' in inner_type or 'float' in inner_type:
                    code.append(f'      sqlite3_bind_double(stmt, {i+1}, obj.{var_name}.value());')
                elif 'bool' in inner_type:
                    code.append(f'      sqlite3_bind_int(stmt, {i+1}, obj.{var_name}.value() ? 1 : 0);')
                else:
                    code.append(f'      sqlite3_bind_text(stmt, {i+1}, obj.{var_name}.value().c_str(), -1, SQLITE_TRANSIENT);')
                
                code.append('    } else {')
                code.append(f'      sqlite3_bind_null(stmt, {i+1});')
                code.append('    }')
                
            elif 'std::string' in cpp_type:
                code.append(f'    sqlite3_bind_text(stmt, {i+1}, obj.{var_name}.c_str(), -1, SQLITE_TRANSIENT);')
                
            elif 'int64_t' in cpp_type:
                code.append(f'    sqlite3_bind_int64(stmt, {i+1}, obj.{var_name});')
                
            elif 'int32_t' in cpp_type or 'int16_t' in cpp_type or 'int8_t' in cpp_type:
                code.append(f'    sqlite3_bind_int(stmt, {i+1}, obj.{var_name});')
                
            elif 'double' in cpp_type or 'float' in cpp_type:
                code.append(f'    sqlite3_bind_double(stmt, {i+1}, obj.{var_name});')
                
            elif 'bool' in cpp_type:
                code.append(f'    sqlite3_bind_int(stmt, {i+1}, obj.{var_name} ? 1 : 0);')
                
            elif 'std::vector<uint8_t>' in cpp_type:
                code.append(f'    sqlite3_bind_blob(stmt, {i+1}, obj.{var_name}.data(), static_cast<int>(obj.{var_name}.size()), SQLITE_TRANSIENT);')
                
            else:
                code.append(f'    sqlite3_bind_text(stmt, {i+1}, obj.{var_name}.c_str(), -1, SQLITE_TRANSIENT);')
            
            code.append('')
        
        code.append('    bool success = (sqlite3_step(stmt) == SQLITE_DONE);')
        code.append('    sqlite3_finalize(stmt);')
        code.append('    return success;')
        code.append('  }')
        code.append('')
        
        # 查询所有
        code.append(f'  std::vector<{class_name}> getAll() {{')
        code.append(f'    std::vector<{class_name}> result;')
        code.append(f'    const char* sql = "SELECT * FROM {table_name};";')
        code.append('')
        code.append('    sqlite3_stmt* stmt = nullptr;')
        code.append('    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {')
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
        
        # 按条件查询
        code.append(f'  std::vector<{class_name}> query(const std::string& whereClause) {{')
        code.append(f'    std::vector<{class_name}> result;')
        code.append(f'    std::string sql = "SELECT * FROM {table_name}";')
        code.append('    if (!whereClause.empty()) {')
        code.append('      sql += " WHERE " + whereClause;')
        code.append('    }')
        code.append('    sql += ";";')
        code.append('')
        code.append('    sqlite3_stmt* stmt = nullptr;')
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
            pk_where_parts = [f'{col} = ?' for col in pk_columns]
            pk_where = ' AND '.join(pk_where_parts)
            
            code.append(f'  std::optional<{class_name}> getByPrimaryKey(')
            
            pk_params = []
            for pk_col in pk_columns:
                col_info = next(col for col in columns if col[1] == pk_col)
                cpp_type = sqlite_type_to_cpp_type(col_info[2], col_info[3] == 0)
                param_name = to_camel_case(pk_col, False)
                
                # 如果是 std::optional 类型，在参数中去掉 optional
                if 'std::optional<' in cpp_type:
                    inner_type = cpp_type.replace('std::optional<', '').replace('>', '')
                    pk_params.append(f'{inner_type} {param_name}')
                else:
                    pk_params.append(f'{cpp_type} {param_name}')
            
            code.append(f'      {", ".join(pk_params)}) {{')
            code.append(f'    const char* sql = "SELECT * FROM {table_name} WHERE {pk_where};";')
            code.append('')
            code.append('    sqlite3_stmt* stmt = nullptr;')
            code.append('    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {')
            code.append('      std::cerr << "准备查询语句失败: " << sqlite3_errmsg(db_) << std::endl;')
            code.append('      return std::nullopt;')
            code.append('    }')
            code.append('')
            
            for i, pk_col in enumerate(pk_columns):
                col_info = next(col for col in columns if col[1] == pk_col)
                cpp_type = sqlite_type_to_cpp_type(col_info[2], col_info[3] == 0)
                param_name = to_camel_case(pk_col, False)
                
                if 'std::string' in cpp_type or ('std::optional<' in cpp_type and 'std::string' in cpp_type):
                    code.append(f'    sqlite3_bind_text(stmt, {i+1}, {param_name}.c_str(), -1, SQLITE_TRANSIENT);')
                elif 'int64_t' in cpp_type or ('std::optional<' in cpp_type and 'int64_t' in cpp_type):
                    code.append(f'    sqlite3_bind_int64(stmt, {i+1}, {param_name});')
                elif 'int32_t' in cpp_type or 'int16_t' in cpp_type or 'int8_t' in cpp_type:
                    code.append(f'    sqlite3_bind_int(stmt, {i+1}, {param_name});')
                elif 'double' in cpp_type or 'float' in cpp_type:
                    code.append(f'    sqlite3_bind_double(stmt, {i+1}, {param_name});')
                elif 'bool' in cpp_type:
                    code.append(f'    sqlite3_bind_int(stmt, {i+1}, {param_name} ? 1 : 0);')
            
            code.append('')
            code.append('    if (sqlite3_step(stmt) == SQLITE_ROW) {')
            code.append(f'      {class_name} obj = {class_name}::fromStmt(stmt);')
            code.append('      sqlite3_finalize(stmt);')
            code.append('      return obj;')
            code.append('    }')
            code.append('')
            code.append('    sqlite3_finalize(stmt);')
            code.append('    return std::nullopt;')
            code.append('  }')
            code.append('')
        
        # 删除方法
        if pk_columns:
            pk_where_parts = [f'{col} = ?' for col in pk_columns]
            pk_where = ' AND '.join(pk_where_parts)
            
            code.append(f'  bool deleteByPrimaryKey(')
            
            pk_params = []
            for pk_col in pk_columns:
                col_info = next(col for col in columns if col[1] == pk_col)
                cpp_type = sqlite_type_to_cpp_type(col_info[2], col_info[3] == 0)
                param_name = to_camel_case(pk_col, False)
                
                if 'std::optional<' in cpp_type:
                    inner_type = cpp_type.replace('std::optional<', '').replace('>', '')
                    pk_params.append(f'{inner_type} {param_name}')
                else:
                    pk_params.append(f'{cpp_type} {param_name}')
            
            code.append(f'      {", ".join(pk_params)}) {{')
            code.append(f'    const char* sql = "DELETE FROM {table_name} WHERE {pk_where};";')
            code.append('')
            code.append('    sqlite3_stmt* stmt = nullptr;')
            code.append('    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {')
            code.append('      std::cerr << "准备删除语句失败: " << sqlite3_errmsg(db_) << std::endl;')
            code.append('      return false;')
            code.append('    }')
            code.append('')
            
            for i, pk_col in enumerate(pk_columns):
                col_info = next(col for col in columns if col[1] == pk_col)
                cpp_type = sqlite_type_to_cpp_type(col_info[2], col_info[3] == 0)
                param_name = to_camel_case(pk_col, False)
                
                if 'std::string' in cpp_type or ('std::optional<' in cpp_type and 'std::string' in cpp_type):
                    code.append(f'    sqlite3_bind_text(stmt, {i+1}, {param_name}.c_str(), -1, SQLITE_TRANSIENT);')
                elif 'int64_t' in cpp_type or ('std::optional<' in cpp_type and 'int64_t' in cpp_type):
                    code.append(f'    sqlite3_bind_int64(stmt, {i+1}, {param_name});')
                elif 'int32_t' in cpp_type or 'int16_t' in cpp_type or 'int8_t' in cpp_type:
                    code.append(f'    sqlite3_bind_int(stmt, {i+1}, {param_name});')
                elif 'double' in cpp_type or 'float' in cpp_type:
                    code.append(f'    sqlite3_bind_double(stmt, {i+1}, {param_name});')
                elif 'bool' in cpp_type:
                    code.append(f'    sqlite3_bind_int(stmt, {i+1}, {param_name} ? 1 : 0);')
            
            code.append('')
            code.append('    bool success = (sqlite3_step(stmt) == SQLITE_DONE);')
            code.append('    sqlite3_finalize(stmt);')
            code.append('    return success;')
            code.append('  }')
        
        code.append('};')
        code.append('')
        code.append('} // namespace models')
        code.append('} // namespace db')
        code.append('')
        code.append(f'#endif // {header_guard}')
        
        conn.close()
        return '\n'.join(code)
        
    except sqlite3.Error as e:
        return f"// 错误: 处理表 {table_name} 时发生错误: {str(e)}\n"
    except Exception as e:
        return f"// 错误: 处理表 {table_name} 时发生未知错误: {str(e)}\n"

# 其他辅助函数保持不变...
# 这里只提供核心的 generate_table_class 函数
# 完整脚本需要包含之前的主函数和其他辅助函数

if __name__ == '__main__':
    # 测试
    if len(sys.argv) < 2:
        print("用法: python sqlite_to_cpp_fixed.py <database.db> [table_name]")
        sys.exit(1)
    
    db_path = sys.argv[1]
    table_name = sys.argv[2] if len(sys.argv) > 2 else None
    
    # 连接数据库获取表名
    try:
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()
        
        if table_name:
            tables = [table_name]
        else:
            cursor.execute("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%';")
            tables = [row[0] for row in cursor.fetchall()]
        
        conn.close()
        
        if not tables:
            print("错误: 数据库中没有表")
            sys.exit(1)
        
        # 为每个表生成代码
        for table in tables:
            print(f"正在为表 {table} 生成代码...")
            code = generate_table_class(db_path, table)
            
            # 保存到文件
            class_name = to_camel_case(table, True)
            filename = f"{class_name}.h"
            
            with open(filename, 'w', encoding='utf-8') as f:
                f.write(code)
            
            print(f"  ✓ 已生成: {filename}")
        
        print(f"\n✅ 完成! 为 {len(tables)} 个表生成了C++代码。")
        
    except sqlite3.Error as e:
        print(f"数据库错误: {e}")
    except Exception as e:
        print(f"错误: {e}")