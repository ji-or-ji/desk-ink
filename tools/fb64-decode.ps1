<#
.SYNOPSIS
  把固件串口日志里的 FB64 块还原成 400×300 的 PNG。

.DESCRIPTION
  配合固件的 APP_QEMU_DUMP_B64 / APP_QEMU_TOUR：
  日志里形如
    FB64 BEGIN 15000 <label>
    <每行 200 字符的 base64>
    FB64 END
  本脚本按块解码成 1-bit 位图（1=黑）并输出 PNG。

.EXAMPLE
  .\fb64-decode.ps1 -Log ..\..\qemu-log.txt -OutDir .\fb64
#>
param(
    [Parameter(Mandatory = $true)][string]$Log,
    [string]$OutDir = '',
    [int]$Scale = 2
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$W = 400; $H = 300; $STRIDE = 50

if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path (Split-Path -Parent $Log) 'fb64'
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$lines = Get-Content -LiteralPath $Log
$idx = 0
$i = 0
while ($i -lt $lines.Count) {
    $line = $lines[$i]
    if ($line -match 'FB64 BEGIN (\d+)\s*(.*)$') {
        $len = [int]$Matches[1]
        $label = $Matches[2].Trim()
        $i++
        $sb = New-Object System.Text.StringBuilder
        while ($i -lt $lines.Count -and $lines[$i] -notmatch 'FB64 END') {
            [void]$sb.Append($lines[$i].Trim())
            $i++
        }
        $bytes = [Convert]::FromBase64String($sb.ToString())
        if ($bytes.Length -ne $len) {
            Write-Warning ("block {0} decoded {1} bytes, expected {2}" -f $idx, $bytes.Length, $len)
        }

        $bmp = New-Object System.Drawing.Bitmap $W, $H, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
        $rect = New-Object System.Drawing.Rectangle 0, 0, $W, $H
        $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $bmp.PixelFormat)
        $rowStride = $data.Stride
        $row = New-Object byte[] $rowStride
        for ($y = 0; $y -lt $H; $y++) {
            $p = 0
            for ($x = 0; $x -lt $W; $x++) {
                $bi = $y * $STRIDE + [int][Math]::Floor($x / 8)
                $on = ($bytes[$bi] -band (0x80 -shr ($x % 8))) -ne 0
                $c = if ($on) { [byte]30 } else { [byte]244 }
                $row[$p] = $c; $row[$p + 1] = $c; $row[$p + 2] = $c; $p += 3
            }
            [System.Runtime.InteropServices.Marshal]::Copy($row, 0, [IntPtr]($data.Scan0.ToInt64() + $y * $rowStride), $rowStride)
        }
        $bmp.UnlockBits($data)

        $safe = if ($label) { ($label -replace '[^A-Za-z0-9\-]', '_') } else { 'frame' }
        $name = '{0:D2}-{1}.png' -f $idx, $safe
        $out = Join-Path $OutDir $name
        if ($Scale -le 1) {
            $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
        } else {
            $big = New-Object System.Drawing.Bitmap ($W * $Scale), ($H * $Scale)
            $g = [System.Drawing.Graphics]::FromImage($big)
            $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
            $g.DrawImage($bmp, 0, 0, $W * $Scale, $H * $Scale)
            $g.Dispose()
            $big.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
            $big.Dispose()
        }
        $bmp.Dispose()
        Write-Output ("saved {0}  ({1})" -f $out, $label)
        $idx++
    }
    $i++
}
Write-Output ("total {0} frame(s) -> {1}" -f $idx, $OutDir)
