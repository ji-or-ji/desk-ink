Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

$outDir = Split-Path $PSScriptRoot -Parent   # 输出到 software/font/
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$pfc = New-Object System.Drawing.Text.PrivateFontCollection
$pfc.AddFontFile('C:\Windows\Fonts\simsun.ttc')
$fam = $pfc.Families[0]
$font = New-Object System.Drawing.Font -ArgumentList $fam, 16, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
$flags = [System.Windows.Forms.TextFormatFlags]::NoPadding -bor [System.Windows.Forms.TextFormatFlags]::NoPrefix -bor [System.Windows.Forms.TextFormatFlags]::SingleLine

$CS = 16
$bmp = New-Object System.Drawing.Bitmap -ArgumentList $CS, $CS, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
$rect = New-Object System.Drawing.Rectangle -ArgumentList 0, 0, $CS, $CS
$pt = New-Object System.Drawing.Point(0, 0)

# 96 个字符 (0x20..0x7F)，每个 16 字节（8 列 × 16 行）
$N = 96
$data = New-Object byte[] ($N * 16)

for ($c = 0x20; $c -le 0x7F; $c++) {
    $ch = [string][char]$c
    $g.Clear([System.Drawing.Color]::White)
    if ($c -ne 0x20) {
        [System.Windows.Forms.TextRenderer]::DrawText($g, $ch, $font, $pt, [System.Drawing.Color]::Black, $flags)
    }
    $d = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $d.Stride
    $px = New-Object byte[] ($stride * $CS)
    [System.Runtime.InteropServices.Marshal]::Copy($d.Scan0, $px, 0, $px.Length)
    $bmp.UnlockBits($d)

    $base = ($c - 0x20) * 16
    for ($y = 0; $y -lt 16; $y++) {
        $b = 0
        for ($x = 0; $x -lt 8; $x++) {
            if ($px[$y * $stride + $x * 4] -lt 128) { $b = $b -bor (1 -shl (7 - $x)) }
        }
        $data[$base + $y] = [byte]$b
    }
}
$g.Dispose(); $bmp.Dispose()

# 输出 bin
$outBin = Join-Path $outDir 'ascii8x16.bin'
[System.IO.File]::WriteAllBytes($outBin, $data)
Write-Output ("ascii bin = " + (Get-Item $outBin).Length + " bytes -> " + $outBin)

# 顺便输出几个字符的 ASCII art 供检查
function DumpChar([int]$code) {
    $base = ($code - 0x20) * 16
    Write-Output ("--- " + [string][char]$code + " (0x" + $code.ToString('X2') + ") ---")
    for ($y = 0; $y -lt 16; $y++) {
        $b = $data[$base + $y]
        $line = ''
        for ($x = 0; $x -lt 8; $x++) {
            if (($b -band (1 -shl (7 - $x))) -ne 0) { $line += '#' } else { $line += '.' }
        }
        Write-Output $line
    }
}
DumpChar 0x41   # A
DumpChar 0x30   # 0
DumpChar 0x3A   # :
