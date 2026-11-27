# scripts/package-win7-release.ps1
#
# 发布前校验脚本（固定口径，见 docs/2026-11-17-Reference-ReleasePackaging-v1.0.md v1.1）：
#   * 发布资产固定为 2 个：validproxy.exe、validproxy-cli.exe（均为全静态 EXE）
#   * msvcrt-win32 工具链（win32 线程 + -static）下 EXE 只导入系统 DLL，
#     不再需要 libwinpthread-1.dll，因此 **不生成 zip 资产**。
#
# 用法：
#   powershell -File scripts/package-win7-release.ps1
# 退出码：0 = 校验通过（无 zip 生成）；1 = 产物存在运行时 DLL 依赖；2 = 参数/环境问题。

param(
    [string]$Objdump = ""
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$binDir = Join-Path $repoRoot 'bin'

# ── 1. 确认两个 EXE 存在 ──────────────────────────────────────────────
$exes = @(
    (Join-Path $binDir 'validproxy.exe'),
    (Join-Path $binDir 'validproxy-cli.exe')
)
foreach ($exe in $exes) {
    if (-not (Test-Path -LiteralPath $exe)) {
        Write-Error "找不到发布产物：$exe（请先执行 cmake --build --preset release --target validproxy validproxy-cli）"
        exit 2
    }
}

# ── 2. 核验导入表：不得含任何 MinGW 运行时 DLL ────────────────────────
if ($Objdump -eq "") {
    foreach ($c in @("D:\mingw64-win32-msvcrt\bin\objdump.exe", "E:\mingw64\bin\objdump.exe", "E:\clion\bin\mingw\bin\objdump.exe")) {
        if (Test-Path -LiteralPath $c) { $Objdump = $c; break }
    }
}
if ($Objdump -eq "" -or -not (Test-Path -LiteralPath $Objdump)) {
    Write-Error "找不到 objdump.exe（可用 -Objdump <path> 指定）"
    exit 2
}

$forbidden = @('libwinpthread', 'libstdc++-6', 'libgcc_s_', 'wxmsw', 'wxbase', 'api-ms-win-crt', 'ucrtbase')
$failed = $false
foreach ($exe in $exes) {
    $imports = @()
    foreach ($line in (& $Objdump -p $exe 2>&1)) {
        if ($line -match 'DLL Name:\s*(\S+)') { $imports += $Matches[1] }
    }
    Write-Output ("[package] {0}: {1}" -f (Split-Path $exe -Leaf), ($imports -join ', '))
    foreach ($imp in $imports) {
        foreach ($bad in $forbidden) {
            if ($imp -like "*$bad*") {
                Write-Error ("{0} 仍依赖运行时 DLL「{1}」，不符合全静态发布口径。" -f (Split-Path $exe -Leaf), $imp)
                $failed = $true
            }
        }
    }
    if ($imports.Count -eq 0) {
        Write-Error ("无法从 {0} 读取导入表（objdump 输出异常？）" -f (Split-Path $exe -Leaf))
        $failed = $true
    }
}
if ($failed) { exit 1 }

# ── 3. 结果 ──────────────────────────────────────────────────────────
foreach ($exe in $exes) {
    $info = Get-Item -LiteralPath $exe
    $sha = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    Write-Output ("[package] {0}: {1} bytes, sha256={2}" -f $info.Name, $info.Length, $sha)
}
Write-Output "[package] OK — 两个全静态 EXE，无 MinGW 运行时 DLL，无需压缩包（规范见 docs/2026-11-17-Reference-ReleasePackaging-v1.0.md v1.1）"
exit 0
