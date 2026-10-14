# run_sync_db_schema.ps1
# 安全执行 sync_db_schema.sql：逐条独立执行，忽略「已存在」类错误。
#
# 为什么必须逐条执行：
#   sqlite3 在一批语句中遇到错误会中止该批**后续所有语句**（已实测）。
#   而 ALTER TABLE ADD COLUMN / INSERT 均非幂等，重复执行必报错，
#   会连带后面的语句全部被跳过。
#
# 两个实测踩到的坑：
#   1) 不可用 `sqlite3 $Db $stmt` 直接传参——Windows 下 PowerShell 会吞掉
#      SQL 中的双引号（JSON 值里的 {"id": 变成 {id:），24 条 INSERT 全部语法错误。
#      故改为把每条语句写入临时 .sql 后用 `.read` 执行。
#   2) `.read` 的路径必须用正斜杠——sqlite3 把反斜杠当转义符
#      （C:\Users -> C:Users）。
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

# ---- 解析 .sql：取全部语句（DDL + 种子 INSERT），跳过注释行与验证段查询 ----
$ddl = @(); $buf = ''
foreach ($line in (Get-Content -LiteralPath $scriptFile)) {
    $t = $line.Trim()
    if ($t.StartsWith('--'))  { continue }
    if ($t -match '^\.(mode|headers|print|output|separator)\b') { continue }
    if ($t -match '^(SELECT|PRAGMA)\b') { continue }
    if ($t -match '^(FROM|WHERE|ORDER|GROUP|LIMIT|AND|OR)\b') { continue }
    if ($t -eq '')            { continue }
    $buf += ' ' + $t
    if ($t.EndsWith(';')) { $ddl += $buf.Trim(); $buf = '' }
}
if ($buf.Trim() -ne '') { $ddl += $buf.Trim() }

Write-Host "[执行] 解析出 $($ddl.Count) 条语句"
$tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("sqlsync_" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $tmpDir | Out-Null
$ok = 0; $skip = 0; $fail = 0; $n = 0
foreach ($stmt in $ddl) {
    $n++
    $sqlFile = Join-Path $tmpDir ("s{0}.sql" -f $n)
    Set-Content -LiteralPath $sqlFile -Value ($stmt + ";") -Encoding UTF8
    $sqlFileFwd = $sqlFile -replace '\\', '/'
    $r = sqlite3 $Db ".read $sqlFileFwd" 2>&1
    if ($LASTEXITCODE -eq 0) { $ok++; continue }
    if (($r -join "`n") -match 'duplicate column|already exists') {
        $skip++
        Write-Host ("  -> 跳过: " + $stmt.Substring(0, [Math]::Min(56, $stmt.Length)))
        continue
    }
    $fail++
    $msg = ($r -join ' ')
    Write-Host ("  -> 错误: " + $msg.Substring(0, [Math]::Min(90, $msg.Length)))
    Write-Host ("     SQL: " + $stmt.Substring(0, [Math]::Min(150, $stmt.Length)))
}
Remove-Item $tmpDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "[汇总] 成功 $ok / 跳过 $skip / 失败 $fail"

Write-Host ""
Write-Host "[验证]"
sqlite3 -column -header $Db "SELECT 'ProfileItem.Region' AS chk, COUNT(*) AS cnt FROM pragma_table_info('ProfileItem') WHERE name='Region';"
sqlite3 -column -header $Db "SELECT 'ComponentBundle 行数' AS chk, COUNT(*) AS cnt FROM ComponentBundle;"
sqlite3 -column -header $Db "SELECT 'FullConfigTemplateItem 行数' AS chk, COUNT(*) AS cnt FROM FullConfigTemplateItem;"

if ($fail -gt 0) { Write-Host ""; Write-Host "[完成] 存在失败语句"; exit 1 }
Write-Host ""
Write-Host "[完成] 全部成功"
exit 0
