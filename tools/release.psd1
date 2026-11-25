# release.psd1 - Orta's names for the toolkit's release scripts (option-c tools/release/port_release.ps1,
# port_release_zip.ps1; their headers list every key). Used through tools\release.ps1 / tools\release_zip.ps1, which
# run with the repository as the current folder (relative paths below are relative to it).
# Release layout (maintainer 2026-11-21, from v1.3.0): only orta_launcher.exe on top, the game data\orta.exe (named in
# launcher/main.c); GameExe here is the DEV build's output whose link line the payload copies (ORTA.exe stays).
@{
    Who           = "Panzer Dragoon Orta release"
    Title         = "Panzer Dragoon Orta Recomp"
    ExeName       = "orta_launcher.exe"
    Target        = "orta_recomp"
    GameExe       = "ORTA.exe"
    GameName      = "ORTA"
    GenMatch      = '[\\/]orta[\\/]gen[\\/][^\\/]+$'
    Rc            = "assets\orta.rc"
    RcFiles       = @("assets\icon.ico")
    Config        = @("config\game.toml", "config\variants\*.toml")
    Licence       = "LICENSE-Orta-recomp-GPL-3.0.txt"
    PublicRepo    = "Orta-Recompiled"
    Data          = @("assets\pipelist_orta_pal.bin")
    ZipPrefix     = "Orta-Recompiled"
    Readme        = "tools\release_readme.txt"
    # RecompLauncher's manifest, at the top of the zip next to the launcher (maintainer 2026-11-24: in every release)
    RecompLauncher = "recomplauncher.json"
    # the LGPL source bundle (licence option C) from a clone of the public toolkit xboxrecomp-option-c at the release
    # tag (beside the toolkit worktree, see tools\paths.ps1's layout)
    PublicToolkit = "..\..\..\tools\xboxrecomp_option_c_public"
    PublicTag     = "v1.3.2"
}
