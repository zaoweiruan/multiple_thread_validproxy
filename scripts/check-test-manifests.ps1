# scripts/check-test-manifests.ps1
#
# 目的：任何「链接了 wxWidgets 动态库」的测试可执行文件都必须嵌入 Common Controls
# v6 清单。缺失时 Windows 会加载 System32\comctl32.dll 5.82，wxcore 需要的
# GetWindowSubclass 不存在，进程在 main 之前就失败，并弹出
#
#     <exe> - 无法找到入口
#     无法定位程序输入点 GetWindowSubclass 于动态链接库 ...wxmsw333u_core...
#
# 的模态对话框。该对话框会一直挡住 ctest，必须人工点「确定」，测试套件就被人为
# 挂住（表现为某个用例耗掉几十分钟后 0xC0000139）。
#
# 判定方式：只检查「二进制里出现 wxmsw 导入串」的 exe —— 即真正链接动态 wx 的
# 测试；纯 C++/静态测试不需要清单，无谓要求会让门禁噪声化。
#
# 退出码：0 全部合格；1 有 wx 测试缺清单（或目录不可读）。
param(
    [string]$Dir = ''
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Dir)) {
    # 测试 exe 有两个输出目录：绝大多数在 <repo>/tests，少数几个
    # （test_proxy_list_model / test_mutex_policy / test_task_* 等）在 <repo>/bin。
    $Dir = (Join-Path $repo 'tests') + ';' + (Join-Path $repo 'bin')
}

$wxMarker = 'wxmsw'
$manifestMarker = 'Microsoft.Windows.Common-Controls'

$checked = 0
$bad = @()
$seen = @{}

foreach ($directory in ($Dir -split ';')) {
    if ([string]::IsNullOrWhiteSpace($directory)) { continue }
    if (-not (Test-Path -LiteralPath $directory)) {
        Write-Output ("[check-test-manifests] FAIL: directory not found: {0}" -f $directory)
        exit 1
    }

    foreach ($exe in Get-ChildItem -LiteralPath $directory -Filter 'test_*.exe' -File) {
        if ($seen.ContainsKey($exe.Name)) { continue }
        $seen[$exe.Name] = $true

        $bytes = [System.IO.File]::ReadAllBytes($exe.FullName)
        if ($bytes.Length -eq 0) { continue }
        # 二进制转 ASCII：非法字节变成 '?'，但 ASCII 探针（导入名/清单 XML）保持完整。
        $text = [System.Text.Encoding]::ASCII.GetString($bytes)

        if ($text.IndexOf($wxMarker, [System.StringComparison]::Ordinal) -lt 0) { continue }
        $checked++

        if ($text.IndexOf($manifestMarker, [System.StringComparison]::Ordinal) -lt 0) {
            $bad += $exe.Name
        }
    }
}

if ($bad.Count -gt 0) {
    Write-Output '[check-test-manifests] FAIL: wx test executables without a Common Controls v6 manifest:'
    foreach ($name in $bad) {
        Write-Output ("  - {0}（加入 tests/wx_test_manifest.rc 到该 target，否则运行时会弹「无法找到入口」并卡住 ctest）" -f $name)
    }
    exit 1
}

Write-Output ("[check-test-manifests] OK（{0} 个 wx 测试可执行文件均含 Common Controls v6 清单）" -f $checked)
exit 0
