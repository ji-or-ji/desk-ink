param(
    [Parameter(Mandatory=$true)][string]$Path,
    [double]$X = 0.0, [double]$Y = 0.0,
    [double]$W = 1.0, [double]$H = 1.0,
    [double]$Scale = 3.0,
    [string]$Language = 'en-US',
    [string]$OutDir
)

# Read a region of a scanned datasheet page with the Windows built-in OCR engine.
#
# USE THIS WHEN: the vendor manual only exists as page images (jpg/png), and the
# tool at hand cannot read images directly.
#
#   .\ocr.ps1 -Path ..\..\docs\desk-eink-info\depg0420-docs\<page>.jpg `
#       -X 0.42 -Y 0.10 -W 0.56 -H 0.82
#
# THREE GOTCHAS, all learned the hard way:
#
#   1. THIS MUST RUN UNDER Windows PowerShell 5.1 (powershell.exe), NOT pwsh 7.
#      pwsh 7 has no WinRT projection, so [Windows.Media.Ocr.OcrEngine] will not
#      load and the script dies immediately. From pwsh 7, call it like:
#         powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\ocr.ps1 ...
#
#   2. KEEP THIS FILE ASCII-ONLY. PowerShell 5.1 reads .ps1 as ANSI, so non-ASCII
#      characters here corrupt string literals and the file fails to parse.
#
#   3. The temp image must live where WinRT can see it. The system %TEMP% is NOT
#      readable from some sandboxed hosts, and OCR then returns zero lines with no
#      error at all. Default is this script's own folder; override with -OutDir.
#
# WHY CROP: on a full dense table the engine reads cell by cell and returns
# isolated single characters ("0 0 0 1 o I O"). Cropping to one column or one row
# and upscaling 3x fixes it. Use X/Y/W/H to pick the region; the crop is saved so
# you can look at exactly what was fed to the engine.

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime | Out-Null
Add-Type -AssemblyName System.Drawing | Out-Null

# WinRT async calls are not awaitable in PowerShell; wrap IAsyncOperation<T>.
$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
})[0]
function Await($op, $type) {
    $m = $asTaskGeneric.MakeGenericMethod($type)
    $t = $m.Invoke($null, @($op)); $t.Wait(-1) | Out-Null; $t.Result
}

# Every WinRT type has to be loaded explicitly before New-Object can see it.
[Windows.Storage.StorageFile,            Windows.Foundation, ContentType=WindowsRuntime] | Out-Null
[Windows.Graphics.Imaging.BitmapDecoder, Windows.Foundation, ContentType=WindowsRuntime] | Out-Null
[Windows.Media.Ocr.OcrEngine,            Windows.Foundation, ContentType=WindowsRuntime] | Out-Null
[Windows.Globalization.Language,         Windows.Foundation, ContentType=WindowsRuntime] | Out-Null

$engine = $null
if ($Language) {
    $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage(
        (New-Object Windows.Globalization.Language -ArgumentList $Language))
}
if (-not $engine) { $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages() }
if (-not $engine) { throw 'No OCR recognizer available on this machine.' }

if (-not $OutDir) { $OutDir = $PSScriptRoot }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Force -Path $OutDir | Out-Null }
$tmp = Join-Path $OutDir 'ocr-region.png'

$img = [System.Drawing.Image]::FromFile($Path)
$pw = $img.Width; $ph = $img.Height
$sx = [int]($pw * $X); $sy = [int]($ph * $Y)
$sw = [int]($pw * $W); $sh = [int]($ph * $H)
$dw = [int]($sw * $Scale); $dh = [int]($sh * $Scale)

$bmp = New-Object System.Drawing.Bitmap $dw, $dh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.DrawImage($img,
    (New-Object System.Drawing.Rectangle 0, 0, $dw, $dh),
    (New-Object System.Drawing.Rectangle $sx, $sy, $sw, $sh), 'Pixel')
$g.Dispose(); $img.Dispose()
$bmp.Save($tmp, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()

Write-Output ("### source: {0}  ({1}x{2})" -f (Split-Path $Path -Leaf), $pw, $ph)
Write-Output ("### crop:   [{0},{1},{2},{3}]  ->  {4}x{5}  saved to {6}" -f $X, $Y, $W, $H, $dw, $dh, $tmp)

$f  = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($tmp)) ([Windows.Storage.StorageFile])
$s  = Await ($f.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
$d  = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($s)) ([Windows.Graphics.Imaging.BitmapDecoder])
$sb = Await ($d.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
$r  = Await ($engine.RecognizeAsync($sb)) ([Windows.Media.Ocr.OcrResult])

if ($r.Lines.Count -eq 0) {
    Write-Output '(no text recognized -- blank region, or OutDir is not readable by WinRT)'
}
foreach ($line in $r.Lines) { Write-Output ('  ' + $line.Text) }
