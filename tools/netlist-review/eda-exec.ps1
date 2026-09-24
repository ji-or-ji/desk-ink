param(
  [Parameter(Mandatory=$true)][string]$Code,
  [string]$Label = 'exec',
  [string]$Port = '49620'
)
$ErrorActionPreference = 'Continue'

# 解码 Bridge 返回的原始字节。
# 现状：Bridge(HTTP /execute) 的正文实测是 UTF-8；但部分中文错误消息可能来自
# GBK/GB18030 编码的字节，若一律按 UTF-8 解会得到 U+FFFD 乱码。
# 做法：先用“严格” UTF-8 解码（遇到非法字节序列会抛异常），失败再回退 GB18030，
# 这样 UTF-8 与 GBK 两类来源都能正确解出。
# 注意：不要一律用 GB18030 解，那会把正常 UTF-8 的中文解成乱码。
function ConvertFrom-BridgeBytes([byte[]]$Bytes) {
  $strictUtf8 = New-Object System.Text.UTF8Encoding($false, $true)
  try { return $strictUtf8.GetString($Bytes) }
  catch { return [System.Text.Encoding]::GetEncoding('GB18030').GetString($Bytes) }
}

$logdir = Join-Path $PSScriptRoot 'logs'
New-Item -ItemType Directory -Force -Path $logdir | Out-Null
$log = Join-Path $logdir 'eda-exec.log'

$payload = @{ code = $Code } | ConvertTo-Json -Compress
$stamp = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')
Add-Content -Path $log -Value "`n===== [$stamp] $Label =====" -Encoding utf8
Add-Content -Path $log -Value "CODE: $Code" -Encoding utf8

try {
  $r = Invoke-WebRequest -Uri "http://127.0.0.1:$Port/execute" -Method Post -ContentType 'application/json' -Body $payload -TimeoutSec 35 -UseBasicParsing -SkipHttpErrorCheck
  $raw = $r.RawContentStream.ToArray()
  $text = ConvertFrom-BridgeBytes $raw
  Write-Output "HTTP $($r.StatusCode)"
  Write-Output $text
  Add-Content -Path $log -Value "HTTP $($r.StatusCode)" -Encoding utf8
  Add-Content -Path $log -Value "RESP: $text" -Encoding utf8
} catch {
  Write-Output "EXCEPTION: $($_.Exception.Message)"
  Add-Content -Path $log -Value "EXCEPTION: $($_.Exception.Message)" -Encoding utf8
}
