# release.ps1 - the release exe orta_launcher.exe (a local file in private\deliver\ only, never committed).
#
#   powershell -ExecutionPolicy Bypass -File tools\release.ps1 -Version <name> [-Slim]
#
# Since v1.3.0 the toolkit's tools\release\port_release.ps1 does the work (it grew out of this script, option-c step 4;
# it also carries the APU module xbox_apu.dll and the LGPL source bundle) with Orta's names from tools\release.psd1.
# This wrapper adds Orta's own check: the build is tools\build.ps1 -ForRelease (no F9 debug menu, option-c
# docs/porting_guide.md, release steps).
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'Console status output only.')]
param(
    [Parameter(Mandatory)] [string]$Version,
    [switch]$Slim
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "paths.ps1")   # $Repo, $Private, $Toolkit
$Build = Join-Path $Private ("build\" + (Split-Path -Leaf $Repo))
$cache = Get-Content (Join-Path $Build "CMakeCache.txt") -ErrorAction SilentlyContinue
if (-not ($cache -match '^XR_DEBUG_PANEL:BOOL=OFF$')) { throw "build in $Build has the F9 debug menu - run tools\build.ps1 -ForRelease" }
$a = @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $Toolkit "tools\release\port_release.ps1"),
       "-Config", (Join-Path $PSScriptRoot "release.psd1"), "-Version", $Version)
if ($Slim) { $a += "-Slim" }
Push-Location $Repo                       # release.psd1's relative paths
try { & powershell @a; $rc = $LASTEXITCODE } finally { Pop-Location }
exit $rc
