# regress.ps1 - Orta regression before every merge (maintainer rule 2026-10-28; model THPS 2X tools\regress.ps1).
# Replaces the option-c template version (it needs tools\run_dir.ps1 / lock.ps1 / shot_compare.ps1 copies this
# repo does not have yet): every game run goes through tools\run.ps1 (a new run folder per part, never reused),
# the caller holds the lock (run_locked.ps1 -Heavy).
#
#   powershell -ExecutionPolicy Bypass -File <toolkit>\tools\run_locked.ps1 -Who "O (Panzer Dragoon Orta)" -Heavy -- `
#       powershell -ExecutionPolicy Bypass -File tools\regress.ps1
#
# Parts (each a fresh run folder via tools\run.ps1: fresh saves\ + data\, RECOMP_NO_DIALOGS=1, windowed):
#   menu_tour    title -> START -> main menu -> new game -> story intro      (tests\input\main_menu.txt)
#   options      main menu -> OPTIONEN, down through the list               (tests\input\options_menu.txt)
#   pandora      main menu -> PANDORAS BÜCHSE                                (tests\input\pandoras_box.txt)
#   first_level  new game, cut scenes skipped -> episode 1 gameplay        (tests\input\first_level.txt)
#   screenshot   tests\input\shots_tour.txt shots -> tools\shot_compare.ps1 against private\reference\menu_tour
#                (SKIP with the candidate folder until the maintainer OKs a first set)
#   storage      tests\outside_writes.ps1 (nothing written outside)        run when the script exists, else SKIP
# A part PASSES when the exe exits 0 at --max-seconds, the log has no [CRASH] / [NULLPTR], no unresolved ICALL
# ("Failed to resolve"), and the frame-time CSV reached the part's minimum frame count. One summary line per part
# (PASS / FAIL / SKIP + evidence path) goes to the console and to private\regress\<stamp>\summary.txt.
# SKIP is never counted as passed (CLAUDE.md "Resources").
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'Console summary.')]
param([string[]]$Only = @())
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "paths.ps1")   # $Repo, $Private
$Stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$Out = Join-Path $Private "regress\$Stamp"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Summary = Join-Path $Out "summary.txt"

$parts = @(
    @{ name = "menu_tour";   script = "main_menu.txt";    seconds = 100; min_frames = 5000 },
    @{ name = "options";     script = "options_menu.txt"; seconds = 90;  min_frames = 4500 },
    @{ name = "pandora";     script = "pandoras_box.txt"; seconds = 90;  min_frames = 4500 },
    # the maintainer's recorded way into episode 1, then no input, infinite health (maintainer 2026-11-08: no START /
    # A spam, no game over - comparable runs); was first_level.txt
    @{ name = "first_level"; script = "level1_idle_shots.txt"; seconds = 200; min_frames = 10000; env = "ORTA_CHEAT_HEALTH=1" }
)
$lines = @()
function Say([string]$s) { Write-Host $s; $script:lines += $s }

foreach ($p in $parts) {
    if ($Only.Count -and $Only -notcontains $p.name) { continue }
    $before = @(Get-ChildItem (Join-Path $Private "runs") -Directory -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
    $envArg = @()
    if ($p.env) { $envArg = @("-Env", $p.env) }
    powershell -ExecutionPolicy Bypass -File (Join-Path $Repo "tools\run.ps1") -Name "regress_$($p.name)" `
        -Seconds $p.seconds -Script (Join-Path $Repo "tests\input\$($p.script)") @envArg | Out-Null
    $run = Get-ChildItem (Join-Path $Private "runs") -Directory | Where-Object { $before -notcontains $_.FullName -and
        $_.Name -like "*_regress_$($p.name)" } | Sort-Object Name | Select-Object -Last 1
    if (-not $run) { Say ("{0,-12} FAIL  no run folder" -f $p.name); continue }
    $log = Join-Path $run.FullName "data\orta.log"
    $why = @()
    $ft = Join-Path $run.FullName "frametime.csv"
    $frames = 0
    if (Test-Path -LiteralPath $ft) { $frames = (Get-Content -LiteralPath $ft | Measure-Object -Line).Lines - 1 }
    if ($frames -lt $p.min_frames) { $why += "frames $frames < $($p.min_frames)" }
    if (Test-Path -LiteralPath $log) {
        if (Select-String -LiteralPath $log -Pattern '\[CRASH\]|\[NULLPTR\]|FATAL|access violation|unhandled exception' -Quiet) { $why += "crash" }
        if (Select-String -LiteralPath $log -Pattern 'Failed to resolve|is not code' -Quiet) { $why += "unresolved ICALL" }
        # [UNIMPL] lines other than the known ones (XPP's cli/sti pair and D3D's wbinvd: no-ops on the host, every
        # run since M3) fail the part.
        $unimpl = @(Select-String -LiteralPath $log -Pattern '\[UNIMPL\]' | Where-Object {
            $_.Line -notmatch 'at 0x(00306F4F|00306F60|002B1080)\b' })
        if ($unimpl.Count) { $why += "new [UNIMPL] ($($unimpl.Count))" }
        if (-not (Select-String -LiteralPath $log -Pattern 'max-seconds .* reached' -Quiet)) { $why += "no clean exit" }
    } else { $why += "no log" }
    if ($why.Count) { Say ("{0,-12} FAIL  {1} - {2}" -f $p.name, ($why -join ", "), $run.FullName) }
    else { Say ("{0,-12} PASS  {1} frames - {2}" -f $p.name, $frames, $run.FullName) }
}

if (-not $Only.Count -or $Only -contains "screenshot") {
    # tests\input\shots_tour.txt: shots at settled points (run.ps1 sets RECOMP_SHOT_PREFIX = <run>\shot_), PNG via
    # tools\dump_png.ps1, compared by tools\shot_compare.ps1 against private\reference\menu_tour. Without a reference
    # the part is SKIP and names the run folder as the candidate set (a first set needs the maintainer's OK:
    # shot_compare.ps1 -Run <folder> -Update -MaintainerOk).
    $ref = Join-Path $Private "reference\menu_tour"
    $before = @(Get-ChildItem (Join-Path $Private "runs") -Directory -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
    powershell -ExecutionPolicy Bypass -File (Join-Path $Repo "tools\run.ps1") -Name "regress_shots" -Seconds 105 `
        -Script (Join-Path $Repo "tests\input\shots_tour.txt") | Out-Null
    $run = Get-ChildItem (Join-Path $Private "runs") -Directory | Where-Object { $before -notcontains $_.FullName -and
        $_.Name -like "*_regress_shots" } | Sort-Object Name | Select-Object -Last 1
    $want = @(Select-String -Path (Join-Path $Repo "tests\input\shots_tour.txt") -Pattern '^\d+:shot=').Count
    if (-not $run) { Say ("{0,-12} FAIL  no run folder" -f "screenshot") }
    else {
        & (Join-Path $Repo "tools\dump_png.ps1") $run.FullName | Out-Null
        $got = @(Get-ChildItem $run.FullName -Filter "shot_*.png").Count
        if ($got -lt $want) { Say ("{0,-12} FAIL  {1} of {2} shots - {3}" -f "screenshot", $got, $want, $run.FullName) }
        elseif (-not (Test-Path (Join-Path $ref "*.png"))) {
            Say ("{0,-12} SKIP  no reference set yet - {1} candidate shots for the maintainer: {2}" -f "screenshot", $got, $run.FullName)
        } else {
            $cmp = @(& (Join-Path $Repo "tools\shot_compare.ps1") -Run $run.FullName -Ref $ref 2>&1)
            $cmp | Set-Content (Join-Path $Out "screenshot_compare.txt")
            $res = if ($LASTEXITCODE -eq 0) { "PASS" } else { "FAIL" }
            Say ("{0,-12} {1}  {2} - {3}" -f "screenshot", $res, $cmp[-1], (Join-Path $Out "screenshot_compare.txt"))
        }
    }
}
if (-not $Only.Count -or $Only -contains "storage") {
    $ow = Join-Path $Repo "tests\outside_writes.ps1"
    if (Test-Path -LiteralPath $ow) {
        $lay = Join-Path $Private "install\Panzer Dragoon Orta Recomp"
        & $ow -Layout $lay -Exe "ORTA.exe" -Sec 60 -Script (Join-Path $Repo "tests\input\main_menu.txt") *> (Join-Path $Out "outside_writes.txt")
        if ($LASTEXITCODE -eq 0) { Say ("{0,-12} PASS  outside_writes - {1}" -f "storage", (Join-Path $Out "outside_writes.txt")) }
        else { Say ("{0,-12} FAIL  outside_writes exit {1} - {2}" -f "storage", $LASTEXITCODE, (Join-Path $Out "outside_writes.txt")) }
    } else {
        Say ("{0,-12} SKIP  tests\outside_writes.ps1 not there" -f "storage")
    }
}
$lines | Out-File -Encoding utf8 $Summary
Write-Host "summary: $Summary"
if ($lines | Where-Object { $_ -match ' FAIL ' }) { exit 1 }
exit 0
