编译期资产：`strings.json` 是翻译唯一数据源，`scripts/gen-i18n.ps1` 生成 `include/i18n/StringId.h` 与 `src/i18n/StringTable.cpp`。本目录不随包发布，所有文案编译进 exe。
