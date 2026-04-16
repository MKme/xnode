param(
    [string]$OutputPng = "images/ontario_basic_256px.png",
    [string]$OutputC = "src/utils/osm_map/images/osm_no_data_256px.c"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

function New-PointF {
    param([double]$X, [double]$Y)
    return New-Object System.Drawing.PointF([float]$X, [float]$Y)
}

function Convert-LonLatToPoint {
    param(
        [double]$Lon,
        [double]$Lat,
        [double]$MinLon,
        [double]$MaxLon,
        [double]$MinLat,
        [double]$MaxLat,
        [int]$Width,
        [int]$Height,
        [int]$Padding
    )

    $usableWidth = $Width - ($Padding * 2)
    $usableHeight = $Height - ($Padding * 2)
    $x = $Padding + (($Lon - $MinLon) / ($MaxLon - $MinLon)) * $usableWidth
    $y = $Padding + (($MaxLat - $Lat) / ($MaxLat - $MinLat)) * $usableHeight
    return New-PointF $x $y
}

function Convert-LonLatArrayToPoints {
    param(
        [object[]]$Pairs,
        [double]$MinLon,
        [double]$MaxLon,
        [double]$MinLat,
        [double]$MaxLat,
        [int]$Width,
        [int]$Height,
        [int]$Padding
    )

    $points = New-Object System.Collections.Generic.List[System.Drawing.PointF]
    foreach ($pair in $Pairs) {
        $points.Add((Convert-LonLatToPoint -Lon $pair[0] -Lat $pair[1] -MinLon $MinLon -MaxLon $MaxLon -MinLat $MinLat -MaxLat $MaxLat -Width $Width -Height $Height -Padding $Padding))
    }
    return $points.ToArray()
}

function Write-LvglRawAlphaC {
    param(
        [string]$PngPath,
        [string]$CPath,
        [string]$SymbolName
    )

    $bytes = [System.IO.File]::ReadAllBytes($PngPath)
    $png = [System.Drawing.Image]::FromFile($PngPath)
    try {
        $lines = New-Object System.Collections.Generic.List[string]
        $lines.Add('#include "lvgl.h"')
        $lines.Add('')
        $lines.Add('#ifndef LV_ATTRIBUTE_MEM_ALIGN')
        $lines.Add('#define LV_ATTRIBUTE_MEM_ALIGN')
        $lines.Add('#endif')
        $lines.Add('')
        $macroName = ("LV_ATTRIBUTE_IMG_" + $SymbolName.ToUpper())
        $lines.Add("#ifndef $macroName")
        $lines.Add("#define $macroName")
        $lines.Add('#endif')
        $lines.Add('')
        $lines.Add("const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST $macroName uint8_t ${SymbolName}_map[] = {")

        for ($i = 0; $i -lt $bytes.Length; $i += 16) {
            $slice = $bytes[$i..([Math]::Min($i + 15, $bytes.Length - 1))]
            $hex = ($slice | ForEach-Object { '0x{0:x2}' -f $_ }) -join ', '
            $suffix = if ($i + 16 -lt $bytes.Length) { ',' } else { '' }
            $lines.Add("  $hex$suffix")
        }

        $lines.Add('};')
        $lines.Add('')
        $lines.Add("const lv_img_dsc_t $SymbolName = {")
        $lines.Add('  .header.always_zero = 0,')
        $lines.Add("  .header.w = $($png.Width),")
        $lines.Add("  .header.h = $($png.Height),")
        $lines.Add("  .data_size = $($bytes.Length),")
        $lines.Add('  .header.cf = LV_IMG_CF_RAW_ALPHA,')
        $lines.Add("  .data = ${SymbolName}_map,")
        $lines.Add('};')

        $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllLines($CPath, $lines, $utf8NoBom)
    }
    finally {
        $png.Dispose()
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedPng = Join-Path $repoRoot $OutputPng
$resolvedC = Join-Path $repoRoot $OutputC

[System.IO.Directory]::CreateDirectory((Split-Path -Parent $resolvedPng)) | Out-Null
[System.IO.Directory]::CreateDirectory((Split-Path -Parent $resolvedC)) | Out-Null

$width = 256
$height = 256
$padding = 14

$minLon = -95.5
$maxLon = -73.0
$minLat = 41.0
$maxLat = 57.5

$bmp = New-Object System.Drawing.Bitmap($width, $height)
$gfx = [System.Drawing.Graphics]::FromImage($bmp)

try {
    $gfx.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $gfx.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $gfx.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

    $water = [System.Drawing.Color]::FromArgb(255, 201, 226, 241)
    $land = [System.Drawing.Color]::FromArgb(255, 232, 236, 221)
    $ontario = [System.Drawing.Color]::FromArgb(255, 196, 217, 170)
    $ontarioBorder = [System.Drawing.Color]::FromArgb(255, 74, 97, 70)
    $lake = [System.Drawing.Color]::FromArgb(255, 127, 177, 214)
    $grid = [System.Drawing.Color]::FromArgb(255, 173, 193, 209)
    $text = [System.Drawing.Color]::FromArgb(255, 34, 52, 66)
    $city = [System.Drawing.Color]::FromArgb(255, 170, 70, 52)

    $gfx.Clear($water)

    $gridPen = New-Object System.Drawing.Pen($grid, 1)
    $gridFont = New-Object System.Drawing.Font("Segoe UI", 7, [System.Drawing.FontStyle]::Regular)
    $labelBrush = New-Object System.Drawing.SolidBrush($text)

    foreach ($lat in @(42, 46, 50, 54)) {
        $p1 = Convert-LonLatToPoint -Lon $minLon -Lat $lat -MinLon $minLon -MaxLon $maxLon -MinLat $minLat -MaxLat $maxLat -Width $width -Height $height -Padding $padding
        $p2 = Convert-LonLatToPoint -Lon $maxLon -Lat $lat -MinLon $minLon -MaxLon $maxLon -MinLat $minLat -MaxLat $maxLat -Width $width -Height $height -Padding $padding
        $gfx.DrawLine($gridPen, $p1, $p2)
        $gfx.DrawString("${lat}N", $gridFont, $labelBrush, 2, $p1.Y - 6)
    }
    foreach ($lon in @(-90, -85, -80, -75)) {
        $p1 = Convert-LonLatToPoint -Lon $lon -Lat $minLat -MinLon $minLon -MaxLon $maxLon -MinLat $minLat -MaxLat $maxLat -Width $width -Height $height -Padding $padding
        $p2 = Convert-LonLatToPoint -Lon $lon -Lat $maxLat -MinLon $minLon -MaxLon $maxLon -MinLat $minLat -MaxLat $maxLat -Width $width -Height $height -Padding $padding
        $gfx.DrawLine($gridPen, $p1, $p2)
        $gfx.DrawString("{0}W" -f [math]::Abs($lon), $gridFont, $labelBrush, $p1.X - 10, $height - 12)
    }

    $landBrush = New-Object System.Drawing.SolidBrush($land)
    $ontarioBrush = New-Object System.Drawing.SolidBrush($ontario)
    $lakeBrush = New-Object System.Drawing.SolidBrush($lake)
    $borderPen = New-Object System.Drawing.Pen($ontarioBorder, 2.2)
    $coastPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 112, 133, 110), 1.2)

    $manitobaQuebec = Convert-LonLatArrayToPoints @(
        @(-95.5, 57.5), @(-73.0, 57.5), @(-73.0, 45.0), @(-74.0, 44.4), @(-76.0, 43.7),
        @(-79.0, 43.1), @(-82.5, 42.2), @(-89.5, 47.0), @(-95.5, 49.0)
    ) $minLon $maxLon $minLat $maxLat $width $height $padding
    $gfx.FillPolygon($landBrush, $manitobaQuebec)

    $ontarioPoints = Convert-LonLatArrayToPoints @(
        @(-95.2, 49.0), @(-94.7, 50.3), @(-94.0, 51.3), @(-93.7, 52.3), @(-92.8, 53.1),
        @(-91.5, 54.0), @(-90.0, 55.1), @(-87.0, 56.1), @(-84.5, 56.2), @(-82.1, 55.7),
        @(-80.1, 54.9), @(-79.1, 53.9), @(-78.7, 52.5), @(-79.2, 50.9), @(-80.0, 49.8),
        @(-81.7, 48.7), @(-82.8, 47.9), @(-82.0, 46.8), @(-81.3, 46.0), @(-80.2, 45.2),
        @(-79.6, 44.5), @(-79.2, 43.8), @(-78.4, 43.5), @(-77.2, 44.1), @(-76.1, 44.8),
        @(-75.2, 45.6), @(-74.6, 45.3), @(-74.5, 44.7), @(-75.2, 44.2), @(-76.5, 43.7),
        @(-78.4, 43.1), @(-79.2, 42.5), @(-81.9, 42.2), @(-83.4, 42.4), @(-84.6, 45.3),
        @(-88.0, 48.0), @(-91.0, 48.8), @(-93.5, 48.8), @(-95.2, 49.0)
    ) $minLon $maxLon $minLat $maxLat $width $height $padding
    $gfx.FillPolygon($ontarioBrush, $ontarioPoints)
    $gfx.DrawPolygon($borderPen, $ontarioPoints)

    $lakeSuperior = Convert-LonLatArrayToPoints @(
        @(-92.2, 48.0), @(-90.8, 48.3), @(-89.0, 48.9), @(-87.0, 48.8), @(-86.3, 47.9),
        @(-87.8, 47.4), @(-89.8, 47.2), @(-91.5, 47.3), @(-92.4, 47.7)
    ) $minLon $maxLon $minLat $maxLat $width $height $padding
    $lakeHuron = Convert-LonLatArrayToPoints @(
        @(-84.9, 45.9), @(-84.2, 46.4), @(-83.1, 46.2), @(-82.1, 45.7), @(-81.6, 44.9),
        @(-81.9, 44.1), @(-82.9, 44.3), @(-83.9, 44.8), @(-84.7, 45.5)
    ) $minLon $maxLon $minLat $maxLat $width $height $padding
    $lakeErie = Convert-LonLatArrayToPoints @(
        @(-83.2, 42.2), @(-81.0, 42.1), @(-79.2, 42.2), @(-79.1, 42.6), @(-81.4, 42.7), @(-83.0, 42.6)
    ) $minLon $maxLon $minLat $maxLat $width $height $padding
    $lakeOntario = Convert-LonLatArrayToPoints @(
        @(-79.8, 43.1), @(-78.8, 43.1), @(-77.3, 43.2), @(-76.6, 43.5), @(-77.8, 43.7), @(-79.2, 43.6)
    ) $minLon $maxLon $minLat $maxLat $width $height $padding

    foreach ($lakePoly in @($lakeSuperior, $lakeHuron, $lakeErie, $lakeOntario)) {
        $gfx.FillPolygon($lakeBrush, $lakePoly)
        $gfx.DrawPolygon($coastPen, $lakePoly)
    }

    $titleFont = New-Object System.Drawing.Font("Segoe UI", 15, [System.Drawing.FontStyle]::Bold)
    $cityFont = New-Object System.Drawing.Font("Segoe UI", 8, [System.Drawing.FontStyle]::Regular)
    $smallFont = New-Object System.Drawing.Font("Segoe UI", 7, [System.Drawing.FontStyle]::Regular)

    $shadowBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(110, 255, 255, 255))
    $gfx.FillRectangle($shadowBrush, 12, 10, 92, 26)
    $gfx.DrawString("Ontario", $titleFont, $labelBrush, 18, 12)

    $cityBrush = New-Object System.Drawing.SolidBrush($city)
    foreach ($cityInfo in @(
        @{ Name = "Thunder Bay"; Lon = -89.25; Lat = 48.38; Dx = 4; Dy = -8 },
        @{ Name = "Sudbury"; Lon = -81.00; Lat = 46.49; Dx = -34; Dy = -14 },
        @{ Name = "Toronto"; Lon = -79.38; Lat = 43.65; Dx = 4; Dy = 1 },
        @{ Name = "Ottawa"; Lon = -75.70; Lat = 45.42; Dx = -26; Dy = -8 }
    )) {
        $point = Convert-LonLatToPoint -Lon $cityInfo.Lon -Lat $cityInfo.Lat -MinLon $minLon -MaxLon $maxLon -MinLat $minLat -MaxLat $maxLat -Width $width -Height $height -Padding $padding
        $gfx.FillEllipse($cityBrush, $point.X - 2.5, $point.Y - 2.5, 5, 5)
        $gfx.DrawString($cityInfo.Name, $cityFont, $labelBrush, $point.X + $cityInfo.Dx, $point.Y + $cityInfo.Dy)
    }

    $bmp.Save($resolvedPng, [System.Drawing.Imaging.ImageFormat]::Png)
}
finally {
    $gfx.Dispose()
    $bmp.Dispose()
}

Write-LvglRawAlphaC -PngPath $resolvedPng -CPath $resolvedC -SymbolName "osm_no_data_256px"

Write-Output "Generated $resolvedPng"
Write-Output "Generated $resolvedC"
