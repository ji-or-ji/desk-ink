Add-Type -AssemblyName System.Drawing

# 路径基于脚本位置推算，不写死盘符
$fontDir = Split-Path $PSScriptRoot -Parent            # software/font
$projDir = Split-Path (Split-Path $fontDir -Parent) -Parent   # desk-ink
$docsDir = Join-Path $projDir 'docs\desk-eink-info'

$dir = $fontDir
$cjk = [System.IO.File]::ReadAllBytes((Join-Path $dir 'font16_simsun.bin'))
$ascii = [System.IO.File]::ReadAllBytes((Join-Path $dir 'ascii8x16.bin'))

$N = [System.BitConverter]::ToUInt16($cjk, 4)
$codeBase = 6
$bitBase = 6 + $N * 2
Write-Output ("CJK chars = " + $N)

# 建立码点 -> 序号 的查找表（一次性）
$codeMap = @{}
for ($i = 0; $i -lt $N; $i++) {
    $cp = [int][System.BitConverter]::ToUInt16($cjk, $codeBase + $i * 2)
    $codeMap[$cp] = $i
}
Write-Output ("map keys sample: " + ($codeMap.Keys | Sort-Object | Select-Object -First 3))
Write-Output ("lookup U+4E00 (一) -> " + $codeMap[0x4E00])

# dump 第一个字验证点阵数据
$t0 = $bitBase + 0
Write-Output '--- first glyph ---'
for ($ry = 0; $ry -lt 16; $ry++) {
    $lo = $cjk[$t0 + $ry * 2]; $hi = $cjk[$t0 + $ry * 2 + 1]
    $line = ''
    for ($rx = 0; $rx -lt 16; $rx++) {
        if ($rx -lt 8) { $on = ($lo -band (1 -shl (7 - $rx))) -ne 0 }
        else { $on = ($hi -band (1 -shl (15 - $rx))) -ne 0 }
        $line += $(if ($on) { '#' } else { '.' })
    }
    Write-Output $line
}

$W = 400; $H = 300
$fb = New-Object byte[] ($W * $H)
for ($i = 0; $i -lt $fb.Length; $i++) { $fb[$i] = 255 }

function Set-Pixel([int]$x, [int]$y) {
    if ($x -ge 0 -and $x -lt $W -and $y -ge 0 -and $y -lt $H) {
        $script:fb[$y * $W + $x] = 0
    }
}

function Draw-Text([int]$x, [int]$y, [string]$s) {
    $cx = $x
    foreach ($ch in $s.ToCharArray()) {
        $code = [int]$ch
        if ($code -ge 0x20 -and $code -le 0x7F) {
            $b0 = ($code - 0x20) * 16
            for ($ry = 0; $ry -lt 16; $ry++) {
                $bits = $script:ascii[$b0 + $ry]
                if ($bits -ne 0) {
                    for ($rx = 0; $rx -lt 8; $rx++) {
                        if (($bits -band (1 -shl (7 - $rx))) -ne 0) { Set-Pixel ($cx + $rx) ($y + $ry) }
                    }
                }
            }
            $cx += 8
        } else {
            $idx = $script:codeMap[$code]
            if ($idx -eq $null) { $cx += 16; continue }
            $base = $script:bitBase + $idx * 32
            for ($ry = 0; $ry -lt 16; $ry++) {
                $lo = $script:cjk[$base + $ry * 2]
                $hi = $script:cjk[$base + $ry * 2 + 1]
                if ($lo -ne 0) {
                    for ($rx = 0; $rx -lt 8; $rx++) {
                        if (($lo -band (1 -shl (7 - $rx))) -ne 0) { Set-Pixel ($cx + $rx) ($y + $ry) }
                    }
                }
                if ($hi -ne 0) {
                    for ($rx = 8; $rx -lt 16; $rx++) {
                        if (($hi -band (1 -shl (15 - $rx))) -ne 0) { Set-Pixel ($cx + $rx) ($y + $ry) }
                    }
                }
            }
            $cx += 16
        }
    }
}

function Draw-Line([int]$x1, [int]$y1, [int]$x2, [int]$y2) {
    if ($y1 -eq $y2) { for ($x = $x1; $x -le $x2; $x++) { Set-Pixel $x $y1 } }
    elseif ($x1 -eq $x2) { for ($y = $y1; $y -le $y2; $y++) { Set-Pixel $x1 $y } }
}

function Draw-Rect([int]$x, [int]$y, [int]$w, [int]$h) {
    Draw-Line $x $y ($x + $w - 1) $y
    Draw-Line $x ($y + $h - 1) ($x + $w - 1) ($y + $h - 1)
    Draw-Line $x $y $x ($y + $h - 1)
    Draw-Line ($x + $w - 1) $y ($x + $w - 1) ($y + $h - 1)
}

$LX = 266
Draw-Line $LX 64 $LX ($H - 1)
Draw-Line 0 64 ($W - 1) 64
Draw-Line $LX 100 ($W - 1) 100

# 课表长条（横跨整屏）
Draw-Text 8 4  '第3节 数学'
Draw-Text 8 24 '09:40-10:20 · 共40分 · 剩余约23分'
Draw-Text 8 44 '下一节 英语 10:30-11:10'

# 待办
Draw-Text 8 72 '待办'
Draw-Rect 8 99 13 13
Draw-Line 9 105 12 110
Draw-Line 12 110 20 101
Draw-Text 30 96 '英语单词 Unit5'
Draw-Rect 8 129 13 13
Draw-Text 30 126 '物理作业 P42'
Draw-Rect 8 159 13 13
Draw-Text 30 156 '整理数学错题本'
Draw-Rect 8 189 13 13
Draw-Text 30 186 '预习生物第3章'
Draw-Rect 8 219 13 13
Draw-Text 30 216 '买电池'
Draw-Text 8 273 '[键盘编辑 / 拨轮切换]'

# 小窗
Draw-Text ($LX + 8) 72 '2026-09-16 周三'
Draw-Text ($LX + 8) 108 '每日一言'
Draw-Line ($LX + 8) 126 ($W - 8) 126
Draw-Text ($LX + 8) 136 '把一件事'
Draw-Text ($LX + 8) 156 '做完整，比'
Draw-Text ($LX + 8) 176 '做十件半'
Draw-Text ($LX + 8) 196 '成品更有'
Draw-Text ($LX + 8) 216 '价值。'

# 转 Bitmap 输出
$bmp = New-Object System.Drawing.Bitmap -ArgumentList $W, $H, ([System.Drawing.Imaging.PixelFormat]::Format8bppIndexed)
$pal = $bmp.Palette
for ($i = 0; $i -lt 256; $i++) { $pal.Entries[$i] = [System.Drawing.Color]::FromArgb($i, $i, $i) }
$bmp.Palette = $pal
$rect = New-Object System.Drawing.Rectangle -ArgumentList 0, 0, $W, $H
$d = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, [System.Drawing.Imaging.PixelFormat]::Format8bppIndexed)
$stride = $d.Stride
$buf = New-Object byte[] ($stride * $H)
for ($y = 0; $y -lt $H; $y++) {
    [System.Array]::Copy($fb, $y * $W, $buf, $y * $stride, $W)
}
[System.Runtime.InteropServices.Marshal]::Copy($buf, 0, $d.Scan0, $buf.Length)
$bmp.UnlockBits($d)

$out1 = Join-Path $docsDir 'ui-preview.png'
$out2 = Join-Path $docsDir 'ui-preview-2x.png'
$bmp.Save($out1, [System.Drawing.Imaging.ImageFormat]::Png)
$big = New-Object System.Drawing.Bitmap -ArgumentList ($W * 2), ($H * 2)
$bg = [System.Drawing.Graphics]::FromImage($big)
$bg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$bg.DrawImage($bmp, 0, 0, ($W * 2), ($H * 2))
$bg.Dispose()
$big.Save($out2, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose(); $big.Dispose()
Write-Output ('saved ' + $out1)
Write-Output ('saved ' + $out2)
