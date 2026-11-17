# scripts/check-win7-deps.ps1
#
# Windows 7 SP1 可运行性闸门：检查 exe（以及随包 DLL）的导入表，
# 拒绝一切「Win7 裸机不存在」的依赖。
#
# 背景（2026-11-17）：`libwinpthread-1.dll`（E:\clion\bin\mingw 与 E:\mingw64
# 两个工具链自带的版本）导入 UCRT API 集（api-ms-win-crt-*），未装 KB2999226
# 的 Win7 会在加载期报：
#   「无法启动此程序，因为计算机中丢失 api-ms-win-crt-convert-l1-1-0.dll」
# 修复方式=发布包随附 MSVCRT 版 libwinpthread（resources/win7/，仅 KERNEL32+msvcrt）。
#
# 用法：
#   powershell -File scripts/check-win7-deps.ps1                          # bin\validproxy.exe
#   powershell -File scripts/check-win7-deps.ps1 -Path bin -IncludeDlls   # 发布目录
#   powershell -File scripts/check-win7-deps.ps1 -Path bin -IncludeDlls -IncludeDevBinaries
# 退出码：0 = Win7 可运行；1 = 存在不可用依赖；2 = 参数/工具问题。

param(
    [string]$Path = "bin/validproxy.exe",
    [switch]$IncludeDlls,
    [switch]$IncludeDevBinaries,
    [string]$Objdump = ""
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$target = Join-Path $repoRoot $Path
if (-not (Test-Path -LiteralPath $target)) {
    Write-Error "目标不存在: $target"
    exit 2
}

if ($Objdump -eq "") {
    foreach ($c in @("E:\mingw64\bin\objdump.exe", "E:\clion\bin\mingw\bin\objdump.exe")) {
        if (Test-Path -LiteralPath $c) { $Objdump = $c; break }
    }
}
if ($Objdump -eq "" -or -not (Test-Path -LiteralPath $Objdump)) {
    $cmd = Get-Command objdump.exe -ErrorAction SilentlyContinue
    if ($null -eq $cmd) {
        Write-Error "找不到 objdump.exe（可用 -Objdump 指定）"
        exit 2
    }
    $Objdump = $cmd.Source
}

# Win7 SP1 自带 / 系统级 DLL（不需要随包）
$systemAllow = @(
    'ntdll', 'kernel32', 'kernelbase', 'user32', 'gdi32', 'advapi32', 'shell32',
    'shlwapi', 'comctl32', 'comdlg32', 'ole32', 'oleaut32', 'oleacc', 'msimg32',
    'winspool.drv', 'version', 'uxtheme', 'ws2_32', 'iphlpapi', 'secur32',
    'dbghelp', 'bcrypt', 'crypt32', 'msvcrt', 'psapi', 'imm32', 'winmm',
    'setupapi', 'cfgmgr32', 'wintrust', 'rpcrt4', 'winhttp', 'dnsapi', 'mswsock',
    'mpr', 'netapi32', 'userenv', 'wtsapi32', 'shcore', 'powrprof', 'nsi',
    'sspicli', 'sechost', 'gdiplus', 'msacm32', 'wininet', 'wldap32', 'normaliz'
)

# 明确允许随包分发的 MinGW 运行时（本身也会被扫描导入表）
$shippedAllow = @('libwinpthread-1.dll', 'libgcc_s_seh-1.dll', 'libstdc++-6.dll')

# 明确禁止：UCRT 运行时（Win7 裸机不存在）
$forbidden = @('api-ms-win-crt', 'ucrtbase', 'vcruntime', 'msvcp')

function Get-Imports([string]$file) {
    $out = & $Objdump -p $file 2>&1
    $names = @()
    foreach ($line in $out) {
        if ($line -match 'DLL Name:\s*(\S+)') { $names += $Matches[1] }
    }
    return $names
}

$files = @()
$item = Get-Item -LiteralPath $target
if ($item.PSIsContainer) {
    $all = @(Get-ChildItem -LiteralPath $target -File | Where-Object {
        $_.Extension -eq '.exe' -or ($IncludeDlls -and $_.Extension -eq '.dll')
    })
    if (-not $IncludeDevBinaries) {
        $all = @($all | Where-Object { $_.Name -notlike 'test_*' -and $_.Name -notlike 'minwx*' })
    }
    $files = $all
} else {
    $files = @($item)
    if ($IncludeDlls) {
        $dir = Split-Path -Parent $item.FullName
        $files += @(Get-ChildItem -LiteralPath $dir -Filter *.dll -File)
    }
}

$violations = @()
$checked = @()
foreach ($f in $files) {
    $checked += $f.Name
    $imports = Get-Imports $f.FullName
    foreach ($imp in $imports) {
        $low = $imp.ToLowerInvariant()
        $isForbidden = $false
        foreach ($bad in $forbidden) {
            if ($low.StartsWith($bad)) { $isForbidden = $true; break }
        }
        if ($isForbidden) {
            $violations += ("{0} -> {1} (UCRT: Win7 裸机缺失，需 KB2999226)" -f $f.Name, $imp)
            continue
        }
        if (($low -replace '\.dll$', '') -in $systemAllow) { continue }
        if ($low -in $shippedAllow) { continue }
        $violations += ("{0} -> {1} (非系统 DLL，Win7 部署需随包提供)" -f $f.Name, $imp)
    }
}

if ($violations.Count -eq 0) {
    Write-Output ("[check-win7-deps] OK: " + ($checked -join ', '))
    exit 0
}

Write-Output "[check-win7-deps] 发现不满足 Win7 SP1 裸机的依赖："
foreach ($v in $violations) { Write-Output ("  " + $v) }
exit 1
