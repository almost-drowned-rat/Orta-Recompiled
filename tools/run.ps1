# run.ps1 - one time-limited dev run of ORTA.exe (untimed; for M1+ bring-up and screenshots).
#
#   powershell -ExecutionPolicy Bypass -File <toolkit>\tools\run_locked.ps1 -Who "O (Panzer Dragoon Orta)" -Heavy -- `
#       powershell -ExecutionPolicy Bypass -File tools\run.ps1 -Name <label> [-Seconds 120] [-Script <pad script>]
#       [-Env NAME=VALUE,...]
#
# A game run is a heavy job (maintainer, 2026-10-29): always under run_locked.ps1 -Heavy as above.
# Output: private\runs\<yyyyMMdd_HHmmss>_<label>\ with stdout.txt, stderr.txt, frametime.csv, shot_*.png and a
# fresh saves\ + data\ (never the install layout's own folders). RECOMP_NO_DIALOGS=1, windowed.
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'Interactive script; console status output only.')]
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [double]$Seconds = 120,
    [string]$Script = "",
    [string[]]$Env = @(),
    [string]$Layout,
    [string[]]$CacheDirs = @(),  # bring-up: folders created in the fresh data\cache\4947002B (the Z: cache) first
    # a saves folder COPIED into the fresh saves\ first (e.g. private\snapshots\<stamp>_level2_start\saves); never
    # the play folder (it holds KEEP_SAVES.txt - rule "never measure in the play folder"), never changed itself
    [string]$SavesFrom = ""
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "paths.ps1")   # $Repo, $Private
if (-not $Layout) { $Layout = Join-Path $Private "install\Panzer Dragoon Orta Recomp" }
$Exe = Join-Path $Layout "ORTA.exe"
if (-not (Test-Path -LiteralPath $Exe)) { throw "not built: $Exe (tools\build.ps1)" }
$Out = Join-Path $Private ("runs\{0}_{1}" -f (Get-Date -Format "yyyyMMdd_HHmmss"), $Name)
$Saves = Join-Path $Out "saves"; $Data = Join-Path $Out "data"
New-Item -ItemType Directory -Force -Path $Out, $Saves, $Data | Out-Null
if ($SavesFrom) {
    if (Test-Path -LiteralPath (Join-Path $SavesFrom "KEEP_SAVES.txt")) { throw "-SavesFrom ${SavesFrom}: a KEEP_SAVES folder (play saves) is never used for runs" }
    Copy-Item -Path (Join-Path $SavesFrom "*") -Destination $Saves -Recurse
}
# run_locked.ps1 passes an array as one comma-joined string: split it again.
foreach ($c in ($CacheDirs | ForEach-Object { $_ -split ',' } | Where-Object { $_ })) {
    New-Item -ItemType Directory -Force -Path (Join-Path $Data "cache\4947002B\$c") | Out-Null
}

$envs = [ordered]@{
    RECOMP_NO_DIALOGS  = "1"
    RECOMP_VBLANK      = "1"
    RECOMP_FRAME_TIME  = (Join-Path $Out "frametime.csv")
    RECOMP_FPS_LOG     = "1"
    RECOMP_SHOT_PREFIX = (Join-Path $Out "shot_")
    RECOMP_DISPLAY_MODE = "windowed"
}
if ($Script) { $envs["RECOMP_PAD_SCRIPT"] = "@" + (Resolve-Path -LiteralPath $Script).Path }
# A comma-joined list (run_locked / -File pass an array as one string) is split only before "NAME=", so values with
# commas (RECOMP_GL_DUMP=<prefix>,10,9000-9090) stay whole (2026-11-02).
foreach ($e in ($Env | ForEach-Object { $_ -split ',(?=[A-Za-z_][A-Za-z0-9_]*=)' } | Where-Object { $_ })) {
    $k, $v = $e -split "=", 2
    $envs[$k] = $v
}
$saved = @{}
foreach ($k in $envs.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k); [Environment]::SetEnvironmentVariable($k, $envs[$k]) }
# env.txt: every RECOMP_* / ORTA_* variable the exe will see - this script's own plus the ones a wrapper set in the
# parent environment (RECOMP_GL_DUMP, RECOMP_VK_DRAW_TRACE, ...; they were missing before, 2026-11-02).
(Get-ChildItem Env: | Where-Object { $_.Name -like "RECOMP_*" -or $_.Name -like "ORTA_*" } | Sort-Object Name |
    ForEach-Object { "$($_.Name)=$($_.Value)" }) | Out-File -Encoding utf8 (Join-Path $Out "env.txt")
try {
    $argList = @("--max-seconds", "$Seconds", "--game-dir", "`"$(Join-Path $Layout 'game')`"",
                 "--save-dir", "`"$Saves`"", "--data-dir", "`"$Data`"")
    $t = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process -FilePath $Exe -ArgumentList $argList -WorkingDirectory $Layout -PassThru -NoNewWindow `
        -RedirectStandardOutput (Join-Path $Out "stdout.txt") -RedirectStandardError (Join-Path $Out "stderr.txt")
    $null = $p.Handle
    if (-not $p.WaitForExit([int](($Seconds + 60) * 1000))) {
        Write-Host "still running $([int]$Seconds + 60) s after start - killing it"
        Stop-Process -Id $p.Id -Force
        $p.WaitForExit()
    }
    Write-Host ("exit {0} after {1:N0} s -> {2}" -f $p.ExitCode, $t.Elapsed.TotalSeconds, $Out)
} finally {
    foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k]) }
}
