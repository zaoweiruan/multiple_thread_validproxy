# run_sync_db_schema.ps1
# 安全执行 sync_db_schema.sql：逐条独立执行，忽略「已存在」类错误。
#
# 为什么必须逐条执行：
#   sqlite3 在一条语句出错时会中止该批**后续所有语句**（已实测）。
#   而 sync_db_schema.sql 里的 ALTER TABLE ADD COLUMN 不是幂等的，
#   重复执行必报 duplicate column，会连带后面的 DDL 全部跳过。
#
# 用法: run_sync_db_schema.ps1 [-Db <数据库路径>]

param(
    [string]$Db = (Join-Path (Split-Path $PSScriptRoot -Parent) "bin\worker\guiNDB.db")
)

$ErrorActionPreference = "Continue"
$scriptFile = Join-Path $PSScriptRoot "sync_db_schema.sql"
if (-not (Test-Path $Db))        { Write-Error "目标数据库不存在: $Db";       exit 1 }
if (-not (Test-Path $scriptFile)) { Write-Error "找不到同步脚本: $scriptFile"; exit 1 }

$backupDb = "$Db.bak-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
Copy-Item -LiteralPath $Db -Destination $backupDb -Force
Write-Host "[OK] 已备份至: $backupDb"

$ddl = @(); $buf = ''
foreach ($line in (Get-Content -LiteralPath $scriptFile)) {
    $t = $line.Trim()
    if ($t.StartsWith('--')) { continue }
    if ($t -match '^\.(mode|headers|print|output|separator)\b') { continue }
    if ($t -match '^(SELECT|PRAGMA)\b') { continue }
    if ($t -match '^(FROM|WHERE|ORDER|GROUP|LIMIT|AND|OR)\b') { continue }
    if ($t -eq '') { continue }
    $buf += ' ' + $t
    if ($t.EndsWith(';')) { $ddl += $buf.Trim(); $buf = '' }
}
if ($buf.Trim() -ne '') { $ddl += $buf.Trim() }

Write-Host "[执行] 解析出 $($ddl.Count) 条 DDL"
$ok = 0; $skip = 0; $fail = 0
foreach ($stmt in $ddl) {
    $r = sqlite3 $Db $stmt 2>&1
    if ($LASTEXITCODE -eq 0) { $ok++; continue }
    if (($r -join "`n") -match 'duplicate column|already exists') {
        $skip++; Write-Host ("  -> 跳过: " + $stmt.Substring(0,[Math]::Min(56,$stmt.Length))); continue
    }
    $fail++; Write-Host "  -> 错误: $r"; Write-Host "     SQL: $stmt"
}
Write-Host "`n[汇总] 成功 $ok / 跳过 $skip / 失败 $fail"
Write-Host "`n[验证]"
sqlite3 -column -header $Db "SELECT 'ProfileItem.Region' AS chk, COUNT(*) AS cnt FROM pragma_table_info('ProfileItem') WHERE name='Region';"
if ($fail -gt 0) { Write-Host "`n[完成] 存在失败语句"; exit 1 }
Write-Host "`n[完成] 全部成功"
exit 0
