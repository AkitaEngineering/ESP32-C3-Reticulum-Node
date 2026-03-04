try {
  $p = New-Object System.IO.Ports.SerialPort 'COM16',115200
  $p.Open()
  Write-Host 'PORT_FREE'
  $p.Close()
} catch {
  Write-Host 'PORT_BUSY'
  Write-Host $_.Exception.Message
}
