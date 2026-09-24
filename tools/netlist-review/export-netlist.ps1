param(
  [Parameter(Mandatory=$true)][string]$Code,
  [Parameter(Mandatory=$true)][string]$OutFile,
  [string]$Port = '49620'
)
$ErrorActionPreference = 'Continue'

# 解码 Bridge 返回的原始字节，与 eda-exec.ps1 同一套逻辑。
# 现状：Bridge(HTTP /execute) 的正文实测是 UTF-8；但部分中文错误消息可能来自
# GBK/GB18030 编码的字节，若一律按 UTF-8 解会得到 U+FFFD 乱码。
# 做法：严格 UTF-8 优先，遇到非法字节序列再回退 GB18030。
# 注意：不要一律用 936/GB18030 解，那会把正常 UTF-8 的中文解成乱码
#（旧版本这里用“出现 U+FFFD 就整段按 936 重解”的启发式，容易误判，已换掉）。
function ConvertFrom-BridgeBytes([byte[]]$Bytes) {
  $strictUtf8 = New-Object System.Text.UTF8Encoding($false, $true)
  try { return $strictUtf8.GetString($Bytes) }
  catch { return [System.Text.Encoding]::GetEncoding('GB18030').GetString($Bytes) }
}

$payload = @{ code = $Code } | ConvertTo-Json -Compress
try {
  $r = Invoke-WebRequest -Uri "http://127.0.0.1:$Port/execute" -Method Post -ContentType 'application/json' -Body $payload -TimeoutSec 90 -UseBasicParsing -SkipHttpErrorCheck
  $bytes = $r.RawContentStream.ToArray()
  $text = ConvertFrom-BridgeBytes $bytes
  $obj = $text | ConvertFrom-Json
  if ($obj.success) {
    $res = $obj.result
    if ($res -is [string]) { $content = $res }
    else { $content = ($res | ConvertTo-Json -Depth 100) }
    [System.IO.File]::WriteAllText($OutFile, $content, (New-Object System.Text.UTF8Encoding($false)))
    Write-Output "OK $OutFile bytes=$((Get-Item $OutFile).Length)"
  } else {
    Write-Output "ERR $($obj.error)"
  }
} catch {
  Write-Output "EXCEPTION $($_.Exception.Message)"
}
