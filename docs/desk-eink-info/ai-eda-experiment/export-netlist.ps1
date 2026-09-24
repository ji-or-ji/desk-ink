param(
  [Parameter(Mandatory=$true)][string]$Code,
  [Parameter(Mandatory=$true)][string]$OutFile,
  [string]$Port = '49620'
)
$ErrorActionPreference = 'Continue'
$payload = @{ code = $Code } | ConvertTo-Json -Compress
try {
  $r = Invoke-WebRequest -Uri "http://127.0.0.1:$Port/execute" -Method Post -ContentType 'application/json' -Body $payload -TimeoutSec 90 -UseBasicParsing -SkipHttpErrorCheck
  $bytes = $r.RawContentStream.ToArray()
  $utf8 = [System.Text.Encoding]::UTF8.GetString($bytes)
  if ($utf8.IndexOf([char]0xFFFD) -ge 0) {
    $text = [System.Text.Encoding]::GetEncoding(936).GetString($bytes)
  } else {
    $text = $utf8
  }
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
