# scripts/package-win7-release.ps1
#
# 发布压缩包打包脚本（固定口径，后续一律遵照执行）
#
# 口径（见 docs/2026-11-17-Reference-ReleasePackaging-v1.0.md）：
#   * 发布资产共 3 个：validproxy.exe、validproxy-cli.exe、validproxy-<ver>-win64.zip
#   * 压缩包内 **只放 libwinpthread-1.dll**（MSVCRT 版 Win7 运行时），
#     **不放** exe / validproxy-cli / 模板 / README / 图标 / config.json
#
# 用法：
#   powershell -File scripts/package-win7-release.ps1                 # 版本取 git 最新 tag
#   powershell -File scripts/package-win7-release.ps1 -Version 1.6.1
#   powershell -File scripts/package-win7-release.ps1 -Version 1.6.1 -OutDir bin
# 退出码：0 = 成功；1 = 校验失败；2 = 参数/环境问题。

param(
    [string]$Version = "",
    [string]$OutDir = "bin",
    [string]$Dll = "",
    [string]$Objdump = ""
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

# 期望的 MSVCRT 版 libwinpthread（与 resources/win7/README.md 一致）
$ExpectedSha256 = 'B3AD954EC35305895E2370D0A1E3CA6A8203DB1D37C7C49CF5D06E72A5F0DB69'
$DllEntryName = 'libwinpthread-1.dll'

if ($Dll -eq "") { $Dll = Join-Path $repoRoot 'resources/win7/libwinpthread-1.dll' }
if (-not (Test-Path -LiteralPath $Dll)) {
    Write-Error "找不到 MSVCRT 版 libwinpthread: $Dll"
    exit 2
}

# ── 1. 版本号：参数 > git tag > 缺省 ────────────────────────────────
if ($Version -eq "") {
    try {
        $tag = (git -C $repoRoot describe --tags --abbrev=0 2>$null)
        if ($tag) { $Version = $tag.TrimStart('v', 'V') }
    } catch { }
}
if ($Version -eq "") { $Version = '1.6.1' }

# ── 2. DLL 校验：sha256 + 导入表 ────────────────────────────────────
$sha = (Get-FileHash -LiteralPath $Dll -Algorithm SHA256).Hash
if ($sha -ne $ExpectedSha256) {
    Write-Error ("libwinpthread sha256 不符：`n  实际 $sha`n  期望 $ExpectedSha256`n请确认 resources/win7/README.md 的来源与替换流程。")
    exit 1
}

if ($Objdump -eq "") {
    foreach ($c in @("E:\mingw64\bin\objdump.exe", "E:\clion\bin\mingw\bin\objdump.exe")) {
        if (Test-Path -LiteralPath $c) { $Objdump = $c; break }
    }
}
if ($Objdump -ne "" -and (Test-Path -LiteralPath $Objdump)) {
    $imports = @()
    foreach ($line in (& $Objdump -p $Dll 2>&1)) {
        if ($line -match 'DLL Name:\s*(\S+)') { $imports += $Matches[1] }
    }
    foreach ($imp in $imports) {
        if ($imp -match '^(api-ms-win-crt|ucrtbase)') {
            Write-Error ("libwinpthread 依赖 UCRT（$imp），Win7 裸机不可用，拒绝打包。")
            exit 1
        }
    }
    Write-Output ("[package] imports: " + ($imports -join ', '))
}

# ── 3. 只放一个 DLL 的压缩包 ────────────────────────────────────────
$outDirPath = Join-Path $repoRoot $OutDir
if (-not (Test-Path -LiteralPath $outDirPath)) { New-Item -ItemType Directory -Force -Path $outDirPath | Out-Null }
$zipPath = Join-Path $outDirPath ("validproxy-{0}-win64.zip" -f $Version)
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::Open($zipPath, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    # 条目固定为根目录下的 libwinpthread-1.dll，不带目录前缀
    [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
        $archive, $Dll, $DllEntryName, [System.IO.Compression.CompressionLevel]::Optimal)
}
finally {
    $archive.Dispose()
}

# ── 4. 结果与自检 ──────────────────────────────────────────────────
$zipInfo = Get-Item -LiteralPath $zipPath
$zipSha = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
$check = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
$entries = @($check.Entries | ForEach-Object { $_.FullName })
$check.Dispose()

Write-Output ("[package] zip    : " + $zipPath)
Write-Output ("[package] size   : " + $zipInfo.Length + " bytes")
Write-Output ("[package] sha256 : " + $zipSha)
Write-Output ("[package] entries: " + ($entries -join ', '))

if ($entries.Count -ne 1 -or $entries[0] -ne $DllEntryName) {
    Write-Error "压缩包条目不符规范（应只含 $DllEntryName）。"
    exit 1
}

Write-Output "[package] OK — 压缩包内只含 $DllEntryName（规范见 docs/2026-11-17-Reference-ReleasePackaging-v1.0.md）"
exit 0
