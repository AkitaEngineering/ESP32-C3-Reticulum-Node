$portName = 'COM16'
$baud = 115200
$outFile = Join-Path $PSScriptRoot 'boot.log'

try {
  $p = New-Object System.IO.Ports.SerialPort $portName, $baud, 'None', 8, 'One'
  $p.NewLine = "`n"
  $p.ReadTimeout = 500
  $p.Open()
} catch {
  Write-Host "Failed to open $portName : $($_.Exception.Message)"
  exit 2
}

# Toggle lines to try to reset the board (works for many devboards)
try {
  $p.DtrEnable = $false
  $p.RtsEnable = $true
  Start-Sleep -Milliseconds 120
  $p.RtsEnable = $false
  Start-Sleep -Milliseconds 120
} catch {
  Write-Host "Warning toggling DTR/RTS: $($_.Exception.Message)"
}

$sw = [Diagnostics.Stopwatch]::StartNew()
$buf = New-Object System.Text.StringBuilder
$timeoutSec = 10
Write-Host "Capturing boot output for $timeoutSec seconds..."
while ($sw.Elapsed.TotalSeconds -lt $timeoutSec) {
  try {
    $line = $p.ReadLine()
    [void]$buf.AppendLine($line)
    Write-Host $line
  } catch {
    Start-Sleep -Milliseconds 50
  }
}

$p.Close()

Set-Content -LiteralPath $outFile -Value $buf.ToString()
Write-Host "Saved boot log to $outFile"
