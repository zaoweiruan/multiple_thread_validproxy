# scripts/check-ui-literals.ps1
#
# 检查 UI 层是否残留「窄字节非 ASCII 字面量」。
#
# 背景（2026-11-17 Win7 bugfix）：
#   wxString 的隐式 const char* 构造使用 wxConvLibc（Windows ANSI 代码页），
#   源文件里的 UTF-8 字面量会被按 GBK/CP1252 解码：
#     - 偶数长度片段 → 乱码（"更新" → "鏇存柊"）
#     - 奇数长度片段 → 转换失败得到空串 → wxMenu 空标签断言 → Debug Alert 弹窗
#   因此 UI 层非 ASCII 字符串必须是：
#     L"..."            (wxString 参数)
#     u8"..."           (std::string / Logger / JSON 载荷，显示侧用 FromUTF8)
#     wxT("...")/_T("...")/wxS("...")  (编译期转宽字符的宏)
#
# 用法：
#   powershell -File scripts/check-ui-literals.ps1             # 默认扫描 src/ui
#   powershell -File scripts/check-ui-literals.ps1 -Root src   # 扫描全仓（噪声更多）
# 退出码：0 = 干净；1 = 存在违规。

param(
    [string]$Root = "src/ui",
    [switch]$Verbose
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$scanRoot = Join-Path $repoRoot $Root
if (-not (Test-Path -LiteralPath $scanRoot)) {
    Write-Error "扫描目录不存在: $scanRoot"
    exit 2
}

$literalPattern = '(?<prefix>(?:u8|u|U|L)?)"(?<body>(?:[^"\\]|\\.)*)"'
$wideMacros = @('wxT(', '_T(', 'wxS(')
$violations = New-Object System.Collections.Generic.List[string]

$files = Get-ChildItem -LiteralPath $scanRoot -Recurse -File |
    Where-Object { $_.Extension -in @('.cpp', '.h', '.hpp') }
foreach ($file in $files) {
    $lines = Get-Content -LiteralPath $file.FullName -Encoding UTF8
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]

        # 去掉行注释（简单：忽略字符串内部的 //）
        $literalMatches = [regex]::Matches($line, $literalPattern)
        $spans = @()
        foreach ($m in $literalMatches) { $spans += , @($m.Index, $m.Index + $m.Length) }

        $cut = -1
        for ($c = 0; $c -lt $line.Length - 1; $c++) {
            if ($line[$c] -eq '/' -and $line[$c + 1] -eq '/') {
                $inside = $false
                foreach ($sp in $spans) { if ($c -ge $sp[0] -and $c -lt $sp[1]) { $inside = $true; break } }
                if (-not $inside) { $cut = $c; break }
            }
        }
        $code = if ($cut -ge 0) { $line.Substring(0, $cut) } else { $line }

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

            $rel = Resolve-Path -LiteralPath $file.FullName -Relative
            $violations.Add(("{0}:{1}: {2}" -f $rel, ($i + 1), $m.Value))
        }
    }
}

if ($violations.Count -eq 0) {
    Write-Output "[check-ui-literals] OK ($Root)"
    exit 0
}

Write-Output ("[check-ui-literals] 发现 {0} 处窄字节非 ASCII 字面量（应改为 L""..."" 或 u8""...""）：" -f $violations.Count)
foreach ($v in $violations) { Write-Output ("  " + $v) }
exit 1
