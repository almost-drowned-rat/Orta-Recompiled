# lift.ps1 - Panzer Dragoon Orta: disc image -> generated C (the toolkit's lifting pipeline).
#
#   powershell -ExecutionPolicy Bypass -File <toolkit>\tools\run_locked.ps1 -Who "O (Panzer Dragoon Orta)" -Heavy -- `
#       powershell -ExecutionPolicy Bypass -File tools\lift.ps1 [-Iso <path>] [-Layout <dir>]
#
# A lift is a heavy job (maintainer, 2026-10-29): run it under run_locked.ps1 -Heavy as above.
# Steps:
#   1. game files: extract the disc into <Layout>\game\ once (read-only access to the ISO)
#   2. xbe_parser -> disasm -> func_id -> abi_analysis -> recomp, all settings from config\game.toml
#      (toolkit tools/gameconfig.py): the common part plus the variant file whose SHA-256 matches default.xbe
#      (seeds, overrides). An XBE that matches no variant is refused by the tools.
# Everything generated goes to private\ (never committed), one folder per version (toolkit option-c variant
# mechanism: every version in one dev exe):
#   private\orta\pipeline\<variant>\  intermediate JSON
#   private\orta\gen\<variant>\       generated C + headers (CMake: RECOMP_GEN_DIR)
# A full rebuild is needed after every lift (CLAUDE.md: only after lifter changes).

[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'Interactive script; console status output only.')]
param(
    [string]$Iso,                  # default: <root>\roms\xbox\Panzer Dragoon Orta (Europe).iso
    [string]$Layout,
    [string]$Variant = "pal",
    [string]$TraceFunctions = "",  # bring-up: JSON list of addresses for entry/exit traces (tools.recomp)
    [string]$Gen = ""              # other gen dir (e.g. a trace lift for a call census); default private\orta\gen\<variant>
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "paths.ps1")   # $Repo, $Private, $ToolsRoot, $Toolkit, $Roms
if (-not $Iso) { $Iso = Join-Path $Roms "xbox\Panzer Dragoon Orta (Europe).iso" }
$LiftDir  = Join-Path $Private "orta"
if (-not $Layout) { $Layout = Join-Path $Private "install\Panzer Dragoon Orta Recomp" }
$Game     = Join-Path $Layout "game"
$Xbe      = Join-Path $Game "default.xbe"
$Pipe     = Join-Path $LiftDir "pipeline\$Variant"
if (-not $Gen) { $Gen = Join-Path $LiftDir "gen\$Variant" }
$Config   = Join-Path $Repo "config\game.toml"

function Invoke-Py([string]$what, [string[]]$a) {
    Write-Host "== $what"
    $t = [Diagnostics.Stopwatch]::StartNew()
    # The tools print status lines on stderr; under "Stop" PowerShell 5.1 turns those into terminating errors once
    # the caller redirects the streams (lift #2 died on "game config ... 678 seeds"). Plain text, exit code decides.
    $ErrorActionPreference = "Continue"
    & py -3 @a 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    if ($code -ne 0) { throw "$what failed (exit $code)" }
    Write-Host ("   {0:N0} s" -f $t.Elapsed.TotalSeconds)
}

Push-Location $Toolkit
try {
    if (-not (Test-Path -LiteralPath $Xbe)) {
        if (-not (Test-Path -LiteralPath $Iso)) { throw "Disc image not found: $Iso" }
        New-Item -ItemType Directory -Force -Path $Game | Out-Null
        Invoke-Py "extract disc -> $Game" @("-m", "tools.xiso", "unpack", $Iso, "-o", $Game)
    }

    $dis = Join-Path $Pipe "disasm"; $fid = Join-Path $Pipe "func_id"
    $abi = Join-Path $Pipe "abi";    $rec = Join-Path $Pipe "recomp"
    foreach ($d in @($dis, $fid, $abi, $rec, $Gen)) { New-Item -ItemType Directory -Force -Path $d | Out-Null }
    $analysis = Join-Path $Pipe "xbe.json"

    Invoke-Py "xbe_parser" @("-m", "tools.xbe_parser", $Xbe, "--json", $analysis, "--quiet")
    # Which sections are code: config\game.toml [disasm] text_only + extra_sections (not APPDATA / DOLBY).
    Invoke-Py "disasm" @("-m", "tools.disasm", $Xbe, "-o", $dis, "--analysis-json", $analysis, "--force",
        "--game-config", $Config)
    Invoke-Py "func_id" @("-m", "tools.func_id", $Xbe, "--functions", (Join-Path $dis "functions.json"),
        "--strings", (Join-Path $dis "strings.json"), "--xrefs", (Join-Path $dis "xrefs.json"), "-o", $fid)
    Invoke-Py "abi_analysis" @("-m", "tools.abi_analysis", $Xbe, "--disasm-dir", $dis, "--func-id-dir", $fid,
        "--output-dir", $abi)

    # No partition layout yet (config\variants\<v>.partitions.json): wipe and regenerate everything.
    Get-ChildItem $Gen -File -ErrorAction SilentlyContinue | Remove-Item
    $trace = @(); if ($TraceFunctions) { $trace = @("--trace-functions", $TraceFunctions) }
    Invoke-Py "recomp" (@("-m", "tools.recomp", $Xbe, "--all", "--game-config", $Config, "--game-name", "ORTA",
        "--gen-dir", $Gen, "--disasm-dir", $dis, "--func-id-dir", $fid, "--abi-dir", $abi, "-o", $rec) + $trace)
} finally { Pop-Location }

$n = (Get-ChildItem $Gen -Filter "*.c").Count
Write-Host "Done: $n C files in $Gen"
