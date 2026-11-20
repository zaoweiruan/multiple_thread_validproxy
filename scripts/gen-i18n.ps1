# scripts/gen-i18n.ps1
#
# 由 resources/i18n/strings.json 生成两张头/实现文件：
#   include/i18n/StringId.h      enum class StrId + kStrIdCount
#   src/i18n/StringTable.cpp     4 张静态表（zh-w / zh-u8 / en-w / en-u8）+ 访问器 + static_assert
#
# 生成文件入库（编译期资产，不依赖 PowerShell / 不随包）。
# 校验在生成阶段完成：
#   - id 唯一且符合 ^[A-Za-z][A-Za-z0-9]*(\.[A-Za-z][A-Za-z0-9]*)+$
#   - zh / en 非空；`{}` 占位符数量一致
#   - en 不含 CJK，除非 "fixed": true（语言名等两侧同值项）
#   - `\t` 之后的加速键两侧逐字一致
#   - 同一 模块.区域 分组内，`&` 后的助记符在同语言下不重复（zh / en 各查一遍）
#
# 用法：
#   powershell -File scripts/gen-i18n.ps1           # 生成
#   powershell -File scripts/gen-i18n.ps1 -Check    # 只校验：重新生成到内存并与入库文件比较
#
# 退出码：0 = 成功/一致；1 = 校验失败或（-Check）内容不一致；2 = 参数/文件错误。

param(
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$jsonPath = Join-Path $repoRoot 'resources/i18n/strings.json'
$idHeaderPath = Join-Path $repoRoot 'include/i18n/StringId.h'
$tableSourcePath = Join-Path $repoRoot 'src/i18n/StringTable.cpp'

if (-not (Test-Path -LiteralPath $jsonPath)) {
    Write-Error "字符串资产不存在: $jsonPath"
    exit 2
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Fail([string]$message, [string]$where) {
    Write-Output ("[gen-i18n] 错误: {0}{1}" -f $message, $(if ($where) { "（$where）" } else { "" }))
    exit 1
}

# ---------------------------------------------------------------- 读取资产
$raw = [System.IO.File]::ReadAllText($jsonPath, [System.Text.Encoding]::UTF8)
try {
    $doc = $raw | ConvertFrom-Json
} catch {
    Fail ("strings.json 不是合法 JSON：" + $_.Exception.Message) ''
}
if ($null -eq $doc.strings -or $doc.strings.Count -eq 0) {
    Fail 'strings.json 缺少非空 strings 数组' ''
}

$cjkPattern = '[一-鿿]'
$idPattern = '^[A-Za-z][A-Za-z0-9]*(\.[A-Za-z][A-Za-z0-9]*)+$'

# ---------------------------------------------------------------- 校验
$seen = @{}
$mnemonics = @{}
$index = -1
foreach ($s in $doc.strings) {
    $index++
    $where = "第 " + ($index + 1) + " 项"
    $id = [string]$s.id
    if ($id -eq '') { Fail '缺少 id' $where }
    if ($id -notmatch $idPattern) { Fail ("id 不合法: " + $id) $where }
    if ($seen.ContainsKey($id)) { Fail ("id 重复: " + $id) $where }
    $seen[$id] = $true

    $zh = [string]$s.zh
    $en = [string]$s.en
    if ($zh -eq '') { Fail ("zh 为空: " + $id) $where }
    if ($en -eq '') { Fail ("en 为空: " + $id) $where }

    $fixed = $false
    if ($null -ne $s.fixed) { $fixed = [bool]$s.fixed }

    if ((-not $fixed) -and ($en -match $cjkPattern)) {
        Fail ("en 含中文字符（语言名等两侧同值项请加 fixed 标记）: " + $id) $where
    }

    $zhPlaceholders = ([regex]::Matches($zh, '\{\}')).Count
    $enPlaceholders = ([regex]::Matches($en, '\{\}')).Count
    if ($zhPlaceholders -ne $enPlaceholders) {
        Fail ("占位符数量不一致（zh $zhPlaceholders / en $enPlaceholders）: " + $id) $where
    }

    $tab = [char]9
    $zhTab = $zh.IndexOf($tab)
    $enTab = $en.IndexOf($tab)
    $zhAccel = if ($zhTab -ge 0) { $zh.Substring($zhTab) } else { '' }
    $enAccel = if ($enTab -ge 0) { $en.Substring($enTab) } else { '' }
    if ($zhAccel -ne $enAccel) {
        Fail ("加速键不一致（zh '" + $zhAccel + "' / en '" + $enAccel + "'）: " + $id) $where
    }

    # 助记符唯一性：同 模块.区域 分组、同语言内 & 后的字母不重复
    $segments = $id.Split('.')
    $group = $segments[0] + '.' + $segments[1]
    foreach ($pair in @(@('zh', $zh), @('en', $en))) {
        $text = $pair[1]
        $amp = $text.IndexOf('&')
        if ($amp -lt 0 -or ($amp + 1) -ge $text.Length) { continue }
        $letter = $text.Substring($amp + 1, 1)
        if ($letter -eq '&') { continue }
        $key = $group + '|' + $pair[0] + '|' + $letter.ToLowerInvariant()
        if ($mnemonics.ContainsKey($key)) {
            Fail ("助记符 '" + $letter + "' 在 " + $group + " 下重复（" + $pair[0] + "）: " + $id + " 与 " + $mnemonics[$key]) $where
        }
        $mnemonics[$key] = $id
    }
}

# ---------------------------------------------------------------- 转义
function Get-CppBody([string]$text) {
    $sb = New-Object System.Text.StringBuilder ($text.Length + 8)
    foreach ($ch in $text.ToCharArray()) {
        $code = [int]$ch
        if ($code -eq 92) { [void]$sb.Append('\\') }
        elseif ($code -eq 34) { [void]$sb.Append('\"') }
        elseif ($code -eq 9) { [void]$sb.Append('\t') }
        elseif ($code -eq 10) { [void]$sb.Append('\n') }
        elseif ($code -eq 13) { }
        else { [void]$sb.Append($ch) }
    }
    return $sb.ToString()
}

function Get-EnumName([string]$id) {
    return ($id -replace '\.', '_')
}

# ---------------------------------------------------------------- 生成头文件
$nl = "`n"
$idLines = New-Object System.Collections.Generic.List[string]
$idLines.Add('// 自动生成，请勿手改。');
$idLines.Add('// 源: resources/i18n/strings.json  生成器: scripts/gen-i18n.ps1');
$idLines.Add('');
$idLines.Add('#ifndef I18N_STRING_ID_H');
$idLines.Add('#define I18N_STRING_ID_H');
$idLines.Add('');
$idLines.Add('namespace i18n {');
$idLines.Add('');
$idLines.Add('enum class StrId : int {');
$i2 = -1
foreach ($s in $doc.strings) {
    $i2++
    $name = Get-EnumName ([string]$s.id)
    $idLines.Add(('    {0} = {1},' -f $name, $i2));
}
$idLines.Add('    Count');
$idLines.Add('};');
$idLines.Add('');
$idLines.Add('constexpr int kStrIdCount = static_cast<int>(StrId::Count);');
$idLines.Add('');
$idLines.Add('}  // namespace i18n');
$idLines.Add('');
$idLines.Add('#endif  // I18N_STRING_ID_H');
$idHeader = ($idLines -join $nl) + $nl

# ---------------------------------------------------------------- 生成表实现
function Get-TableBlock([string]$name, [string]$type, [string]$prefix, $items) {
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add(('static const {0} const {1}[] = {{' -f $type, $name));
    foreach ($s in $items) {
        $body = Get-CppBody ([string]$s.$prefix)
        $lines.Add(('    {0}"{1}",' -f $(if ($type -eq 'wchar_t*') { 'L' } else { 'u8' }), $body));
    }
    $lines.Add('};');
    return ($lines -join $nl)
}

$tableLines = New-Object System.Collections.Generic.List[string]
$tableLines.Add('// 自动生成，请勿手改。');
$tableLines.Add('// 源: resources/i18n/strings.json  生成器: scripts/gen-i18n.ps1');
$tableLines.Add('');
$tableLines.Add('#include "i18n/StringTable.h"');
$tableLines.Add('');
$tableLines.Add('namespace i18n {');
$tableLines.Add('namespace table {');
$tableLines.Add('');
$tableLines.Add((Get-TableBlock 'kZhW' 'wchar_t*' 'zh' $doc.strings));
$tableLines.Add('');
$tableLines.Add((Get-TableBlock 'kZhU8' 'char*' 'zh' $doc.strings));
$tableLines.Add('');
$tableLines.Add((Get-TableBlock 'kEnW' 'wchar_t*' 'en' $doc.strings));
$tableLines.Add('');
$tableLines.Add((Get-TableBlock 'kEnU8' 'char*' 'en' $doc.strings));
$tableLines.Add('');
foreach ($pair in @(@('kZhW', 'zh-w'), @('kZhU8', 'zh-u8'), @('kEnW', 'en-w'), @('kEnU8', 'en-u8'))) {
    $tableLines.Add(('static_assert(static_cast<int>(sizeof({0}) / sizeof({0}[0])) == kStrIdCount, "{1} table out of sync");' -f $pair[0], $pair[1]));
}
$tableLines.Add('');
$tableLines.Add('const wchar_t* const* zhW() { return kZhW; }');
$tableLines.Add('const char* const* zhU8() { return kZhU8; }');
$tableLines.Add('const wchar_t* const* enW() { return kEnW; }');
$tableLines.Add('const char* const* enU8() { return kEnU8; }');
$tableLines.Add('int count() { return kStrIdCount; }');
$tableLines.Add('');
$tableLines.Add('}  // namespace table');
$tableLines.Add('}  // namespace i18n');
$tableSource = ($tableLines -join $nl) + $nl

# ---------------------------------------------------------------- 写出 / 校验
function Compare-Generated([string]$path, [string]$content, [string]$label) {
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Output ("[gen-i18n] 缺少生成文件: {0}" -f $label)
        return $false
    }
    $existing = [System.IO.File]::ReadAllText($path, [System.Text.Encoding]::UTF8)
    if ($existing -ne $content) {
        Write-Output ("[gen-i18n] 生成文件与资产不一致: {0}（请运行 scripts/gen-i18n.ps1 重新生成）" -f $label)
        return $false
    }
    return $true
}

if ($Check) {
    $okHeader = Compare-Generated $idHeaderPath $idHeader 'include/i18n/StringId.h'
    $okTable = Compare-Generated $tableSourcePath $tableSource 'src/i18n/StringTable.cpp'
    if ($okHeader -and $okTable) {
        $n = @($doc.strings).Count
        Write-Output ("[gen-i18n] OK（{0} 条，-Check 一致）" -f $n)
        exit 0
    }
    exit 1
}

$dir = Split-Path -Parent $idHeaderPath
if (-not (Test-Path -LiteralPath $dir)) { [void](New-Item -ItemType Directory -Path $dir) }
$dir = Split-Path -Parent $tableSourcePath
if (-not (Test-Path -LiteralPath $dir)) { [void](New-Item -ItemType Directory -Path $dir) }

[System.IO.File]::WriteAllText($idHeaderPath, $idHeader, $utf8NoBom)
[System.IO.File]::WriteAllText($tableSourcePath, $tableSource, $utf8NoBom)
Write-Output ("[gen-i18n] 已生成 {0} 条：include/i18n/StringId.h, src/i18n/StringTable.cpp" -f @($doc.strings).Count)
exit 0
