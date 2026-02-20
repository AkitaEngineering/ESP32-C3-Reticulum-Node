$portName = 'COM16'
$baud = 115200
$outFile = Join-Path $PSScriptRoot 'boot_manual.log'

try {
  $p = New-Object System.IO.Ports.SerialPort $portName, $baud, 'None', 8, 'One'
  $p.NewLine = "`n"
  $p.ReadTimeout = 200
  $p.Open()
} catch {
  Write-Host "Failed to open $portName : $($_.Exception.Message)"
  exit 2
}

Write-Host "Opened $portName at $baud. Press the board 'EN' (reset) button now to capture boot output."
$sw = [Diagnostics.Stopwatch]::StartNew()
$buf = New-Object System.Text.StringBuilder
$timeoutSec = 15
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
