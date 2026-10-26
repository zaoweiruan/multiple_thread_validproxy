# run_sync_db_schema.ps1
# 安全执行 sync_db_schema.sql：逐条独立执行，自动忽略「已存在」类错误。
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
# 用法:
#   .\run_sync_db_schema.ps1                          # 默认维护 bin\worker\guiNDB.db
#   .\run_sync_db_schema.ps1 -Db bin\guiNDB.db         # 维护指定库
#   .\run_sync_db_schema.ps1 -Db bin\guiNDB.db -VerifyOnly  # 只校验，不执行/不备份
#   .\run_sync_db_schema.ps1 -SqliteExe "D:\sqlite3.exe"
#
# 说明:
#   - 默认备份使用 sqlite3 `.backup`（WAL 安全）；失败才回退到 Copy-Item。
#   - sync_db_schema.sql 的种子数据已改为 INSERT OR IGNORE，重复执行不会
#     删除或覆盖用户自定义的 ComponentBundle / FullConfigTemplateItem。

param(
    [string]$Db = (Join-Path (Split-Path $PSScriptRoot -Parent) "bin\worker\guiNDB.db"),
    [string]$SqliteExe = "",
    [switch]$VerifyOnly,
    [switch]$SkipBackup
)

$ErrorActionPreference = "Stop"
$scriptFile = Join-Path $PSScriptRoot "sync_db_schema.sql"

function Resolve-SqliteExe {
    param([string]$Explicit)

    if ($Explicit -ne "") {
        if (-not (Test-Path -LiteralPath $Explicit)) {
            throw "指定的 sqlite3 不存在: $Explicit"
        }
        return (Resolve-Path -LiteralPath $Explicit).Path
    }

    $command = Get-Command "sqlite3.exe" -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $candidates = @(
        "C:\Program Files (x86)\SQLite Expert\Professional 5\sqlite3.exe",
        "C:\Program Files\SQLite Expert\Professional 5\sqlite3.exe",
        "C:\sqlite3\sqlite3.exe",
        "C:\sqlite\sqlite3.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw "找不到 sqlite3.exe，请用 -SqliteExe 指定路径。"
}

function Backup-Database {
    param(
        [string]$Sqlite,
        [string]$DbPath
    )

    $backup = "$DbPath.bak-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
    $backupFwd = $backup -replace '\\', '/'
    $output = & $Sqlite $DbPath ".backup `"$backupFwd`"" 2>&1
    if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $backup)) {
        Write-Host "[OK] 已备份至: $backup"
        return
    }

    Write-Warning "sqlite3 .backup 失败，回退到 Copy-Item（请先确认数据库未被占用）。"
    if ($output) {
        Write-Warning (($output | Out-String).Trim())
    }
    Copy-Item -LiteralPath $DbPath -Destination $backup -Force
    Write-Host "[OK] 已复制备份至: $backup"
}

# 按 SQL 语法切分语句：正确处理单引号、双引号、方括号和注释。
# 只返回需要执行的 DDL/DML；验证段中的 SELECT/PRAGMA 交给 -VerifyOnly 校验。
function Get-SqlStatements {
    param([string]$Path)

    $text = [System.IO.File]::ReadAllText($Path, [System.Text.Encoding]::UTF8)

    # 去掉 sqlite3 点命令行（.mode / .headers 等），它们仅用于人工查看验证段。
    $lines = $text -split "`r?`n"
    $keptLines = New-Object System.Collections.Generic.List[string]
    foreach ($line in $lines) {
        if ($line.TrimStart().StartsWith(".")) {
            continue
        }
        $keptLines.Add($line)
    }
    $text = $keptLines -join "`n"

    $raw = New-Object System.Collections.Generic.List[string]
    $buffer = New-Object System.Text.StringBuilder
    $state = "Normal"
    $index = 0
    $length = $text.Length

    while ($index -lt $length) {
        $ch = $text[$index]
        $next = [char]0
        if (($index + 1) -lt $length) {
            $next = $text[$index + 1]
        }

        if ($state -eq "Normal") {
            if ($ch -eq '-' -and $next -eq '-') {
                $state = "LineComment"
                $index += 2
                continue
            }
            if ($ch -eq '/' -and $next -eq '*') {
                $state = "BlockComment"
                $index += 2
                continue
            }
            if ($ch -eq "'") {
                $state = "SingleQuote"
                [void]$buffer.Append($ch)
                $index++
                continue
            }
            if ($ch -eq '"') {
                $state = "DoubleQuote"
                [void]$buffer.Append($ch)
                $index++
                continue
            }
            if ($ch -eq '[') {
                $state = "Bracket"
                [void]$buffer.Append($ch)
                $index++
                continue
            }
            if ($ch -eq ';') {
                $statement = $buffer.ToString().Trim()
                if ($statement.Length -gt 0) {
                    $raw.Add($statement)
                }
                [void]$buffer.Clear()
                $index++
                continue
            }
            [void]$buffer.Append($ch)
            $index++
            continue
        }

        if ($state -eq "SingleQuote") {
            [void]$buffer.Append($ch)
            if ($ch -eq "'") {
                if ($next -eq "'") {
                    [void]$buffer.Append($next)
                    $index += 2
                    continue
                }
                $state = "Normal"
            }
            $index++
            continue
        }

        if ($state -eq "DoubleQuote") {
            [void]$buffer.Append($ch)
            if ($ch -eq '"') {
                if ($next -eq '"') {
                    [void]$buffer.Append($next)
                    $index += 2
                    continue
                }
                $state = "Normal"
            }
            $index++
            continue
        }

        if ($state -eq "Bracket") {
            [void]$buffer.Append($ch)
            if ($ch -eq ']') {
                if ($next -eq ']') {
                    [void]$buffer.Append($next)
                    $index += 2
                    continue
                }
                $state = "Normal"
            }
            $index++
            continue
        }

        if ($state -eq "LineComment") {
            if ($ch -eq "`r" -or $ch -eq "`n") {
                [void]$buffer.Append(" ")
                $state = "Normal"
            }
            $index++
            continue
        }

        # BlockComment
        if ($ch -eq '*' -and $next -eq '/') {
            $state = "Normal"
            $index += 2
            continue
        }
        $index++
    }

    $tail = $buffer.ToString().Trim()
    if ($tail.Length -gt 0) {
        $raw.Add($tail)
    }

    $result = New-Object System.Collections.Generic.List[string]
    foreach ($statement in $raw) {
        $lead = $statement.TrimStart()
        if ($lead -match '^(SELECT|PRAGMA|EXPLAIN)\b') {
            continue
        }
        $result.Add($statement)
    }
    return $result.ToArray()
}

function Invoke-SqliteSql {
    param(
        [string]$Sqlite,
        [string]$DbPath,
        [string]$Sql,
        [string]$TempDir,
        [string]$Name
    )

    $sqlFile = Join-Path $TempDir ($Name + ".sql")
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($sqlFile, $Sql + ";" + "`n", $utf8NoBom)
    $sqlFileFwd = $sqlFile -replace '\\', '/'

    $output = & $Sqlite $DbPath ".read `"$sqlFileFwd`"" 2>&1
    return [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Output   = (($output | Out-String).Trim())
    }
}

function Invoke-SqliteScalar {
    param(
        [string]$Sqlite,
        [string]$DbPath,
        [string]$Sql,
        [string]$TempDir,
        [string]$Name
    )

    $result = Invoke-SqliteSql -Sqlite $Sqlite -DbPath $DbPath -Sql $Sql -TempDir $TempDir -Name $Name
    if ($result.ExitCode -ne 0) {
        throw "校验 SQL 执行失败: $Sql`n$($result.Output)"
    }
    return $result.Output.Trim()
}

$script:validationFailures = 0
function Assert-Validation {
    param(
        [string]$Label,
        [string]$Actual,
        [string]$Expected,
        [string]$Mode = "eq"
    )

    $ok = $false
    if ($Mode -eq "eq") {
        $ok = ($Actual -eq $Expected)
    } else {
        $ok = ([int]$Actual -ge [int]$Expected)
    }

    if ($ok) {
        Write-Host ("[PASS] {0} = {1}" -f $Label, $Actual)
    } else {
        $script:validationFailures++
        Write-Host ("[FAIL] {0} = {1} (期望 {2} {3})" -f $Label, $Actual, $Mode, $Expected)
    }
}

if (-not (Test-Path -LiteralPath $Db)) {
    Write-Error "目标数据库不存在: $Db"
    exit 1
}
if (-not (Test-Path -LiteralPath $scriptFile)) {
    Write-Error "找不到同步脚本: $scriptFile"
    exit 1
}

if (-not [System.IO.Path]::IsPathRooted($Db)) {
    $Db = Join-Path (Get-Location).Path $Db
}
$Db = [System.IO.Path]::GetFullPath($Db)

try {
    $sqlite = Resolve-SqliteExe -Explicit $SqliteExe
} catch {
    Write-Error $_
    exit 1
}

Write-Host "目标数据库: $Db"
Write-Host "sqlite3:    $sqlite"

$tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("sqlsync_" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $tmpDir | Out-Null
$ok = 0
$skip = 0
$fail = 0

try {
    if (-not $VerifyOnly) {
        if (-not $SkipBackup) {
            Backup-Database -Sqlite $sqlite -DbPath $Db
        }

        $statements = @(Get-SqlStatements -Path $scriptFile)
        Write-Host "[执行] 解析出 $($statements.Count) 条语句"

        $n = 0
        foreach ($statement in $statements) {
            $n++
            $result = Invoke-SqliteSql -Sqlite $sqlite -DbPath $Db -Sql $statement -TempDir $tmpDir -Name ("s{0}" -f $n)
            if ($result.ExitCode -eq 0) {
                $ok++
                continue
            }
            if ($result.Output -match 'duplicate column|already exists') {
                $skip++
                Write-Host ("  -> 跳过: " + $statement.Substring(0, [Math]::Min(56, $statement.Length)))
                continue
            }
            $fail++
            $msg = $result.Output
            Write-Host ("  -> 错误: " + $msg.Substring(0, [Math]::Min(120, $msg.Length)))
            Write-Host ("     SQL: " + $statement.Substring(0, [Math]::Min(180, $statement.Length)))
        }

        Write-Host ""
        Write-Host "[汇总] 成功 $ok / 跳过 $skip / 失败 $fail"
    } else {
        Write-Host "[模式] 仅校验（不执行、不备份）"
    }

    Write-Host ""
    Write-Host "[验证]"

    $tableCount = Invoke-SqliteScalar -Sqlite $sqlite -DbPath $Db -TempDir $tmpDir -Name "v_tables" -Sql @"
SELECT COUNT(*) FROM sqlite_master
WHERE type = 'table' AND name IN (
    'ProfileItem','ProfileExItem','SubItem','DNSItem','RoutingItem',
    'ProfileGroupItem','ServerStatItem','ComponentBundle',
    'proxy_runtime_history','FullConfigTemplateItem','SqlQuery'
)
"@
    Assert-Validation -Label "标准表数量" -Actual $tableCount -Expected "11"

    $regionCount = Invoke-SqliteScalar -Sqlite $sqlite -DbPath $Db -TempDir $tmpDir -Name "v_region" -Sql "SELECT COUNT(*) FROM pragma_table_info('ProfileItem') WHERE name = 'Region'"
    Assert-Validation -Label "ProfileItem.Region" -Actual $regionCount -Expected "1"

    $profileExCount = Invoke-SqliteScalar -Sqlite $sqlite -DbPath $Db -TempDir $tmpDir -Name "v_profileex" -Sql "SELECT COUNT(*) FROM pragma_table_info('ProfileExItem') WHERE name IN ('consecutive_failures','start_count','total_runtime_ms','crash_count')"
    Assert-Validation -Label "ProfileExItem 运行统计列" -Actual $profileExCount -Expected "4"

    $templateCols = Invoke-SqliteScalar -Sqlite $sqlite -DbPath $Db -TempDir $tmpDir -Name "v_templatecols" -Sql "SELECT COUNT(*) FROM pragma_table_info('FullConfigTemplateItem') WHERE name IN ('ParentId','BundleChain','TemplateType','BundleVersion')"
    Assert-Validation -Label "FullConfigTemplateItem 组件链列" -Actual $templateCols -Expected "4"

    $socksPortCount = Invoke-SqliteScalar -Sqlite $sqlite -DbPath $Db -TempDir $tmpDir -Name "v_socks" -Sql "SELECT COUNT(*) FROM pragma_table_info('proxy_runtime_history') WHERE name = 'socks_port'"
    Assert-Validation -Label "proxy_runtime_history.socks_port" -Actual $socksPortCount -Expected "1"

    $sqlQueryCols = Invoke-SqliteScalar -Sqlite $sqlite -DbPath $Db -TempDir $tmpDir -Name "v_sqlcols" -Sql "SELECT COUNT(*) FROM pragma_table_info('SqlQuery')"
    Assert-Validation -Label "SqlQuery 列数" -Actual $sqlQueryCols -Expected "8"

    $indexCount = Invoke-SqliteScalar -Sqlite $sqlite -DbPath $Db -TempDir $tmpDir -Name "v_indexes" -Sql "SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name IN ('idx_profile_dedup','idx_runtime_history_index','idx_sql_scope')"
    Assert-Validation -Label "关键索引数量" -Actual $indexCount -Expected "3"

    $bundleCount = Invoke-SqliteScalar -Sqlite $sqlite -DbPath $Db -TempDir $tmpDir -Name "v_bundles" -Sql "SELECT COUNT(*) FROM ComponentBundle"
    Assert-Validation -Label "ComponentBundle 行数" -Actual $bundleCount -Expected "16" -Mode "ge"

    $templateCount = Invoke-SqliteScalar -Sqlite $sqlite -DbPath $Db -TempDir $tmpDir -Name "v_templates" -Sql "SELECT COUNT(*) FROM FullConfigTemplateItem"
    Assert-Validation -Label "FullConfigTemplateItem 行数" -Actual $templateCount -Expected "8" -Mode "ge"

    $integrity = Invoke-SqliteScalar -Sqlite $sqlite -DbPath $Db -TempDir $tmpDir -Name "v_integrity" -Sql "PRAGMA integrity_check"
    Assert-Validation -Label "integrity_check" -Actual $integrity -Expected "ok"
} catch {
    Write-Host "[错误] $($_.Exception.Message)"
    $fail++
} finally {
    Remove-Item -LiteralPath $tmpDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ""
if ($fail -gt 0 -or $script:validationFailures -gt 0) {
    Write-Host "[完成] 存在失败项（执行失败 $fail / 校验失败 $($script:validationFailures)）"
    exit 1
}
Write-Host "[完成] 全部成功"
exit 0
