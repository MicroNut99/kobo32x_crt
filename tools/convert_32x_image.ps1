<#
convert_32x_image.ps1  -  make an IMAGE.RAW for the 32X CD loader          (V2)

Output format (what the loader expects):
  * 320 x 200 pixels (default), 2 bytes per pixel  ->  128000 bytes
  * each pixel = 16-bit word  1BBBBBGGGGGRRRRR
        bit 15      = 1 (opaque / 32X priority bit)
        bits 14-10  = blue, bits 9-5 = green, bits 4-0 = red   (5 bit each)
  * big-endian (high byte first), rows top to bottom, no header

The 32X frame buffer holds 65,280 pixel words after its line table, so the picture
can be at most 320 x 204.  The current SH2 program draws 200 lines, so keep
-Height 200 unless you also change IMG_H in sh2_main.c.

Usage
  .\convert_32x_image.ps1 -InputFile picture.png
  .\convert_32x_image.ps1 -InputFile picture.jpg -OutputFile IMAGE.RAW -Mode Fit -Dither FS
  .\convert_32x_image.ps1 -Decode -InputFile IMAGE.RAW -OutputFile check.png     (preview of a .raw)

  -Mode    Stretch  scale to exactly 320x200 (default; ignores aspect ratio)
           Fit      keep aspect ratio, black bars top/bottom or left/right
           Fill     keep aspect ratio, crop the overflow (centre)
  -Dither  None | FS (Floyd-Steinberg)                          (default None)
#>
param(
    [Parameter(Mandatory = $true)][string]$InputFile,
    [string]$OutputFile = "",
    [int]$Height = 200,
    [ValidateSet("Stretch", "Fit", "Fill")][string]$Mode = "Stretch",
    [ValidateSet("None", "FS")][string]$Dither = "None",
    [switch]$Decode
)

Add-Type -AssemblyName System.Drawing
$W = 320

# 0..255 -> 0..31, rounded
function Q5([double]$v) {
    if ($v -lt 0) { $v = 0 } elseif ($v -gt 255) { $v = 255 }
    $i = [int][math]::Round($v)
    return [int][math]::Floor(($i * 31 + 127) / 255)
}

if (-not (Test-Path $InputFile)) {
    Write-Host "Error: cannot find $InputFile" -ForegroundColor Red
    exit 1
}
$inPath = (Resolve-Path $InputFile).Path

# ---------------------------------------------------------------- decode a .raw to a PNG preview
if ($Decode) {
    if ($OutputFile -eq "") { $OutputFile = "preview.png" }
    $d = [System.IO.File]::ReadAllBytes($inPath)
    $n = [int]($d.Length / 2)
    $h = [int][math]::Floor($n / $W)
    $bmp = New-Object System.Drawing.Bitmap($W, $h)
    for ($i = 0; $i -lt ($W * $h); $i++) {
        $v = ($d[$i * 2] * 256) + $d[$i * 2 + 1]
        $r = $v -band 31
        $g = ($v -shr 5) -band 31
        $b = ($v -shr 10) -band 31
        $r8 = ($r -shl 3) -bor ($r -shr 2)
        $g8 = ($g -shl 3) -bor ($g -shr 2)
        $b8 = ($b -shl 3) -bor ($b -shr 2)
        $bmp.SetPixel($i % $W, [int][math]::Floor($i / $W), [System.Drawing.Color]::FromArgb($r8, $g8, $b8))
    }
    $bmp.Save((Join-Path (Get-Location) $OutputFile), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "Decoded $($d.Length) bytes -> ${W}x$h  $OutputFile"
    exit 0
}

# ---------------------------------------------------------------- image -> .raw
if ($OutputFile -eq "") { $OutputFile = "IMAGE.RAW" }
if ($Height -lt 1 -or $Height -gt 204) {
    Write-Host "Error: -Height must be 1..204 (the frame buffer holds at most 320x204 pixels)" -ForegroundColor Red
    exit 1
}
if ($Height -ne 200) {
    Write-Host "Note: the current SH2 program draws 200 lines (IMG_H in sh2_main.c)" -ForegroundColor Yellow
}

$img = [System.Drawing.Image]::FromFile($inPath)
$sw = $img.Width
$sh = $img.Height

$dest = New-Object System.Drawing.Rectangle(0, 0, $W, $Height)
$srcX = 0; $srcY = 0; $srcW = $sw; $srcH = $sh

if ($Mode -eq "Fit") {
    $s = [math]::Min($W / $sw, $Height / $sh)
    $nw = [int][math]::Round($sw * $s)
    $nh = [int][math]::Round($sh * $s)
    $dest = New-Object System.Drawing.Rectangle([int](($W - $nw) / 2), [int](($Height - $nh) / 2), $nw, $nh)
}
elseif ($Mode -eq "Fill") {
    $s = [math]::Max($W / $sw, $Height / $sh)
    $srcW = [int][math]::Round($W / $s)
    $srcH = [int][math]::Round($Height / $s)
    $srcX = [int](($sw - $srcW) / 2)
    $srcY = [int](($sh - $srcH) / 2)
}

$bmp = New-Object System.Drawing.Bitmap($W, $Height)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::Black)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$g.DrawImage($img, $dest, $srcX, $srcY, $srcW, $srcH, [System.Drawing.GraphicsUnit]::Pixel)
$g.Dispose()

$count = $W * $Height
$bytes = New-Object byte[] ($count * 2)

if ($Dither -eq "FS") {
    $fr = New-Object 'double[]' $count
    $fg = New-Object 'double[]' $count
    $fb = New-Object 'double[]' $count
    for ($y = 0; $y -lt $Height; $y++) {
        for ($x = 0; $x -lt $W; $x++) {
            $p = $bmp.GetPixel($x, $y)
            $k = $y * $W + $x
            $fr[$k] = $p.R; $fg[$k] = $p.G; $fb[$k] = $p.B
        }
    }
    for ($y = 0; $y -lt $Height; $y++) {
        for ($x = 0; $x -lt $W; $x++) {
            $k = $y * $W + $x
            $qr = Q5 $fr[$k]; $qg = Q5 $fg[$k]; $qb = Q5 $fb[$k]
            $er = $fr[$k] - ($qr * 255 / 31.0)
            $eg = $fg[$k] - ($qg * 255 / 31.0)
            $eb = $fb[$k] - ($qb * 255 / 31.0)
            $taps = @( @(1, 0, (7 / 16.0)), @(-1, 1, (3 / 16.0)), @(0, 1, (5 / 16.0)), @(1, 1, (1 / 16.0)) )   # brackets matter: comma binds tighter than /
            foreach ($t in $taps) {
                $nx = $x + $t[0]; $ny = $y + $t[1]
                if ($nx -ge 0 -and $nx -lt $W -and $ny -lt $Height) {
                    $nk = $ny * $W + $nx
                    $fr[$nk] += $er * $t[2]
                    $fg[$nk] += $eg * $t[2]
                    $fb[$nk] += $eb * $t[2]
                }
            }
            $word = 0x8000 -bor ($qb -shl 10) -bor ($qg -shl 5) -bor $qr
            $bytes[$k * 2] = ($word -shr 8) -band 0xFF
            $bytes[$k * 2 + 1] = $word -band 0xFF
        }
    }
}
else {
    for ($y = 0; $y -lt $Height; $y++) {
        for ($x = 0; $x -lt $W; $x++) {
            $p = $bmp.GetPixel($x, $y)
            $k = $y * $W + $x
            $word = 0x8000 -bor ((Q5 $p.B) -shl 10) -bor ((Q5 $p.G) -shl 5) -bor (Q5 $p.R)
            $bytes[$k * 2] = ($word -shr 8) -band 0xFF
            $bytes[$k * 2 + 1] = $word -band 0xFF
        }
    }
}

[System.IO.File]::WriteAllBytes((Join-Path (Get-Location) $OutputFile), $bytes)
$img.Dispose()
$bmp.Dispose()
Write-Host "Wrote $OutputFile : $($bytes.Length) bytes (${W}x$Height, 5:5:5 big-endian, bit 15 set)"
