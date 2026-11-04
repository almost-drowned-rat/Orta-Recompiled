# icon_pack.ps1 - the port icon files from tools\icon_draw.ps1 (assets/README.md): <Assets>\icon.ico (16, 32, 48,
# 256 px PNG entries, each drawn at its size) and <Assets>\icon_64.bmp (64x64, 32-bit with alpha, BITMAPV4
# BI_BITFIELDS, bottom-up). The intermediate PNGs go to a new temp folder that is removed afterwards.
#   powershell -ExecutionPolicy Bypass -File tools\icon_pack.ps1 -Assets assets
param([Parameter(Mandatory = $true)][string]$Assets)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$T = Join-Path ([IO.Path]::GetTempPath()) ("orta_icon_" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $T | Out-Null
try {
    $sizes = @(16, 32, 48, 256)
    $pngs = @{}
    foreach ($n in $sizes + @(64)) {
        $f = Join-Path $T "icon_$n.png"
        powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "icon_draw.ps1") -Size $n -Out $f
        $pngs[$n] = [IO.File]::ReadAllBytes($f)
    }
    # ICO: ICONDIR + ICONDIRENTRY per size + PNG data
    $ms = New-Object IO.MemoryStream; $w = New-Object IO.BinaryWriter $ms
    $w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
    $off = 6 + 16 * $sizes.Count
    foreach ($n in $sizes) {
        $b = $(if ($n -ge 256) { 0 } else { $n })
        $w.Write([byte]$b); $w.Write([byte]$b); $w.Write([byte]0); $w.Write([byte]0)
        $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]$pngs[$n].Length); $w.Write([uint32]$off)
        $off += $pngs[$n].Length
    }
    foreach ($n in $sizes) { $w.Write($pngs[$n]) }
    $w.Flush(); [IO.File]::WriteAllBytes((Join-Path $Assets "icon.ico"), $ms.ToArray()); $w.Dispose()
    # BMP V4 64x64 32-bit with alpha
    $img = [System.Drawing.Bitmap]::FromFile((Join-Path $T "icon_64.png"))
    $px = New-Object byte[] (64 * 64 * 4)
    for ($y = 0; $y -lt 64; $y++) {
        for ($x = 0; $x -lt 64; $x++) {
            $c = $img.GetPixel($x, 63 - $y); $o = ($y * 64 + $x) * 4
            $px[$o] = $c.B; $px[$o + 1] = $c.G; $px[$o + 2] = $c.R; $px[$o + 3] = $c.A
        }
    }
    $img.Dispose()
    $ms = New-Object IO.MemoryStream; $w = New-Object IO.BinaryWriter $ms
    $w.Write([byte][char]'B'); $w.Write([byte][char]'M'); $w.Write([uint32](14 + 108 + $px.Length)); $w.Write([uint32]0); $w.Write([uint32](14 + 108))
    $w.Write([uint32]108); $w.Write([int32]64); $w.Write([int32]64); $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]3)
    $w.Write([uint32]$px.Length); $w.Write([int32]2835); $w.Write([int32]2835); $w.Write([uint32]0); $w.Write([uint32]0)
    $w.Write([uint32]0x00FF0000); $w.Write([uint32]0x0000FF00); $w.Write([uint32]0x000000FF); $w.Write([uint32]4278190080)
    $w.Write([uint32]0x73524742)                      # LCS_sRGB
    for ($i = 0; $i -lt 9; $i++) { $w.Write([int32]0) }  # endpoints
    $w.Write([uint32]0); $w.Write([uint32]0); $w.Write([uint32]0)   # gamma
    $w.Write($px); $w.Flush(); [IO.File]::WriteAllBytes((Join-Path $Assets "icon_64.bmp"), $ms.ToArray()); $w.Dispose()
    "icon.ico $((Get-Item (Join-Path $Assets 'icon.ico')).Length) bytes, icon_64.bmp $((Get-Item (Join-Path $Assets 'icon_64.bmp')).Length) bytes"
} finally {
    Remove-Item -LiteralPath $T -Recurse -Force
}
