#ifndef I18N_STRING_TABLE_H
#define I18N_STRING_TABLE_H

#include "i18n/StringId.h"

// 生成的表（src/i18n/StringTable.cpp）只暴露访问器：4 张 const 静态表，
// 有意不做运行期写入，任意线程只读、零锁。
namespace i18n {
namespace table {

const wchar_t* const* zhW();
const char* const* zhU8();
const wchar_t* const* enW();
const char* const* enU8();
int count();

}  // namespace table
}  // namespace i18n

#endif  // I18N_STRING_TABLE_H
