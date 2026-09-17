<#
.SYNOPSIS
  把一张图片压成墨水屏的 1-bit 画面，可选叠加信息条，输出放大的 PNG 预览。

.DESCRIPTION
  这是 PC 端「插画上传」预处理流程的**参考实现**：
  缩放/裁剪 → 灰度 → 有序抖动(4x4 Bayer) → 1-bit 打包 → （可选）信息条叠加。

  文字用的是**设备端的真实字库**（software/font/*.bin），所以预览和屏上一致。

.EXAMPLE
  # 用内置合成场景，三种信息条各出一张
  .\render-preview.ps1 -Bar solid -Out bar-solid.png
  .\render-preview.ps1 -Bar invert -Out bar-invert.png
  .\render-preview.ps1 -Bar auto   -Out bar-auto.png

.EXAMPLE
  # 用真实图片
  .\render-preview.ps1 -Source D:\pic.jpg -Bar auto -Out preview.png

.EXAMPLE
  # 小窗尺寸、不抖动、反色
  .\render-preview.ps1 -Source D:\pic.jpg -Width 134 -Height 200 -NoDither -Invert
#>
param(
    [string]$Source = '',

    [int]$Width = 400,
    [int]$Height = 300,

    [ValidateSet('cover', 'stretch', 'contain')]
    [string]$Fit = 'cover',

    [switch]$NoDither,
    [int]$Threshold = 128,
    [switch]$Invert,

    [ValidateSet('none', 'solid', 'invert', 'auto')]
    [string]$Bar = 'none',
    [int]$BarHeight = 32,
    [string]$BarLeft = '09:40',
    [string]$BarRight = '第3节 数学',

    [string]$Out = '',
    [int]$Scale = 2
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$script:PW = $Width
$script:PH = $Height
$script:PSTRIDE = [int][Math]::Ceiling($Width / 8)

# ---------------- 字库 ----------------
$fontDir = Join-Path (Split-Path $PSScriptRoot -Parent) 'software\font'
$hPath = Join-Path $fontDir 'font16_simsun.bin'
$aPath = Join-Path $fontDir 'ascii8x16.bin'
if (-not (Test-Path $hPath) -or -not (Test-Path $aPath)) {
    throw "找不到字库：$fontDir（请从仓库根运行，或确认 software/font 存在）"
}
$hBuf = [IO.File]::ReadAllBytes($hPath)
$aBuf = [IO.File]::ReadAllBytes($aPath)
$hCount = [BitConverter]::ToUInt16($hBuf, 4)
$hBase = 6 + $hCount * 2
$hMap = @{}
for ($i = 0; $i -lt $hCount; $i++) { $hMap[[int][BitConverter]::ToUInt16($hBuf, 6 + $i * 2)] = $i }

# ---------------- framebuffer（1 = 黑）----------------
function New-Fb { return [byte[]]::new($script:PSTRIDE * $script:PH) }

function Set-Px($buf, [int]$x, [int]$y, [bool]$black) {
    if ($x -lt 0 -or $x -ge $script:PW -or $y -lt 0 -or $y -ge $script:PH) { return }
    $bi = $y * $script:PSTRIDE + [int][Math]::Floor($x / 8)
    $m = [byte](0x80 -shr ($x % 8))
    if ($black) { $buf[$bi] = [byte]($buf[$bi] -bor $m) }
    else { $buf[$bi] = [byte]($buf[$bi] -band [byte](0xFF -bxor $m)) }
}
function Get-Px($buf, [int]$x, [int]$y) {
    if ($x -lt 0 -or $x -ge $script:PW -or $y -lt 0 -or $y -ge $script:PH) { return $false }
    $bi = $y * $script:PSTRIDE + [int][Math]::Floor($x / 8)
    return (($buf[$bi] -band [byte](0x80 -shr ($x % 8))) -ne 0)
}
function Invert-Rect($buf, [int]$x0, [int]$y0, [int]$w, [int]$h) {
    for ($y = [Math]::Max(0, $y0); $y -lt [Math]::Min($script:PH, $y0 + $h); $y++) {
        for ($x = [Math]::Max(0, $x0); $x -lt [Math]::Min($script:PW, $x0 + $w); $x++) {
            $bi = $y * $script:PSTRIDE + [int][Math]::Floor($x / 8)
            $buf[$bi] = [byte]($buf[$bi] -bxor [byte](0x80 -shr ($x % 8)))
        }
    }
}
function Count-Dark($buf, [int]$x0, [int]$y0, [int]$w, [int]$h) {
    $n = 0
    for ($y = [Math]::Max(0, $y0); $y -lt [Math]::Min($script:PH, $y0 + $h); $y++) {
        for ($x = [Math]::Max(0, $x0); $x -lt [Math]::Min($script:PW, $x0 + $w); $x++) {
            if (Get-Px $buf $x $y) { $n++ }
        }
    }
    return $n
}
function Fill-Rect($buf, [int]$x0, [int]$y0, [int]$w, [int]$h, [bool]$black) {
    for ($y = [Math]::Max(0, $y0); $y -lt [Math]::Min($script:PH, $y0 + $h); $y++) {
        for ($x = [Math]::Max(0, $x0); $x -lt [Math]::Min($script:PW, $x0 + $w); $x++) { Set-Px $buf $x $y $black }
    }
}
function Draw-Text($buf, [int]$x, [int]$y, [string]$s, [bool]$black) {
    $cx = $x
    foreach ($ch in $s.ToCharArray()) {
        $code = [int][char]$ch
        if ($code -lt 128) {
            if ($code -lt 32 -or $code -gt 126) { $cx += 8; continue }
            $off = ($code - 32) * 16
            for ($row = 0; $row -lt 16; $row++) {
                $b = $aBuf[$off + $row]
                for ($col = 0; $col -lt 8; $col++) {
                    if (($b -band (0x80 -shr $col)) -ne 0) { Set-Px $buf ($cx + $col) ($y + $row) $black }
                }
            }
            $cx += 8
        } else {
            if ($hMap.ContainsKey($code)) {
                $off = $hBase + $hMap[$code] * 32
                for ($row = 0; $row -lt 16; $row++) {
                    $hi = $hBuf[$off + $row * 2]; $lo = $hBuf[$off + $row * 2 + 1]
                    for ($col = 0; $col -lt 8; $col++) {
                        if (($hi -band (0x80 -shr $col)) -ne 0) { Set-Px $buf ($cx + $col) ($y + $row) $black }
                        if (($lo -band (0x80 -shr $col)) -ne 0) { Set-Px $buf ($cx + 8 + $col) ($y + $row) $black }
                    }
                }
            }
            $cx += 16
        }
    }
}
function Text-Width([string]$s) {
    $w = 0
    foreach ($ch in $s.ToCharArray()) { if ([int][char]$ch -lt 128) { $w += 8 } else { $w += 16 } }
    return $w
}

# ---------------- 图像 → 1-bit ----------------
$bayer = @(@(0, 8, 2, 10), @(12, 4, 14, 6), @(3, 11, 1, 9), @(15, 7, 13, 5))

function New-SceneFb {
    # 内置合成场景：亮天空 + 远山 + 近山 + 地面
    $buf = New-Fb
    for ($y = 0; $y -lt $script:PH; $y++) {
        for ($x = 0; $x -lt $script:PW; $x++) {
            $v = 238 - ($y * 0.52)
            $dx = $x - ($script:PW * 0.79); $dy = $y - ($script:PH * 0.22)
            $r = [Math]::Sqrt($dx * $dx + $dy * $dy)
            if ($r -lt 28) { $v = 252 } elseif ($r -lt 34) { $v = 205 }
            $m1 = ($script:PH * 0.44) - [Math]::Abs($x - ($script:PW * 0.27)) * 0.72
            if ($y -gt $m1) { $v = 88 }
            $m2 = ($script:PH * 0.59) - [Math]::Abs($x - ($script:PW * 0.71)) * 0.55
            if ($y -gt $m2) { $v = 58 }
            if ($y -gt ($script:PH * 0.79)) { $v = 44 + (($x * 5) % 26) }
            $t = $bayer[$y % 4][$x % 4]
            if (($v + ($t / 5) - 50) -lt 128) { Set-Px $buf $x $y $true }
        }
    }
    return $buf
}

function New-PhotoFb([string]$path, [string]$fit) {
    $src = [System.Drawing.Bitmap]::FromFile($path)
    $tmp = New-Object System.Drawing.Bitmap $script:PW, $script:PH, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($tmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic

    if ($fit -eq 'stretch') {
        $g.DrawImage($src, 0, 0, $script:PW, $script:PH)
    }
    elseif ($fit -eq 'contain') {
        $g.Clear([System.Drawing.Color]::White)
        $k = [Math]::Min($script:PW / $src.Width, $script:PH / $src.Height)
        $dw = [int]($src.Width * $k); $dh = [int]($src.Height * $k)
        $g.DrawImage($src, [int](($script:PW - $dw) / 2), [int](($script:PH - $dh) / 2), $dw, $dh)
    }
    else {
        # cover：按目标比例居中裁剪，再缩放
        $targetRatio = $script:PW / $script:PH
        $srcRatio = $src.Width / $src.Height
        if ($srcRatio -gt $targetRatio) {
            $cropW = [int]($src.Height * $targetRatio); $cropH = $src.Height
            $offX = [int](($src.Width - $cropW) / 2); $offY = 0
        }
        else {
            $cropW = $src.Width; $cropH = [int]($src.Width / $targetRatio)
            $offX = 0; $offY = [int](($src.Height - $cropH) / 2)
        }
        $cropRect = New-Object System.Drawing.Rectangle $offX, $offY, $cropW, $cropH
        $crop = $src.Clone($cropRect, $src.PixelFormat)
        $g.DrawImage($crop, 0, 0, $script:PW, $script:PH)
        $crop.Dispose()
    }

    $g.Dispose(); $src.Dispose()

    $rect = New-Object System.Drawing.Rectangle 0, 0, $script:PW, $script:PH
    $data = $tmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $stride = $data.Stride
    $px = [byte[]]::new($stride * $script:PH)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $px, 0, $px.Length)
    $tmp.UnlockBits($data); $tmp.Dispose()

    $buf = New-Fb
    for ($y = 0; $y -lt $script:PH; $y++) {
        for ($x = 0; $x -lt $script:PW; $x++) {
            $p = $y * $stride + $x * 3
            $lum = 0.299 * $px[$p + 2] + 0.587 * $px[$p + 1] + 0.114 * $px[$p]
            if ($script:NoDither) {
                $black = ($lum -lt $Threshold)
            }
            else {
                $black = ($lum -lt (($bayer[$y % 4][$x % 4] + 0.5) / 16 * 255))
            }
            if ($script:Invert) { $black = -not $black }
            if ($black) { Set-Px $buf $x $y $true }
        }
    }
    return $buf
}

# ---------------- 信息条 ----------------
function Invoke-Bar($buf, [string]$mode, [int]$h, [string]$left, [string]$right) {
    if ($h -le 0 -or $mode -eq 'none') { return }
    $h = [Math]::Min($h, $script:PH)
    $ty = [int](($h - 16) / 2)

    switch ($mode) {
        'solid' {
            Fill-Rect $buf 0 0 $script:PW $h $true
            Draw-Text $buf 12 $ty $left $false
            Draw-Text $buf ($script:PW - 12 - (Text-Width $right)) $ty $right $false
        }
        'invert' {
            Invert-Rect $buf 0 0 $script:PW $h
            Draw-Text $buf 12 $ty $left $false
            Draw-Text $buf ($script:PW - 12 - (Text-Width $right)) $ty $right $false
        }
        'auto' {
            # 逐段处理：取包围盒 → 数黑 → 取反 → 按取反后明暗选字色
            $pad = 2
            $runs = @(
                @{ X = 10; Y = $ty; W = (Text-Width $left);  S = $left },
                @{ X = ($script:PW - 12 - (Text-Width $right)); Y = $ty; W = (Text-Width $right); S = $right }
            )
            foreach ($run in $runs) {
                if ($run.W -le 0) { continue }
                $rx = $run.X - $pad; $ry = $run.Y - $pad
                $rw = $run.W + $pad * 2; $rh = 16 + $pad * 2
                $area = $rw * $rh
                $d0 = Count-Dark $buf $rx $ry $rw $rh
                Invert-Rect $buf $rx $ry $rw $rh
                $darkAfter = $area - $d0
                $whiteText = ($darkAfter * 2 -gt $area)
                Draw-Text $buf $run.X $run.Y $run.S (-not $whiteText)
            }
        }
    }
}

# ---------------- 输出 PNG ----------------
function Save-Fb($buf, [string]$path, [int]$scale) {
    $bw = $script:PW * $scale; $bh = $script:PH * $scale
    $bmp = New-Object System.Drawing.Bitmap $bw, $bh, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $rect = New-Object System.Drawing.Rectangle 0, 0, $bw, $bh
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $bmp.PixelFormat)
    $rowStride = $data.Stride
    $row = [byte[]]::new($rowStride)
    for ($y = 0; $y -lt $script:PH; $y++) {
        $p = 0
        for ($x = 0; $x -lt $script:PW; $x++) {
            if (Get-Px $buf $x $y) { $c = [byte]30 } else { $c = [byte]244 }
            for ($sx = 0; $sx -lt $scale; $sx++) { $row[$p] = $c; $row[$p + 1] = $c; $row[$p + 2] = $c; $p += 3 }
        }
        for ($sy = 0; $sy -lt $scale; $sy++) {
            [System.Runtime.InteropServices.Marshal]::Copy($row, 0, [IntPtr]($data.Scan0.ToInt64() + ($y * $scale + $sy) * $rowStride), $rowStride)
        }
    }
    $bmp.UnlockBits($data)
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

# ---------------- 主流程 ----------------
if ([string]::IsNullOrWhiteSpace($Source)) {
    Write-Host '未指定 -Source，使用内置合成场景'
    $fb = New-SceneFb
}
else {
    if (-not (Test-Path $Source)) { throw "找不到源图：$Source" }
    $fb = New-PhotoFb $Source $Fit
}

Invoke-Bar $fb $Bar $BarHeight $BarLeft $BarRight

if ([string]::IsNullOrWhiteSpace($Out)) {
    $Out = Join-Path (Get-Location) 'preview.png'
}
Save-Fb $fb $Out $Scale

$bytes = $script:PSTRIDE * $script:PH
Write-Host ("已输出 {0}  ({1}x{2}, 1-bit 占 {3} 字节)" -f $Out, $script:PW, $script:PH, $bytes)
