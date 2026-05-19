# Silent Harbor — Automated Test Script v2
# Plan: 1779073276415-silent-harbor.md
# Scope: Build + Unit tests + Stress compilation verification
# Does NOT run xray-dependent CLI paths (they block for real proxy tests)

$ErrorActionPreference = 'Continue'
$buildDir = "E:\eclipse_workspace\multiple_thread_validproxy\build"
$binDir   = "E:\eclipse_workspace\multiple_thread_validproxy\bin"
$logFile  = "E:\eclipse_workspace\multiple_thread_validproxy\Testing\test_run_$(Get-Date -Format 'yyyyMMdd_HHmmss').log"

function Write-Log($msg) {
    $ts = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    "$ts  $msg" | Tee-Object -FilePath $logFile -Append
}

$passCount = 0; $failCount = 0

function Run-Test {
    param([string]$Label, [scriptblock]$Cmd)
    Write-Log "=== START: $Label ==="
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        $output = & $Cmd 2>&1 | Out-String
        $sw.Stop()
        if ($LASTEXITCODE -eq 0 -or $LASTEXITCODE -eq $null) {
            Write-Log "=== PASS: $Label (${sw.Elapsed.TotalSeconds:F1}s) ==="
            $script:passCount++
        } else {
            Write-Log "=== FAIL: $Label exit=$LASTEXITCODE (${sw.Elapsed.TotalSeconds:F1}s) ==="
            Write-Log $output
            $script:failCount++
        }
    } catch {
        $sw.Stop()
        Write-Log "=== FAIL: $Label (${sw.Elapsed.TotalSeconds:F1}s) — $($_.Exception.Message) ==="
        $script:failCount++
    }
}

# ── 1. Build ───────────────────────────────────────────────────────
Run-Test "Build-Release" {
    cmake --build $buildDir --parallel 8
}

# ── 2. Unit tests ──────────────────────────────────────────────────
Run-Test "test_curl_easy_handle" {
    & "$binDir\test_curl_easy_handle.exe" --gtest_color=no
}

Run-Test "test_dedup_11" {
    & "$binDir\test_dedup.exe" --gtest_color=no
}

Run-Test "test_model_3" {
    & "$binDir\test_model.exe" --gtest_color=no
}

# ── 3. Linkage check — verify all symbols resolve ─────────────────
#     Dump all symbols from the GUI binary; grep for the three
#     new/renamed symbols introduced by Silent Harbor.
$nmOk = $true

$symbols = & "E:\w64devkit\bin\nm.exe" "$binDir\validproxy.exe" 2>&1 | Out-String

$required = @(
    "?findFirstProxyAsync",        # new async method
    "?findBestProxyAsync",         # new async method
    "?doFindFirstProxy",           # async worker
    "?doFindBestProxy",            # async worker
    "?refreshResults",             # ProxyListPanel inline delay refresh
    "?selectProxyByIndexId"        # linear-scan row selection
)

foreach ($sym in $required) {
    if ($symbols -match [regex]::Escape($sym)) {
        Write-Log "  SYMBOL OK: $sym"
    } else {
        Write-Log "  SYMBOL MISSING: $sym"
        $nmOk = $false
    }
}

if ($nmOk) { $passCount++ } else { $failCount++ }

# ── Summary ────────────────────────────────────────────────────────
Write-Log ""
Write-Log "============================="
Write-Log "PASSED: $passCount   FAILED: $failCount"
Write-Log "============================="
exit ($failCount -gt 0)
