#!/usr/bin/env python3
"""
SQLite 表转 C++ 结构体代码生成器 - 简化修复版
完全修复字符串语法错误
"""

import sqlite3
import sys
import os
import re

def sqlite_type_to_cpp_type(sqlite_type, is_nullable=False):
    if not sqlite_type:
        return 'std::string'
    
    sqlite_type = sqlite_type.lower().split('(')[0]
    
    mapping = {
        'integer': 'std::string', 'int': 'std::string', 'bigint': 'std::string',
        'real': 'std::string', 'double': 'std::string', 'float': 'std::string',
        'text': 'std::string', 'varchar': 'std::string', 'char': 'std::string',
        'blob': 'std::vector<uint8_t>', 'boolean': 'bool',
    }
    
    cpp_type = mapping.get(sqlite_type, 'std::string')
    
    if is_nullable and cpp_type not in ['std::string','int','double','std::vector<uint8_t>']:
        return f'std::optional<{cpp_type}>'
    
    return cpp_type

def to_camel_case(name, first_upper=True):
    if not name:
        return name
    
    parts = re.split(r'[_\s-]+', name)
    filtered = [p for p in parts if p]
    
    if not filtered:
        return name
    
    camel = ''.join(p.capitalize() for p in filtered)
    
    if not first_upper and camel:
        camel = camel[0].lower() + camel[1:]
    
    return camel

def generate_table_class_simple(db_path, table_name):
    """简化的生成函数，避免复杂字符串处理"""
    try:
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()
        
        cursor.execute(f"PRAGMA table_info({table_name})")
        columns = cursor.fetchall()
        
        if not columns:
            return f"// 表 {table_name} 不存在\n"
        
        class_name = to_camel_case(table_name, True)
        code_lines = []
        
        # 头文件
        header_guard = f"DB_{table_name.upper()}_H"
        code_lines.append(f'#ifndef {header_guard}')
        code_lines.append(f'#define {header_guard}')
        code_lines.append('')
        code_lines.append('#include <string>')
        code_lines.append('#include <vector>')
        code_lines.append('#include <optional>')
        code_lines.append('#include <sqlite3.h>')
        code_lines.append('#include <iostream>')
        code_lines.append('#include <sstream>')
        code_lines.append('')
        code_lines.append('namespace db {')
        code_lines.append('namespace models {')
        code_lines.append('')
        
        # 结构体定义
        code_lines.append(f'struct {class_name} {{')
        
        # 成员变量
        for col in columns:
            col_name = col[1]
            col_type = col[2]
            not_null = col[3] == 1
            cpp_type = sqlite_type_to_cpp_type(col_type, not not_null)
            var_name = to_camel_case(col_name, False)
            #var_name = col_name
            
            code_lines.append(f'  {cpp_type} {var_name};  // {col_name}')
        
        code_lines.append('')
        code_lines.append(f'  {class_name}() = default;')
        code_lines.append('')
        
        # fromStmt 方法
        code_lines.append(f'  static {class_name} fromStmt(sqlite3_stmt* stmt) {{')
        code_lines.append(f'    {class_name} obj;')
        code_lines.append(f'    const char* text;')
        
        for i, col in enumerate(columns):
            col_name = col[1]
            col_type = col[2]
            not_null = col[3] == 1
            cpp_type = sqlite_type_to_cpp_type(col_type, not not_null)
            var_name = to_camel_case(col_name, False)
            #var_name = col_name
            
            code_lines.append(f'    // {col_name}')
            
            if 'std::optional<' in cpp_type:
                code_lines.append(f'    if (sqlite3_column_type(stmt, {i}) != SQLITE_NULL) {{')
                inner_type = cpp_type.replace('std::optional<', '').replace('>', '')
                
                if 'std::string' in inner_type:
                    code_lines.append(f'      text = (const char*)sqlite3_column_text(stmt, {i});')
                    code_lines.append(f'      obj.{var_name} = text ? text : "";')
                elif 'int' in inner_type:
                    code_lines.append(f'      obj.{var_name} = sqlite3_column_int64(stmt, {i});')
                elif 'double' in inner_type:
                    code_lines.append(f'      obj.{var_name} = sqlite3_column_double(stmt, {i});')
                elif 'bool' in inner_type:
                    code_lines.append(f'      obj.{var_name} = sqlite3_column_int(stmt, {i}) != 0;')
                code_lines.append('    }')
            else :
                code_lines.append(f'    text = (const char*)sqlite3_column_text(stmt, {i});')
                code_lines.append(f'    obj.{var_name} = text ? text : "";')

        
        code_lines.append('    return obj;')
        code_lines.append('  }')
        code_lines.append('')
        
        # toString 方法
        code_lines.append('  std::string toString() const {')
        code_lines.append('    std::ostringstream oss;')
        code_lines.append('    oss << "{";')
        
        for i, col in enumerate(columns):
            col_name = col[1]

            var_name = to_camel_case(col_name, False)
             #var_name = col_name
            
            if i > 0:
                code_lines.append('    oss << ", ";')
            
            code_lines.append(f'    oss << \"\\"{col_name}\\": ";')
            
            # 简单处理，避免复杂类型转换
            code_lines.append(f'    oss << {var_name};  // 简化输出')
        
        code_lines.append('    oss << "}";')
        code_lines.append('    return oss.str();')
        code_lines.append('  }')
        code_lines.append('};')
        code_lines.append('')
        
        # DAO 类
        dao_name = f'{class_name}DAO'
        code_lines.append(f'class {dao_name} {{')
        code_lines.append('private:')
        code_lines.append('  sqlite3* db_;')
        code_lines.append('')
        code_lines.append('public:')
        code_lines.append(f'  explicit {dao_name}(sqlite3* db) : db_(db) {{}}')
        code_lines.append('')
        
        # getAll 方法
        code_lines.append(f'  std::vector<{class_name}> getAll() {{')
        code_lines.append(f'    std::vector<{class_name}> result;')
        code_lines.append(f'    const char* sql = "SELECT * FROM {table_name};";')
        code_lines.append('')
        code_lines.append('    sqlite3_stmt* stmt = nullptr;')
        code_lines.append('    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {')
        code_lines.append('      std::cerr << "SQL错误: " << sqlite3_errmsg(db_) << std::endl;')
        code_lines.append('      return result;')
        code_lines.append('    }')
        code_lines.append('')
        code_lines.append('    while (sqlite3_step(stmt) == SQLITE_ROW) {')
        code_lines.append(f'      result.push_back({class_name}::fromStmt(stmt));')
        code_lines.append('    }')
        code_lines.append('')
        code_lines.append('    sqlite3_finalize(stmt);')
        code_lines.append('    return result;')
        code_lines.append('  }')
        code_lines.append('};')
        code_lines.append('')
        
        code_lines.append('} // namespace models')
        code_lines.append('} // namespace db')
        code_lines.append('')
        code_lines.append(f'#endif // {header_guard}')
        
        conn.close()
        return '\n'.join(code_lines)
        
    except Exception as e:
        return f"// 错误: {str(e)}\n"

def main():
    if len(sys.argv) < 2:
        print("用法: python sqlite_to_cpp_simple.py <database.db> [table_name]")
        return
    
    db_path = sys.argv[1]
    table_name = sys.argv[2] if len(sys.argv) > 2 else None
    
    if not os.path.exists(db_path):
        print(f"错误: 文件不存在: {db_path}")
        return
    
    try:
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()
        
        if table_name:
            tables = [table_name]
        else:
            cursor.execute("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%';")
            tables = [row[0] for row in cursor.fetchall()]
        
        conn.close()
        
        for table in tables:
            print(f"生成表: {table}")
            code = generate_table_class_simple(db_path, table)
            
            filename = f"{to_camel_case(table, True)}.h"
            with open(filename, 'w', encoding='utf-8') as f:
                f.write(code)
            
            print(f"  已保存: {filename}")
        
        print(f"\n完成! 生成了 {len(tables)} 个文件")
        
    except Exception as e:
        print(f"错误: {e}")

if __name__ == '__main__':
    main()