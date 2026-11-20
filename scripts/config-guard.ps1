# scripts/config-guard.ps1
#
# 目的：UI 自动化绝不允许改动仓库里的生产配置 <repo>\bin\config.json。
# 该文件是受版本控制的、带本机绝对路径的样例配置；一旦被测试重新序列化，
# 差异会伪装成「开发者改了配置」，而且 nlohmann 的一行 dump 会覆盖人工排版。
#
# 用法（在 build-and-test.bat 里成对调用）：
#   -Snapshot  测试前记录 sha256
#   -Verify    测试后比对，不一致则退出码 1
#
# 退出码：0 一致（或已快照）；1 被改动 / 没有快照 / 文件缺失。

param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Snapshot', 'Verify')]
    [string]$Mode
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$target = Join-Path $repo 'bin\config.json'
$stamp = Join-Path $repo 'temp\config-guard.sha256'

if (-not (Test-Path -LiteralPath $target)) {
    Write-Output ("[config-guard] FAIL: missing {0}" -f $target)
    exit 1
}

$hash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()

if ($Mode -eq 'Snapshot') {
    $dir = Split-Path -Parent $stamp
    if (-not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }
    [System.IO.File]::WriteAllText($stamp, $hash, (New-Object System.Text.UTF8Encoding($false)))
    Write-Output ("[config-guard] snapshotted bin/config.json ({0})" -f $hash.Substring(0, 12))
    exit 0
}

if (-not (Test-Path -LiteralPath $stamp)) {
    Write-Output '[config-guard] FAIL: no snapshot; run with -Snapshot before the tests'
    exit 1
}

$before = ([System.IO.File]::ReadAllText($stamp)).Trim()
if ($before -ne $hash) {
    Write-Output '[config-guard] FAIL: a UI test rewrote bin/config.json (production config)'
    Write-Output ("  before: {0}" -f $before)
    Write-Output ("  after : {0}" -f $hash)
    Write-Output '  restore with: git checkout -- bin/config.json; then find the test that saves config'
    exit 1
}

Write-Output '[config-guard] OK: bin/config.json untouched by the test run'
exit 0
