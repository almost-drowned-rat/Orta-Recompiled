# paths.ps1 - the folder layout, derived from where this repo lives (dot-source it: . "$PSScriptRoot\paths.ps1").
#
#   <root>\projects\<game>\orta_recomp[_<worktree>]   this repo and its worktrees     ($Repo, $Proj)
#   <root>\projects\<game>\private\                     the port's data: layouts, build dirs, generated C, evidence ($Private)
#   <root>\tools\xboxrecomp[_<name>]                    the toolkit and its worktrees   ($ToolsRoot)
#   <root>\tools\private\                               shared: deps, lock               ($Shared)
#   <root>\roms\xbox\                                   the maintainer's disc images     ($Roms)
#
# The paths must not contain spaces: llvm-mingw clang + Ninja + CMake use them directly (no short-path junction;
# THPS 2X tools\paths.ps1, SSD move 2026-10-29).
$Repo      = Split-Path -Parent $PSScriptRoot
$Proj      = Split-Path -Parent $Repo
$Root      = Split-Path -Parent (Split-Path -Parent $Proj)
$Private   = Join-Path $Proj "private"
$ToolsRoot = Join-Path $Root "tools"
$Shared    = Join-Path $ToolsRoot "private"
$Roms      = Join-Path $Root "roms"
# This port's toolkit: option-c pinned in its own worktree (detached at the pinned commit; never commit there, move
# it forward only together with the toolkit's owner). RC_TOOLKIT overrides it (e.g. another worktree for a test).
$Toolkit   = if ($env:RC_TOOLKIT) { $env:RC_TOOLKIT } else { Join-Path $ToolsRoot "xboxrecomp_orta" }
foreach ($p in @($Repo, $Private, $ToolsRoot, $Toolkit)) {
    if ($p -match ' ') { throw "path with a space: $p (the build needs space-free paths, see tools\paths.ps1)" }
}
