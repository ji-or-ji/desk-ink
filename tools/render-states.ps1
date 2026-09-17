<#
.SYNOPSIS
  把固件 ui.c 的各个界面状态画成一张对照图。

.DESCRIPTION
  这是 ui.c 的**忠实移植**：底层原语（fb_*）、字模绘制、以及 ui_render_* 各状态
  的坐标与文案，全部照 `software/components/ui/ui.c` 抄。
  取数与 `app.c` 的 seed_placeholder_data() 一致，所以出图和实机接近。

  目的：不用读代码、不用硬件，一眼看清已实现到什么程度。

.EXAMPLE
  .\render-states.ps1 -Out states.png
  .\render-states.ps1 -Photo D:\pic.jpg -Out states.png
#>
param(
    [string]$Photo = '',
    [string]$Out = '',
    [int]$Scale = 2
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# ================== 布局常量（照 ui.c） ==================
$SCHED_X = 0; $SCHED_Y = 0; $SCHED_W = 400; $SCHED_H = 64
$TODO_X = 0; $TODO_Y = 64; $TODO_W = 266; $TODO_H = 236
$PANEL_X = 266; $PANEL_Y = 64; $PANEL_W = 134; $PANEL_H = 236
$DATE_H = 36
$CONTENT_Y = $PANEL_Y + $DATE_H      # 100
$CONTENT_H = $PANEL_H - $DATE_H      # 200
$PAD = 8; $LINE_H = 20
$UI_BAR_H = 32
$EINK_W = 400; $EINK_H = 300

# 状态枚举
$UI_CFG_REFRESH = 0; $UI_CFG_POMO_FOCUS = 1; $UI_CFG_POMO_BREAK = 2
$UI_CFG_ROTATE = 3; $UI_CFG_ART_MODE = 4; $UI_CFG_BAR_MODE = 5; $UI_CFG_COUNT = 6
$UI_BAR_SOLID = 0; $UI_BAR_INVERT = 1; $UI_BAR_INVERT_AUTO = 2
$UI_PANEL_NEWS = 0; $UI_PANEL_IMAGE = 1; $UI_PANEL_QUOTE = 2

# ================== 字库 ==================
$fontDir = Join-Path (Split-Path $PSScriptRoot -Parent) 'software\font'
$hBuf = [IO.File]::ReadAllBytes((Join-Path $fontDir 'font16_simsun.bin'))
$aBuf = [IO.File]::ReadAllBytes((Join-Path $fontDir 'ascii8x16.bin'))
$hCount = [BitConverter]::ToUInt16($hBuf, 4)
$hBase = 6 + $hCount * 2
$hMap = @{}
for ($i = 0; $i -lt $hCount; $i++) { $hMap[[int][BitConverter]::ToUInt16($hBuf, 6 + $i * 2)] = $i }

# ================== 画布（整张对照图共用一个 1-bit 缓冲） ==================
$COLS = 4; $ROWS = 3
$LBL_H = 26; $MARGIN = 8
$CELL_STEP_X = $EINK_W + $MARGIN * 2
$CELL_STEP_Y = $EINK_H + $LBL_H + $MARGIN
$CANVAS_W = $COLS * $CELL_STEP_X + $MARGIN
$CANVAS_H = $ROWS * $CELL_STEP_Y + $MARGIN
$CANVAS_STRIDE = [int][Math]::Ceiling($CANVAS_W / 8)

$script:CANVAS = [byte[]]::new($CANVAS_STRIDE * $CANVAS_H)
$script:OX = 0; $script:OY = 0

function Set-Px([int]$x, [int]$y, [bool]$black) {
    $ax = $x + $script:OX; $ay = $y + $script:OY
    if ($ax -lt 0 -or $ax -ge $CANVAS_W -or $ay -lt 0 -or $ay -ge $CANVAS_H) { return }
    $bi = $ay * $CANVAS_STRIDE + [int][Math]::Floor($ax / 8)
    $m = [byte](0x80 -shr ($ax % 8))
    if ($black) { $script:CANVAS[$bi] = [byte]($script:CANVAS[$bi] -bor $m) }
    else { $script:CANVAS[$bi] = [byte]($script:CANVAS[$bi] -band [byte](0xFF -bxor $m)) }
}
function Get-PxAbs([int]$ax, [int]$ay) {
    if ($ax -lt 0 -or $ax -ge $CANVAS_W -or $ay -lt 0 -or $ay -ge $CANVAS_H) { return $false }
    $bi = $ay * $CANVAS_STRIDE + [int][Math]::Floor($ax / 8)
    return (($script:CANVAS[$bi] -band [byte](0x80 -shr ($ax % 8))) -ne 0)
}
function Get-Px([int]$x, [int]$y) { return (Get-PxAbs ($x + $script:OX) ($y + $script:OY)) }

# ---------- 绘制原语（照 eink.h 语义） ----------
function Fb-Clear([bool]$white) {
    for ($y = 0; $y -lt $EINK_H; $y++) { for ($x = 0; $x -lt $EINK_W; $x++) { Set-Px $x $y (-not $white) } }
}
function Fb-HLine([int]$x0, [int]$x1, [int]$y, [bool]$black) {
    if ($x1 -lt $x0) { $t = $x0; $x0 = $x1; $x1 = $t }
    for ($x = $x0; $x -le $x1; $x++) { Set-Px $x $y $black }
}
function Fb-VLine([int]$x, [int]$y0, [int]$y1, [bool]$black) {
    if ($y1 -lt $y0) { $t = $y0; $y0 = $y1; $y1 = $t }
    for ($y = $y0; $y -le $y1; $y++) { Set-Px $x $y $black }
}
function Fb-Rect([int]$x, [int]$y, [int]$w, [int]$h, [bool]$black, [bool]$fill) {
    if ($fill) {
        for ($yy = $y; $yy -lt $y + $h; $yy++) { for ($xx = $x; $xx -lt $x + $w; $xx++) { Set-Px $xx $yy $black } }
    }
    else {
        Fb-HLine $x ($x + $w - 1) $y $black
        Fb-HLine $x ($x + $w - 1) ($y + $h - 1) $black
        Fb-VLine $x $y ($y + $h - 1) $black
        Fb-VLine ($x + $w - 1) $y ($y + $h - 1) $black
    }
}
function Fb-Blit1Bit($data, [int]$x, [int]$y, [int]$w, [int]$h, [bool]$invert) {
    $st = [int][Math]::Ceiling($w / 8)
    for ($yy = 0; $yy -lt $h; $yy++) {
        for ($xx = 0; $xx -lt $w; $xx++) {
            $bit = (($data[$yy * $st + [int][Math]::Floor($xx / 8)] -shr (7 - ($xx % 8))) -band 1) -ne 0
            Set-Px ($x + $xx) ($y + $yy) ($bit -xor $invert)
        }
    }
}

# ---------- 字模 ----------
function Get-GlyphInfo([int]$cp) {
    if ($cp -lt 128) {
        if ($cp -lt 32 -or $cp -gt 126) { return $null }
        return @{ Off = ($cp - 32) * 16; W = 8; PerRow = 1; Buf = $aBuf }
    }
    if ($hMap.ContainsKey($cp)) { return @{ Off = $hBase + $hMap[$cp] * 32; W = 16; PerRow = 2; Buf = $hBuf } }
    return $null
}
function Font-TextWidth([string]$s) {
    $w = 0
    foreach ($ch in $s.ToCharArray()) { $w += $(if ([int][char]$ch -lt 128) { 8 } else { 16 }) }
    return $w
}
# invert=true 时该字格反白（黑底白字）
function Font-DrawText([int]$x, [int]$y, [string]$s, [bool]$invert) {
    $cx = $x
    foreach ($ch in $s.ToCharArray()) {
        $cp = [int][char]$ch
        $cw = $(if ($cp -lt 128) { 8 } else { 16 })
        $gi = Get-GlyphInfo $cp
        if ($gi) {
            if ($invert) { Fb-Rect $cx $y $gi.W 16 $true $true }
            for ($row = 0; $row -lt 16; $row++) {
                for ($col = 0; $col -lt $gi.W; $col++) {
                    $byteIdx = $row * $gi.PerRow + [int][Math]::Floor($col / 8)
                    $bit = ($gi.Buf[$gi.Off + $byteIdx] -shr (7 - ($col % 8))) -band 1
                    if ($bit -ne 0) { Set-Px ($cx + $col) ($y + $row) (-not $invert) }
                }
            }
        }
        $cx += $cw
    }
    return $cx - $x
}

# ================== 状态数据（照 app.c 的 seed_placeholder_data） ==================
$script:DATA = @{
    Period   = 3
    CurName  = '数学'
    CurStart = '09:40'
    CurEnd   = '10:20'
    CurLeft  = 23
    NextName = '英语'
    NextStart = '10:30'
    NextEnd  = '11:10'
    Date     = '2026-09-17 周四'
    Clock    = '09:40'
    Quote    = '把一件事做完整，比做十件半成品更有价值。'
    NewsTitle = '校内通知'
    NewsBody  = '下周三下午进行秋季运动会彩排，请各班提前十分钟到操场集合，注意携带水杯。'
    Todos    = @(
        @{ done = $true;  text = '英语单词 Unit5' },
        @{ done = $false; text = '买电池' },
        @{ done = $false; text = '预习生物第3章' }
    )
}
$script:ART_MODE = 0
$script:BAR_MODE = $UI_BAR_SOLID
$script:POOL = @()      # @{kind;title;body;imageId}
$script:POOL_CUR = 0

# ================== ui.c 移植 ==================
function Clear-Region([int]$x, [int]$y, [int]$w, [int]$h) { Fb-Rect $x $y $w $h $false $true }
function Draw-PanelBorder([int]$y, [int]$h) { Fb-VLine $PANEL_X $y ($y + $h - 1) $true }

function Draw-Wrapped([int]$x, [int]$y, [int]$maxW, [string]$text, [int]$lineH, [int]$maxLines) {
    if (-not $text) { return 0 }
    $lines = 0; $line = ''; $lineW = 0
    foreach ($ch in $text.ToCharArray()) {
        $cw = $(if ([int][char]$ch -lt 128) { 8 } else { 16 })
        if ($lineW + $cw -gt $maxW -and $line.Length -gt 0) {
            [void](Font-DrawText $x ($y + $lines * $lineH) $line $false)
            $lines++; $line = ''; $lineW = 0
            if ($lines -ge $maxLines) { break }
        }
        $line += $ch; $lineW += $cw
    }
    if ($line.Length -gt 0 -and $lines -lt $maxLines) {
        [void](Font-DrawText $x ($y + $lines * $lineH) $line $false)
        $lines++
    }
    return $lines
}

function Draw-Schedule {
    Clear-Region $SCHED_X $SCHED_Y $SCHED_W $SCHED_H
    Fb-HLine $SCHED_X ($SCHED_X + $SCHED_W - 1) ($SCHED_Y + $SCHED_H - 1) $true
    if (-not $DATA.CurName) { [void](Font-DrawText $PAD 4 '暂无课程' $false); return }
    [void](Font-DrawText $PAD 4 ("第{0}节 {1}" -f $DATA.Period, $DATA.CurName) $false)
    $total = 40
    [void](Font-DrawText $PAD 24 ("{0}-{1} · 共{2}分 · 剩余约{3}分" -f $DATA.CurStart, $DATA.CurEnd, $total, $DATA.CurLeft) $false)
    [void](Font-DrawText $PAD 44 ("下一节 {0} {1}-{2}" -f $DATA.NextName, $DATA.NextStart, $DATA.NextEnd) $false)
}

function Draw-Checkbox([int]$x, [int]$y, [bool]$done) {
    Fb-Rect $x $y 13 13 $true $false
    if ($done) {
        foreach ($p in @(@(3,6), @(4,7), @(5,8), @(6,7), @(7,6), @(8,5), @(9,4))) { Set-Px ($x + $p[0]) ($y + $p[1]) $true }
    }
}
function Draw-Todo([int]$cursor, [bool]$editing) {
    Clear-Region $TODO_X $TODO_Y $TODO_W $TODO_H
    Fb-VLine $PANEL_X $TODO_Y ($TODO_Y + $TODO_H - 1) $true
    [void](Font-DrawText $PAD ($TODO_Y + 8) '待办' $false)
    if ($DATA.Todos.Count -eq 0) { [void](Font-DrawText $PAD ($TODO_Y + 35) '（暂无待办）' $false); return }
    $iy = $TODO_Y + 35
    for ($i = 0; $i -lt $DATA.Todos.Count; $i++) {
        $y = $iy + $i * 30
        if ($i -eq $cursor) { Fb-Rect 2 $y 3 13 $true $true }
        Draw-Checkbox $PAD $y $DATA.Todos[$i].done
        [void](Font-DrawText 30 ($y - 3) $DATA.Todos[$i].text $false)
        if ($editing -and $i -eq $cursor) {
            $w = Font-TextWidth $DATA.Todos[$i].text
            Fb-Rect (30 + $w) ($y - 3) 8 16 $true $true
        }
    }
}

function Draw-Date {
    Clear-Region $PANEL_X $PANEL_Y $PANEL_W $DATE_H
    Draw-PanelBorder $PANEL_Y $DATE_H
    [void](Font-DrawText ($PANEL_X + $PAD) ($PANEL_Y + 8) $DATA.Date $false)
    Fb-HLine $PANEL_X ($PANEL_X + $PANEL_W - 1) ($CONTENT_Y - 1) $true
}

function Panel-Title([int]$kind) {
    switch ($kind) { 0 { '新闻' } 1 { '插画' } 2 { '每日一言' } default { '' } }
}

$script:IMG_FULL = $null
$script:IMG_PANEL = $null

function Draw-Panel {
    Clear-Region $PANEL_X $CONTENT_Y $PANEL_W $CONTENT_H
    Draw-PanelBorder $CONTENT_Y $CONTENT_H
    if ($script:POOL.Count -eq 0) { [void](Font-DrawText ($PANEL_X + $PAD) ($CONTENT_Y + 8) '暂无内容' $false); return }
    $it = $script:POOL[$script:POOL_CUR]
    if ($it.kind -eq $UI_PANEL_IMAGE) {
        if ($script:IMG_PANEL) { Fb-Blit1Bit $script:IMG_PANEL $PANEL_X $CONTENT_Y 134 200 $false }
        return
    }
    $title = $(if ($it.title) { $it.title } else { Panel-Title $it.kind })
    [void](Font-DrawText ($PANEL_X + $PAD) ($CONTENT_Y + 8) $title $false)
    Fb-HLine ($PANEL_X + $PAD) ($PANEL_X + $PANEL_W - $PAD) ($CONTENT_Y + 26) $true
    [void](Draw-Wrapped ($PANEL_X + $PAD) ($CONTENT_Y + 36) ($PANEL_W - 2 * $PAD) $it.body $LINE_H ([int](($CONTENT_H - 46) / $LINE_H)))
}

function Draw-Pomo([int]$secondsLeft) {
    Clear-Region $PANEL_X $CONTENT_Y $PANEL_W $CONTENT_H
    Draw-PanelBorder $CONTENT_Y $CONTENT_H
    if ($secondsLeft -lt 0) { $secondsLeft = 0 }
    $t = '{0:d2}:{1:d2}' -f [int]($secondsLeft / 60), ($secondsLeft % 60)
    [void](Font-DrawText ($PANEL_X + $PAD) ($CONTENT_Y + 8) '番茄钟' $false)
    Fb-HLine ($PANEL_X + $PAD) ($PANEL_X + $PANEL_W - $PAD) ($CONTENT_Y + 26) $true
    [void](Font-DrawText ($PANEL_X + $PAD + 24) ($CONTENT_Y + 70) $t $false)
    [void](Font-DrawText ($PANEL_X + $PAD + 12) ($CONTENT_Y + 110) '中央键暂停' $false)
}

function Count-Dark([int]$x, [int]$y, [int]$w, [int]$h) {
    $c = 0
    for ($yy = $y; $yy -lt $y + $h; $yy++) { for ($xx = $x; $xx -lt $x + $w; $xx++) { if (Get-Px $xx $yy) { $c++ } } }
    return $c
}
function Invert-Rect([int]$x, [int]$y, [int]$w, [int]$h) {
    for ($yy = $y; $yy -lt $y + $h; $yy++) { for ($xx = $x; $xx -lt $x + $w; $xx++) { Set-Px $xx $yy (-not (Get-Px $xx $yy)) } }
}

function Draw-SegmentAuto([int]$x, [int]$y, [string]$text) {
    if (-not $text) { return }
    $w = Font-TextWidth $text
    $bx = $x - 2; $by = $y; $bw = $w + 4; $bh = 16
    if ($bx -lt 0) { $bw += $bx; $bx = 0 }
    if ($by -lt 0) { $bh += $by; $by = 0 }
    if ($bx + $bw -gt $EINK_W) { $bw = $EINK_W - $bx }
    if ($by + $bh -gt $EINK_H) { $bh = $EINK_H - $by }
    if ($bw -le 0 -or $bh -le 0) { return }
    $d0 = Count-Dark $bx $by $bw $bh
    Invert-Rect $bx $by $bw $bh
    $darkAfter = $bw * $bh - $d0
    $white = ($darkAfter * 2 -gt $bw * $bh)
    [void](Font-DrawText $x $y $text $white)
}

function Draw-InfoBar {
    $left = $DATA.Clock
    $right = ''
    if ($DATA.CurName) { $right = "第{0}节 {1}" -f $DATA.Period, $DATA.CurName }
    $lx = 8; $ly = 8
    $rx = $EINK_W - 8 - (Font-TextWidth $right); $ry = 8
    switch ($script:BAR_MODE) {
        $UI_BAR_SOLID {
            Fb-Rect 0 0 $EINK_W $UI_BAR_H $true $true
            [void](Font-DrawText $lx $ly $left $true)
            [void](Font-DrawText $rx $ry $right $true)
        }
        $UI_BAR_INVERT {
            Invert-Rect 0 0 $EINK_W $UI_BAR_H
            [void](Font-DrawText $lx $ly $left $true)
            [void](Font-DrawText $rx $ry $right $true)
        }
        default {
            Draw-SegmentAuto $lx $ly $left
            Draw-SegmentAuto $rx $ry $right
        }
    }
}

function Ui-RenderIdle {
    $full = ($script:POOL.Count -gt 0 -and $script:POOL[$script:POOL_CUR].kind -eq $UI_PANEL_IMAGE -and $script:ART_MODE -eq 0)
    if ($full) {
        Fb-Clear $true
        if ($script:IMG_FULL) { Fb-Blit1Bit $script:IMG_FULL 0 0 400 300 $false }
        Draw-InfoBar
        return
    }
    Draw-Schedule
    Draw-Todo -1 $false
    Draw-Date
    Draw-Panel
}

function Ui-RenderMenu([int]$cursor) {
    Ui-RenderIdle
    $bx = 130; $by = 80; $bw = 140; $bh = 130
    Fb-Rect ($bx + 1) ($by + 1) ($bw - 2) ($bh - 2) $false $true
    Fb-Rect $bx $by $bw $bh $true $false
    [void](Font-DrawText ($bx + 10) ($by + 8) '菜单' $false)
    Fb-HLine ($bx + 8) ($bx + $bw - 8) ($by + 28) $true
    $labels = @('番茄钟', '配置', '取消')
    for ($i = 0; $i -lt 3; $i++) {
        $y = $by + 42 + $i * 26
        $sel = ($i -eq $cursor)
        if ($sel) { Fb-Rect ($bx + 4) ($y - 4) ($bw - 8) 22 $true $true }
        [void](Font-DrawText ($bx + 14) $y $labels[$i] $sel)
    }
}

function Config-ItemName([int]$item) {
    switch ($item) {
        0 { '刷新档位' } 1 { '番茄钟专注' } 2 { '番茄钟休息' }
        3 { '小窗轮换' } 4 { '插画显示' } 5 { '信息条' } default { '?' }
    }
}
function Config-ValueLabel([int]$item, [int]$value) {
    switch ($item) {
        0 { if ($value -eq 0) { '自动' } elseif ($value -eq 1) { '常规' } else { '静默' } }
        1 { "$value 分" }
        2 { "$value 分" }
        3 { if ($value) { '开' } else { '关' } }
        4 { if ($value -eq 0) { '全屏' } else { '小窗' } }
        5 { if ($value -eq 0) { '实心' } elseif ($value -eq 1) { '反色' } else { '反色自适应' } }
        default { "$value" }
    }
}

function Ui-RenderConfig([int]$item, [int]$value) {
    Clear-Region 0 0 $EINK_W $EINK_H
    $idx = "{0}/{1}" -f ($item + 1), $UI_CFG_COUNT
    [void](Font-DrawText $PAD 10 '配置' $false)
    [void](Font-DrawText ($EINK_W - $PAD - (Font-TextWidth $idx)) 10 $idx $false)
    Fb-HLine $PAD ($EINK_W - $PAD) 36 $true

    $name = Config-ItemName $item
    [void](Font-DrawText ([int](($EINK_W - (Font-TextWidth $name)) / 2)) 84 $name $false)

    $val = Config-ValueLabel $item $value
    $vw = Font-TextWidth $val
    $vx = [int](($EINK_W - $vw) / 2)
    Fb-Rect ($vx - 14) 124 ($vw + 28) 30 $true $true
    [void](Font-DrawText $vx 131 $val $true)

    Fb-HLine $PAD ($EINK_W - $PAD) 222 $true
    [void](Font-DrawText $PAD 240 '上/下 换项' $false)
    [void](Font-DrawText $PAD 262 '左/右 调值' $false)
    $r1 = '中央短按保存'; $r2 = '长按返回'
    [void](Font-DrawText ($EINK_W - $PAD - (Font-TextWidth $r1)) 240 $r1 $false)
    [void](Font-DrawText ($EINK_W - $PAD - (Font-TextWidth $r2)) 262 $r2 $false)
}

# ================== 插画素材 ==================
$bayer = @(@(0, 8, 2, 10), @(12, 4, 14, 6), @(3, 11, 1, 9), @(15, 7, 13, 5))
function New-Mono([int]$w, [int]$h) {
    $st = [int][Math]::Ceiling($w / 8)
    $buf = [byte[]]::new($st * $h)
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $r = [Math]::Sqrt((($x - $w * 0.72) * ($x - $w * 0.72)) + (($y - $h * 0.30) * ($y - $h * 0.30)))
            $v = 225 - ($y * 0.45)
            if ($r -lt $w * 0.09) { $v = 250 } elseif ($r -lt $w * 0.12) { $v = 190 }
            $m1 = $h * 0.52 - [Math]::Abs($x - $w * 0.25) * 0.5
            if ($y -gt $m1) { $v = 95 }
            if ($y -gt $h * 0.74) { $v = 60 + (($x * 5) % 28) }
            if ($v -lt (($bayer[$y % 4][$x % 4] + 0.5) / 16 * 255)) {
                $buf[$y * $st + [int][Math]::Floor($x / 8)] = [byte]($buf[$y * $st + [int][Math]::Floor($x / 8)] -bor [byte](0x80 -shr ($x % 8)))
            }
        }
    }
    return $buf
}
function New-MonoFromPhoto([string]$path, [int]$w, [int]$h) {
    $src = [System.Drawing.Bitmap]::FromFile($path)
    $tmp = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($tmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $tr = $w / $h; $sr = $src.Width / $src.Height
    if ($sr -gt $tr) { $cw = [int]($src.Height * $tr); $ch = $src.Height; $ox = [int](($src.Width - $cw) / 2); $oy = 0 }
    else { $cw = $src.Width; $ch = [int]($src.Width / $tr); $ox = 0; $oy = [int](($src.Height - $ch) / 2) }
    $crop = $src.Clone((New-Object System.Drawing.Rectangle $ox, $oy, $cw, $ch), $src.PixelFormat)
    $g.DrawImage($crop, 0, 0, $w, $h)
    $g.Dispose(); $crop.Dispose(); $src.Dispose()

    $rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
    $d = $tmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $stride = $d.Stride
    $px = [byte[]]::new($stride * $h)
    [System.Runtime.InteropServices.Marshal]::Copy($d.Scan0, $px, 0, $px.Length)
    $tmp.UnlockBits($d); $tmp.Dispose()

    $st = [int][Math]::Ceiling($w / 8)
    $buf = [byte[]]::new($st * $h)
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $p = $y * $stride + $x * 3
            $lum = 0.299 * $px[$p + 2] + 0.587 * $px[$p + 1] + 0.114 * $px[$p]
            if ($lum -lt (($bayer[$y % 4][$x % 4] + 0.5) / 16 * 255)) {
                $buf[$y * $st + [int][Math]::Floor($x / 8)] = [byte]($buf[$y * $st + [int][Math]::Floor($x / 8)] -bor [byte](0x80 -shr ($x % 8)))
            }
        }
    }
    return $buf
}

if ($Photo -and (Test-Path $Photo)) {
    Write-Host "用照片：$Photo"
    $script:IMG_FULL = New-MonoFromPhoto $Photo 400 300
    $script:IMG_PANEL = New-MonoFromPhoto $Photo 134 200
}
else {
    Write-Host '用内置合成插画'
    $script:IMG_FULL = New-Mono 400 300
    $script:IMG_PANEL = New-Mono 134 200
}

# ================== 逐个状态出图 ==================
$states = @(
    @{ L = '① IDLE · 每日一言';      F = { $script:POOL = @(@{kind = $UI_PANEL_QUOTE; title = ''; body = $DATA.Quote }); $script:POOL_CUR = 0; $script:ART_MODE = 0; Ui-RenderIdle } },
    @{ L = '② IDLE · 新闻';          F = { $script:POOL = @(@{kind = $UI_PANEL_NEWS; title = $DATA.NewsTitle; body = $DATA.NewsBody }); $script:POOL_CUR = 0; $script:ART_MODE = 0; Ui-RenderIdle } },
    @{ L = '③ IDLE · 插画(小窗)';    F = { $script:POOL = @(@{kind = $UI_PANEL_IMAGE; title = ''; body = '' }); $script:POOL_CUR = 0; $script:ART_MODE = 1; Ui-RenderIdle } },
    @{ L = '④ IDLE · 全屏插画 · 实心'; F = { $script:ART_MODE = 0; $script:BAR_MODE = $UI_BAR_SOLID; Ui-RenderIdle } },
    @{ L = '⑤ IDLE · 全屏插画 · 反色'; F = { $script:BAR_MODE = $UI_BAR_INVERT; Ui-RenderIdle } },
    @{ L = '⑥ IDLE · 全屏插画 · 自适应'; F = { $script:BAR_MODE = $UI_BAR_INVERT_AUTO; Ui-RenderIdle } },
    @{ L = '⑦ IDLE · 内容池为空';    F = { $script:POOL = @(); $script:POOL_CUR = 0; $script:ART_MODE = 0; Ui-RenderIdle } },
    @{ L = '⑧ MENU · 光标在"配置"';  F = { $script:POOL = @(@{kind = $UI_PANEL_QUOTE; title = ''; body = $DATA.Quote }); $script:ART_MODE = 0; Ui-RenderMenu 1 } },
    @{ L = '⑨ CONFIG · 信息条=反色自适应'; F = { Ui-RenderConfig $UI_CFG_BAR_MODE 2 } },
    @{ L = '⑩ TODO_NAV · 光标第2条'; F = { $script:ART_MODE = 0; Ui-RenderIdle; Draw-Todo 1 $false } },
    @{ L = '⑪ TODO_EDIT · 编辑第3条'; F = { Ui-RenderIdle; Draw-Todo 2 $true } },
    @{ L = '⑫ POMO · 番茄钟';        F = { Ui-RenderIdle; Draw-Pomo 1477 } }
)

for ($i = 0; $i -lt $states.Count; $i++) {
    if ($i -ge $COLS * $ROWS) { break }
    $col = $i % $COLS; $row = [int]($i / $COLS)
    $script:OX = $MARGIN + $col * $CELL_STEP_X
    $script:OY = $MARGIN + $LBL_H + $row * $CELL_STEP_Y
    # 清该格
    for ($y = 0; $y -lt $EINK_H; $y++) { for ($x = 0; $x -lt $EINK_W; $x++) { Set-Px $x $y $false } }
    & $states[$i].F
    # 标签
    $script:OY = $MARGIN + $row * $CELL_STEP_Y
    [void](Font-DrawText ($script:OX + 2) 3 $states[$i].L $false)
}

# ================== 输出 ==================
function Save-Canvas([string]$path, [int]$scale) {
    $bmp = New-Object System.Drawing.Bitmap $CANVAS_W, $CANVAS_H, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $rect = New-Object System.Drawing.Rectangle 0, 0, $CANVAS_W, $CANVAS_H
    $d = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $bmp.PixelFormat)
    $stride = $d.Stride
    $row = [byte[]]::new($stride)
    for ($y = 0; $y -lt $CANVAS_H; $y++) {
        $p = 0
        for ($x = 0; $x -lt $CANVAS_W; $x++) {
            $bit = ($script:CANVAS[$y * $CANVAS_STRIDE + [int][Math]::Floor($x / 8)] -shr (7 - ($x % 8))) -band 1
            $c = [byte]$(if ($bit -ne 0) { 30 } else { 244 })
            $row[$p] = $c; $row[$p + 1] = $c; $row[$p + 2] = $c; $p += 3
        }
        [System.Runtime.InteropServices.Marshal]::Copy($row, 0, [IntPtr]($d.Scan0.ToInt64() + $y * $stride), $stride)
    }
    $bmp.UnlockBits($d)
    if ($scale -gt 1) {
        $big = New-Object System.Drawing.Bitmap ($CANVAS_W * $scale), ($CANVAS_H * $scale)
        $g = [System.Drawing.Graphics]::FromImage($big)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
        $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
        $g.DrawImage($bmp, 0, 0, ($CANVAS_W * $scale), ($CANVAS_H * $scale))
        $g.Dispose()
        $bmp.Dispose()
        $big.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
        $big.Dispose()
    }
    else {
        $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
    }
}

if ([string]::IsNullOrWhiteSpace($Out)) { $Out = Join-Path (Get-Location) 'states.png' }
Save-Canvas $Out $Scale
Write-Host ("已输出 {0}  ({1}x{2} 逻辑，{3}x 放大)" -f $Out, $CANVAS_W, $CANVAS_H, $Scale)
