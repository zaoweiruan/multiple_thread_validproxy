# scripts/check-ui-literals.ps1
#
# 检查两类源码字面量问题：
#
#   -Mode narrow（默认，2026-11-17 Win7 bugfix 门禁）
#     检查 UI 层是否残留「窄字节非 ASCII 字面量」。
#     背景：wxString 的隐式 const char* 构造使用 wxConvLibc（Windows ANSI 代码页），
#     源文件里的 UTF-8 字面量会被按 GBK/CP1252 解码：
#       - 偶数长度片段 → 乱码（"更新" → "鏇存柊"）
#       - 奇数长度片段 → 转换失败得到空串 → wxMenu 空标签断言 → Debug Alert 弹窗
#     因此 UI 层非 ASCII 字符串必须是：
#       L"..."            (wxString 参数)
#       u8"..."           (std::string / Logger / JSON 载荷，显示侧用 FromUTF8)
#       wxT("...")/_T("...")/wxS("...")  (编译期转宽字符的宏)
#
#   -Mode cjk（2026-11-18 双语言门禁）
#     src/ 与 include/ 中不得出现「未登记的中文字符串字面量」（L""/u8""/"" 一律算）。
#     唯一例外是 scripts/i18n-allowlist.txt 中登记的文件；该清单只减不增。
#     src/i18n/** 是生成代码，自动跳过。
#
# 注释与字面量的识别方式：单遍字符扫描（Get-StrippedText），不是逐行截断。
#   - 行注释 //、块注释 /* */ 一律替换为空格，换行保留 → 行号不漂移；
#   - 字符串内（含转义 \"）的 // 与 /* 不算注释；
#   - 原始字符串 R"delim( ... )delim" 整体抹为空（内部常有裸引号）。因此本脚本不会报出
#     写在原始字符串里的中文：当前仓库内无此类写法；若将来需要，请改为查表。
#
# 用法：
#   powershell -File scripts/check-ui-literals.ps1                                  # 默认 narrow + src/ui
#   powershell -File scripts/check-ui-literals.ps1 -Root src                        # narrow，扫全仓
#   powershell -File scripts/check-ui-literals.ps1 -Mode cjk                        # 默认扫 src,include
#   powershell -File scripts/check-ui-literals.ps1 -Mode cjk -Root src -AllowList scripts/i18n-allowlist.txt
#
# allowlist 格式（-Mode cjk）：
#   # 注释行
#   src/ui/MainFrame.cpp | P1 主窗口 88 条 | 2027-06-30 | 88
#     └ 路径（相对仓库根，正斜杠） | 理由 | 复核到期日（YYYY-MM-DD） | 该文件允许的条数上限（可选）
#   前三列必填；到期日已过只告警不失败。
#   第 4 列存在时按「只减不增」强制执行：实测条数 > 上限即失败（失败后应删除多余的中文字面量，
#   而不是上调上限）。迁移过程中实测数下降时，可把上限同步下调。
#
# 退出码：0 = 干净；1 = 存在违规；2 = 参数/配置错误。

param(
    [string[]]$Root = $null,
    [ValidateSet('narrow', 'cjk')]
    [string]$Mode = 'narrow',
    [string]$AllowList = '',
    [switch]$Verbose
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

if ($null -eq $Root -or $Root.Count -eq 0) {
    $Root = if ($Mode -eq 'cjk') { @('src', 'include') } else { @('src/ui') }
}

$scanRoots = @()
foreach ($r in $Root) {
    $full = Join-Path $repoRoot $r
    if (-not (Test-Path -LiteralPath $full)) {
        Write-Error "扫描目录不存在: $full"
        exit 2
    }
    $scanRoots += $full
}

$literalPattern = '(?<prefix>(?:u8|u|U|L)?)"(?<body>(?:[^"\\]|\\.)*)"'
$wideMacros = @('wxT(', '_T(', 'wxS(')
$cjkPattern = '[一-鿿]'          # U+4E00 - U+9FFF
$generatedRel = 'src/i18n/'       # 生成的表本身必然含中文

function Get-RelPath([string]$fullPath) {
    $rel = [System.IO.Path]::GetRelativePath($repoRoot, $fullPath)
    return ($rel -replace '\\', '/')
}

# 单遍扫描：注释替换为空格（保留换行），字符串（含转义）与原始字符串原样保留。
function Get-StrippedText([string]$text) {
    $sb = New-Object System.Text.StringBuilder ($text.Length + 16)
    $n = $text.Length
    $i = 0
    $quote = [char]0
    while ($i -lt $n) {
        $c = $text[$i]
        if ($quote -eq [char]0) {
            if ($c -eq '/' -and ($i + 1) -lt $n) {
                $next = $text[$i + 1]
                if ($next -eq '/') {
                    while ($i -lt $n -and $text[$i] -ne "`n") {
                        [void]$sb.Append(' ')
                        $i++
                    }
                    continue
                }
                if ($next -eq '*') {
                    [void]$sb.Append('  ')
                    $i += 2
                    while ($i -lt $n) {
                        if ($text[$i] -eq '*' -and ($i + 1) -lt $n -and $text[$i + 1] -eq '/') {
                            [void]$sb.Append('  ')
                            $i += 2
                            break
                        }
                        if ($text[$i] -eq "`n") { [void]$sb.Append("`n") } else { [void]$sb.Append(' ') }
                        $i++
                    }
                    continue
                }
            }
            if ($c -eq 'R' -and ($i + 1) -lt $n -and $text[$i + 1] -eq '"') {
                $open = $text.IndexOf('(', $i + 2)
                if ($open -gt 0 -and ($open - ($i + 2)) -le 16) {
                    $delim = $text.Substring($i + 2, $open - $i - 2)
                    $term = ')' + $delim + '"'
                    $close = $text.IndexOf($term, $open + 1, [System.StringComparison]::Ordinal)
                    if ($close -ge 0) {
                        $end = $close + $term.Length
                        # 整体抹为空：原始字符串内部常含裸引号（如 R"JSON({ "a": 1 })JSON"），
                        # 若原样保留会让后续字面量正则跨行误匹配，吞掉真实的违规。
                        for ($k = $i; $k -lt $end; $k++) {
                            if ($text[$k] -eq "`n") { [void]$sb.Append("`n") } else { [void]$sb.Append(' ') }
                        }
                        $i = $end
                        continue
                    }
                }
            }
            if ($c -eq '"' -or $c -eq "'") { $quote = $c }
            [void]$sb.Append($c)
            $i++
            continue
        }
        if ($c -eq '\') {
            [void]$sb.Append($c)
            if (($i + 1) -lt $n) { [void]$sb.Append($text[$i + 1]) }
            $i += 2
            continue
        }
        if ($c -eq $quote) { $quote = [char]0 }
        [void]$sb.Append($c)
        $i++
    }
    return $sb.ToString()
}

$files = @()
foreach ($scanRoot in $scanRoots) {
    $files += Get-ChildItem -LiteralPath $scanRoot -Recurse -File |
        Where-Object { $_.Extension -in @('.cpp', '.h', '.hpp') }
}
$files = $files | Sort-Object FullName -Unique

$violations = New-Object System.Collections.Generic.List[string]

# ---------------------------------------------------------------- allowlist
$allow = @{}
if ($Mode -eq 'cjk' -and $AllowList -ne '') {
    $allowPath = Join-Path $repoRoot $AllowList
    if (-not (Test-Path -LiteralPath $allowPath)) {
        Write-Error "allowlist 不存在: $allowPath"
        exit 2
    }
    $lineNo = 0
    foreach ($raw in Get-Content -LiteralPath $allowPath -Encoding UTF8) {
        $lineNo++
        $line = $raw.Trim()
        if ($line -eq '' -or $line.StartsWith('#')) { continue }
        $parts = @($line -split '\|' | ForEach-Object { $_.Trim() })
        if ($parts.Count -lt 3 -or $parts[0] -eq '' -or $parts[1] -eq '' -or $parts[2] -eq '') {
            Write-Error ("allowlist 第 {0} 行格式错误（需 '路径 | 理由 | YYYY-MM-DD[ | 上限条数]'）：{1}" -f $lineNo, $raw)
            exit 2
        }
        $key = ($parts[0] -replace '\\', '/')
        $maxCount = -1
        if ($parts.Count -ge 4 -and $parts[3] -ne '') {
            $parsed = 0
            if (-not [int]::TryParse($parts[3], [ref]$parsed) -or $parsed -lt 0) {
                Write-Error ("allowlist 第 {0} 行上限条数不是非负整数：{1}" -f $lineNo, $parts[3])
                exit 2
            }
            $maxCount = $parsed
        }
        $allow[$key] = @{ Reason = $parts[1]; Expiry = $parts[2]; Max = $maxCount }
        $expiry = [datetime]::MinValue
        if ([datetime]::TryParseExact($parts[2], 'yyyy-MM-dd',
                [System.Globalization.CultureInfo]::InvariantCulture,
                [System.Globalization.DateTimeStyles]::None, [ref]$expiry)) {
            if ($expiry -lt (Get-Date).Date) {
                Write-Warning ("allowlist 条目已过期，请复核并移除：{0}（{1}）" -f $key, $parts[2])
            }
        } else {
            Write-Error ("allowlist 第 {0} 行到期日不是 YYYY-MM-DD：{1}" -f $lineNo, $parts[2])
            exit 2
        }
    }
}

if ($Mode -eq 'cjk') {
    $report = New-Object System.Collections.Generic.List[string]
    $allowedCount = 0
    foreach ($file in $files) {
        $rel = Get-RelPath $file.FullName
        if ($rel.StartsWith($generatedRel, [System.StringComparison]::OrdinalIgnoreCase)) { continue }

        $text = [System.IO.File]::ReadAllText($file.FullName, [System.Text.Encoding]::UTF8)
        $lines = (Get-StrippedText $text) -split "`n"

        $hits = @()
        for ($i = 0; $i -lt $lines.Count; $i++) {
            foreach ($m in [regex]::Matches($lines[$i], $literalPattern)) {
                if ($m.Groups['body'].Value -match $cjkPattern) {
                    $hits += , @(($i + 1), $m.Value)
                }
            }
        }
        if ($hits.Count -eq 0) { continue }

        if ($allow.ContainsKey($rel)) {
            $entry = $allow[$rel]
            if ($entry.Max -ge 0 -and $hits.Count -gt $entry.Max) {
                $violations.Add(("{0}: 中文字面量 {1} 处，超过 allowlist 上限 {2} 处（清单只减不增）" -f $rel, $hits.Count, $entry.Max))
                $report.Add(("  {0}: {1} 处（上限 {2}，已超）" -f $rel, $hits.Count, $entry.Max))
                continue
            }
            $allowedCount += $hits.Count
            if ($Verbose) {
                $capText = if ($entry.Max -ge 0) { "/上限 $($entry.Max)" } else { '' }
                $report.Add(("  [allow] {0}: {1} 处{2}（{3}）" -f $rel, $hits.Count, $capText, $entry.Reason))
            }
            continue
        }

        foreach ($h in $hits) {
            $violations.Add(("{0}:{1}: {2}" -f $rel, $h[0], $h[1]))
        }
        $report.Add(("  {0}: {1} 处" -f $rel, $hits.Count))
    }

    if ($Verbose) { foreach ($r in $report) { Write-Output $r } }

    if ($violations.Count -eq 0) {
        $rootLabel = ($Root -join ', ')
        Write-Output ("[check-ui-literals] OK -Mode cjk ({0}；{1} 处已登记于 allowlist）" -f $rootLabel, $allowedCount)
        exit 0
    }

    Write-Output ("[check-ui-literals] -Mode cjk 发现 {0} 处违规（未登记的中文字面量 / 超出 allowlist 上限；应改为 i18n::wx/u8 查表，或登记进 allowlist）：" -f $violations.Count)
    foreach ($v in $violations) { Write-Output ("  " + $v) }
    exit 1
}

# ------------------------------------------------------------------- narrow
foreach ($file in $files) {
    $text = [System.IO.File]::ReadAllText($file.FullName, [System.Text.Encoding]::UTF8)
    $lines = (Get-StrippedText $text) -split "`n"
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $code = $lines[$i]

        foreach ($m in [regex]::Matches($code, $literalPattern)) {
            $prefix = $m.Groups['prefix'].Value
            if ($prefix -ne '') { continue }              # L/u/U/u8 均显式声明了编码

            $body = $m.Groups['body'].Value
            $hasNonAscii = $false
            foreach ($ch in $body.ToCharArray()) {
                if ([int][char]$ch -gt 127) { $hasNonAscii = $true; break }
            }
            if (-not $hasNonAscii) { continue }

            $before = $code.Substring(0, $m.Index).TrimEnd()
            $wrapped = $false
            foreach ($macro in $wideMacros) {
                if ($before.EndsWith($macro)) { $wrapped = $true; break }
            }
            if ($wrapped) { continue }

            $violations.Add(("{0}:{1}: {2}" -f (Get-RelPath $file.FullName), ($i + 1), $m.Value))
        }
    }
}

if ($violations.Count -eq 0) {
    Write-Output "[check-ui-literals] OK ($($Root -join ', '))"
    exit 0
}

Write-Output ("[check-ui-literals] 发现 {0} 处窄字节非 ASCII 字面量（应改为 L""..."" 或 u8""...""）：" -f $violations.Count)
foreach ($v in $violations) { Write-Output ("  " + $v) }
exit 1
