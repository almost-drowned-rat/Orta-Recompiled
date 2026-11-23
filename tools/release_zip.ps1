# release_zip.ps1 - the player's release zip from the exe tools\release.ps1 built.
#
#   powershell -File tools\release_zip.ps1 -Version <v> -Notes <release_notes.md> [-Folder <name>]
#
# Since v1.3.0 the toolkit's tools\release\port_release_zip.ps1 does the work (it grew out of this script) with Orta's
# names from tools\release.psd1: a new folder private\deliver\v<v>\ with zip\ (orta_launcher.exe, README.txt from
# tools\release_readme.txt, LICENSES\, tools\bundle\ with the pinned llvm-mingw zip), Orta-Recompiled-v<v>-win64.zip,
# SHA256SUMS.txt, release_notes.md. The compiler ships inside the zip; the first start needs no internet.
param(
    [Parameter(Mandatory)] [string]$Version,
    [Parameter(Mandatory)] [string]$Notes,
    [string]$Folder
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "paths.ps1")   # $Toolkit
$a = @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $Toolkit "tools\release\port_release_zip.ps1"),
       "-Config", (Join-Path $PSScriptRoot "release.psd1"), "-Version", $Version, "-Notes", $Notes)
if ($Folder) { $a += @("-Folder", $Folder) }
Push-Location (Split-Path -Parent $PSScriptRoot)   # release.psd1's relative paths
try { & powershell @a; $rc = $LASTEXITCODE } finally { Pop-Location }
exit $rc
