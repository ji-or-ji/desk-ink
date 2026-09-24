param(
  [Parameter(Mandatory=$true)][string]$Code,
  [string]$Label = 'exec',
  [string]$Port = '49620'
)
$ErrorActionPreference = 'Continue'
$logdir = '<工作目录>\desk-ink\docs\desk-eink-info\ai-eda-experiment\logs'
New-Item -ItemType Directory -Force -Path $logdir | Out-Null
$log = Join-Path $logdir 'eda-exec.log'

$payload = @{ code = $Code } | ConvertTo-Json -Compress
$stamp = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')
Add-Content -Path $log -Value "`n===== [$stamp] $Label ====="
Add-Content -Path $log -Value "CODE: $Code"

try {
  $r = Invoke-WebRequest -Uri "http://127.0.0.1:$Port/execute" -Method Post -ContentType 'application/json' -Body $payload -TimeoutSec 35 -UseBasicParsing -SkipHttpErrorCheck
  $raw = $r.RawContentStream.ToArray()
  $text = [System.Text.Encoding]::UTF8.GetString($raw)
  Write-Output "HTTP $($r.StatusCode)"
  Write-Output $text
  Add-Content -Path $log -Value "HTTP $($r.StatusCode)"
  Add-Content -Path $log -Value "RESP: $text"
} catch {
  Write-Output "EXCEPTION: $($_.Exception.Message)"
  Add-Content -Path $log -Value "EXCEPTION: $($_.Exception.Message)"
}
