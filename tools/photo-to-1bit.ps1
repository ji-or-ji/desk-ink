<#
.SYNOPSIS
  把一张照片转成墨水屏用的 1-bit 裸位图（无文件头）。

.DESCRIPTION
  流程（与 tools/render-preview.ps1 一致）：
    居中裁成目标比例(cover) → 缩放 → 灰度(0.299R+0.587G+0.114B)
    → 4×4 Bayer 有序抖动 → 1-bit
  输出：行优先、MSB 在左、1 = 黑；stride = ceil(W/8)。

  默认产出两份：
    art_full.bin   400×300  stride 50  15000 字节
    art_panel.bin  134×200  stride 17   3400 字节

.EXAMPLE
  .\photo-to-1bit.ps1 -Source 'D:\pic.jpg'
#>
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [string]$OutDir = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

if (-not (Test-Path -LiteralPath $Source)) { throw "找不到源图：$Source" }
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $repo = Split-Path $PSScriptRoot -Parent
    $OutDir = Join-Path $repo 'software\testdata'
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$bayer = @(@(0, 8, 2, 10), @(12, 4, 14, 6), @(3, 11, 1, 9), @(15, 7, 13, 5))

function Convert-Cover([string]$path, [int]$W, [int]$H) {
    $stride = [int][Math]::Ceiling($W / 8)
    $buf = [byte[]]::new($stride * $H)

    $src = [System.Drawing.Bitmap]::FromFile($path)
    $tmp = New-Object System.Drawing.Bitmap $W, $H, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($tmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic

    $targetRatio = $W / $H
    $srcRatio = $src.Width / $src.Height
    if ($srcRatio -gt $targetRatio) {
        $cropW = [int]($src.Height * $targetRatio); $cropH = $src.Height
        $offX = [int](($src.Width - $cropW) / 2); $offY = 0
    } else {
        $cropW = $src.Width; $cropH = [int]($src.Width / $targetRatio)
        $offX = 0; $offY = [int](($src.Height - $cropH) / 2)
    }
    $cropRect = New-Object System.Drawing.Rectangle $offX, $offY, $cropW, $cropH
    $crop = $src.Clone($cropRect, $src.PixelFormat)
    $g.DrawImage($crop, 0, 0, $W, $H)
    $crop.Dispose()
    $g.Dispose(); $src.Dispose()

    $rect = New-Object System.Drawing.Rectangle 0, 0, $W, $H
    $data = $tmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $rowStride = $data.Stride
    $px = [byte[]]::new($rowStride * $H)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $px, 0, $px.Length)
    $tmp.UnlockBits($data); $tmp.Dispose()

    $dark = 0
    for ($y = 0; $y -lt $H; $y++) {
        for ($x = 0; $x -lt $W; $x++) {
            $p = $y * $rowStride + $x * 3
            $lum = 0.299 * $px[$p + 2] + 0.587 * $px[$p + 1] + 0.114 * $px[$p]
            $thr = ($bayer[$y % 4][$x % 4] + 0.5) / 16 * 255
            if ($lum -lt $thr) {
                $bi = $y * $stride + [int][Math]::Floor($x / 8)
                $buf[$bi] = [byte]($buf[$bi] -bor (0x80 -shr ($x % 8)))
                $dark++
            }
        }
    }
    return @{ Buf = $buf; Dark = $dark; Total = $W * $H; W = $W; H = $H; Stride = $stride }
}

$full = Convert-Cover $Source 400 300
$panel = Convert-Cover $Source 134 200

$fullPath = Join-Path $OutDir 'art_full.bin'
$panelPath = Join-Path $OutDir 'art_panel.bin'
[System.IO.File]::WriteAllBytes($fullPath, $full.Buf)
[System.IO.File]::WriteAllBytes($panelPath, $panel.Buf)

Write-Output ("full : {0}  {1}x{2} stride {3}  {4} bytes  ink {5:P1}" -f $fullPath, $full.W, $full.H, $full.Stride, $full.Buf.Length, ($full.Dark / $full.Total))
Write-Output ("panel: {0}  {1}x{2} stride {3}  {4} bytes  ink {5:P1}" -f $panelPath, $panel.W, $panel.H, $panel.Stride, $panel.Buf.Length, ($panel.Dark / $panel.Total))
