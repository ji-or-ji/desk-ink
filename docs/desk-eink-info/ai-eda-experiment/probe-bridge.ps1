$ErrorActionPreference = 'Continue'
$node = 'C:\Users\<user>\AppData\Local\Temp\opencode\node\node-v22.20.0-win-x64\node.exe'
$skill = 'C:\Users\<user>\.config\opencode\skills\easyeda-api'
$logdir = '<工作目录>\desk-ink\docs\desk-eink-info\ai-eda-experiment\logs'
New-Item -ItemType Directory -Force -Path $logdir | Out-Null
$out = Join-Path $logdir 'bridge.out.log'
$err = Join-Path $logdir 'bridge.err.log'
Remove-Item $out, $err -ErrorAction SilentlyContinue

$p = Start-Process -FilePath $node -ArgumentList 'scripts/bridge-server.mjs' -WorkingDirectory $skill -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
Write-Output "bridge_pid=$($p.Id)"
Start-Sleep -Seconds 3

Write-Output '=== STDOUT ==='
Get-Content $out -ErrorAction SilentlyContinue
Write-Output '=== STDERR ==='
Get-Content $err -ErrorAction SilentlyContinue

$port = $null
foreach ($pp in 49620..49629) {
  try {
    $r = Invoke-WebRequest -Uri "http://127.0.0.1:$pp/health" -TimeoutSec 2 -UseBasicParsing
    if ($r.Content -match 'easyeda-bridge') { $port = $pp; Write-Output "HEALTH[$pp]=$($r.Content)"; break }
  } catch {}
}
Write-Output "bridge_port=$port"

if ($port) {
  Write-Output '=== GET /eda-windows ==='
  try { (Invoke-WebRequest -Uri "http://127.0.0.1:$port/eda-windows" -TimeoutSec 5 -UseBasicParsing).Content } catch { Write-Output "ERR $($_.Exception.Message)" }

  Write-Output '=== POST /execute (return 1+1) ==='
  try {
    $body = '{"code":"return 1+1;"}'
    $resp = Invoke-WebRequest -Uri "http://127.0.0.1:$port/execute" -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 10 -UseBasicParsing
    Write-Output "HTTP $($resp.StatusCode)"
    Write-Output $resp.Content
  } catch {
    Write-Output "EXCEPTION: $($_.Exception.Message)"
    if ($_.Exception.Response) {
      $sr = New-Object System.IO.StreamReader($_.Exception.Response.GetResponseStream())
      Write-Output ("BODY: " + $sr.ReadToEnd())
      Write-Output ("STATUS: " + [int]$_.Exception.Response.StatusCode)
    }
  }
}

if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Write-Output 'done'
