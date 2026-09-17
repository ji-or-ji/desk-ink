Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

$outDir = Split-Path $PSScriptRoot -Parent   # 输出到 software/font/
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

# ---------- 1. GB2312 一级汉字表 ----------
$enc = [System.Text.Encoding]::GetEncoding('gb2312')
$list = New-Object 'System.Collections.Generic.List[string]'
for ($q = 1; $q -le 55; $q++) {
    if ($q -ge 3 -and $q -le 15) { continue }   # 1-2 区符号，16-55 区一级汉字
    $maxW = if ($q -eq 55) { 89 } else { 94 }
    for ($w = 1; $w -le $maxW; $w++) {
        $b = [byte[]]@([byte]($q + 0xA0), [byte]($w + 0xA0))
        $s = $enc.GetString($b)
        if ($s.Length -eq 1) { $list.Add($s) }
    }
}
$set = New-Object 'System.Collections.Generic.HashSet[string]'
foreach ($c in $list) { [void]$set.Add($c) }
$sorted = @($set) | Sort-Object { [int][char]$_ }
$n = $sorted.Count
Write-Output ("字符总数（含标点）= " + $n)
Write-Output ("首字 = " + $sorted[0] + "   末字 = " + $sorted[$n-1])

# ---------- 2. 逐字取模 ----------
$pfc = New-Object System.Drawing.Text.PrivateFontCollection
$pfc.AddFontFile('C:\Windows\Fonts\simsun.ttc')
$fam = $pfc.Families[0]
$font = New-Object System.Drawing.Font -ArgumentList $fam, 16, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
$flags = [System.Windows.Forms.TextFormatFlags]::NoPadding -bor [System.Windows.Forms.TextFormatFlags]::NoPrefix -bor [System.Windows.Forms.TextFormatFlags]::SingleLine

$CS = 20
$bmp = New-Object System.Drawing.Bitmap -ArgumentList $CS, $CS, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit

$bitmaps = New-Object byte[] ($n * 32)
$codes = New-Object uint16[] $n
$rect = New-Object System.Drawing.Rectangle -ArgumentList 0, 0, $CS, $CS
$pt = New-Object System.Drawing.Point(0, 0)
$blackColor = [System.Drawing.Color]::Black
$whiteColor = [System.Drawing.Color]::White

$i = 0
foreach ($ch in $sorted) {
    $g.Clear($whiteColor)
    [System.Windows.Forms.TextRenderer]::DrawText($g, $ch, $font, $pt, $blackColor, $flags)

    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $data.Stride
    $px = New-Object byte[] ($stride * $CS)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $px, 0, $px.Length)
    $bmp.UnlockBits($data)

    $codes[$i] = [uint16][char]$ch
    $base = $i * 32
    for ($y = 0; $y -lt 16; $y++) {
        $lo = 0; $hi = 0
        $rowBase = $y * $stride
        for ($x = 0; $x -lt 16; $x++) {
            if ($px[$rowBase + $x * 4] -lt 128) {
                if ($x -lt 8) { $lo = $lo -bor (1 -shl (7 - $x)) }
                else { $hi = $hi -bor (1 -shl (15 - $x)) }
            }
        }
        $bitmaps[$base + $y * 2] = [byte]$lo
        $bitmaps[$base + $y * 2 + 1] = [byte]$hi
    }
    $i++
    if (($i % 1000) -eq 0) { Write-Output ("  ... " + $i + " / " + $n) }
}

$g.Dispose(); $bmp.Dispose()

# ---------- 3. 输出 bin ----------
$codeBytes = New-Object byte[] ($n * 2)
[System.Buffer]::BlockCopy($codes, 0, $codeBytes, 0, $n * 2)

$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($ms)
$bw.Write([byte[]]@(0x46, 0x31, 0x36, 0x42))   # magic 'F16B'
$bw.Write([uint16]$n)
$bw.Write($codeBytes)
$bw.Write($bitmaps)
$bw.Flush()
$outBin = Join-Path $outDir 'font16_simsun.bin'
[System.IO.File]::WriteAllBytes($outBin, $ms.ToArray())
$bw.Dispose(); $ms.Dispose()

$len = (Get-Item $outBin).Length
Write-Output ("字符数 = " + $n)
Write-Output ("bin 大小 = " + $len + " 字节 (" + [int]($len / 1024) + " KB)")
Write-Output ("输出 = " + $outBin)
