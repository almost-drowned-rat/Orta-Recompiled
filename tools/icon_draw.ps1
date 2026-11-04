# icon_draw.ps1 - draws the Orta port icon (an original stylized wing: three feathers, a ring behind it, on a dark
# rounded square; no game logo, cover art, lettering or branding) at any size into a 32-bit PNG (assets/README.md).
#   powershell -ExecutionPolicy Bypass -File tools\icon_draw.ps1 -Size 256 -Out <png>
param([int]$Size = 256, [Parameter(Mandatory = $true)][string]$Out)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$s = $Size / 256.0
$bmp = New-Object System.Drawing.Bitmap $Size, $Size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.Clear([System.Drawing.Color]::Transparent)
function RoundRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $p.AddArc($x, $y, 2 * $r, 2 * $r, 180, 90); $p.AddArc($x + $w - 2 * $r, $y, 2 * $r, 2 * $r, 270, 90)
    $p.AddArc($x + $w - 2 * $r, $y + $h - 2 * $r, 2 * $r, 2 * $r, 0, 90); $p.AddArc($x, $y + $h - 2 * $r, 2 * $r, 2 * $r, 90, 90)
    $p.CloseFigure(); return $p
}
# background: dark blue-grey rounded square with a soft vertical gradient
$bg = RoundRect (8 * $s) (8 * $s) (240 * $s) (240 * $s) (48 * $s)
$bgBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.PointF 0, (8 * $s)), (New-Object System.Drawing.PointF 0, (248 * $s)), ([System.Drawing.Color]::FromArgb(255, 24, 38, 56)), ([System.Drawing.Color]::FromArgb(255, 10, 16, 26))
$g.FillPath($bgBrush, $bg)
# ring behind the wing
$ringPen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(150, 90, 200, 220)), ([float](10 * $s))
$g.DrawEllipse($ringPen, 54 * $s, 54 * $s, 148 * $s, 148 * $s)
# three feathers fanning from the lower left (root at 70,190) to the upper right
$root = New-Object System.Drawing.PointF (70 * $s), (190 * $s)
$feathers = @(
    @{ tip = @(212, 70);  w = 34; c1 = @(120, 230, 240); c2 = @(40, 120, 190) },
    @{ tip = @(196, 118); w = 30; c1 = @(100, 210, 230); c2 = @(35, 105, 175) },
    @{ tip = @(170, 160); w = 26; c1 = @(80, 190, 220);  c2 = @(30, 90, 160) }
)
foreach ($f in $feathers) {
    $tx = $f.tip[0] * $s; $ty = $f.tip[1] * $s; $w = $f.w * $s
    $dx = $tx - $root.X; $dy = $ty - $root.Y; $len = [Math]::Sqrt($dx * $dx + $dy * $dy); $nx = -$dy / $len; $ny = $dx / $len
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $p.AddBezier($root, (New-Object System.Drawing.PointF ($root.X + $dx * 0.35 + $nx * $w), ($root.Y + $dy * 0.35 + $ny * $w)),
        (New-Object System.Drawing.PointF ($root.X + $dx * 0.8 + $nx * $w * 0.7), ($root.Y + $dy * 0.8 + $ny * $w * 0.7)), (New-Object System.Drawing.PointF $tx, $ty))
    $p.AddBezier((New-Object System.Drawing.PointF $tx, $ty), (New-Object System.Drawing.PointF ($root.X + $dx * 0.75 - $nx * $w * 0.25), ($root.Y + $dy * 0.75 - $ny * $w * 0.25)),
        (New-Object System.Drawing.PointF ($root.X + $dx * 0.3 - $nx * $w * 0.2), ($root.Y + $dy * 0.3 - $ny * $w * 0.2)), $root)
    $p.CloseFigure()
    $br = New-Object System.Drawing.Drawing2D.LinearGradientBrush $root, (New-Object System.Drawing.PointF $tx, $ty), ([System.Drawing.Color]::FromArgb(255, $f.c2[0], $f.c2[1], $f.c2[2])), ([System.Drawing.Color]::FromArgb(255, $f.c1[0], $f.c1[1], $f.c1[2]))
    $g.FillPath($br, $p)
    $edge = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(200, 220, 250, 255)), ([float]([Math]::Max(1.0, 2.5 * $s)))
    $g.DrawPath($edge, $p)
}
# small body arc at the root
$bodyPen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 230, 245, 250)), ([float]([Math]::Max(1.5, 12 * $s)))
$bodyPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round; $bodyPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
$g.DrawBezier($bodyPen, 40 * $s, 214 * $s, 58 * $s, 200 * $s, 74 * $s, 196 * $s, 98 * $s, 200 * $s)
$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
