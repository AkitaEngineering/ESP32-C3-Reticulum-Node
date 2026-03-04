param(
    [Parameter(Mandatory=$true)][string]$Device,
    [Parameter(Mandatory=$true)][int]$Port,
    [Parameter(Mandatory=$true)][string]$Token,
    [Parameter(Mandatory=$true)][string]$ConfigFile
)

if (-not (Test-Path $ConfigFile)) {
    Write-Error "Config file not found: $ConfigFile"
    exit 2
}

$json = Get-Content -Raw $ConfigFile
Invoke-RestMethod -Uri "http://$Device`:$Port/api/v1/config" -Method Post -Body $json -Headers @{ Authorization = "Bearer $Token" } -ContentType 'application/json'
Write-Host "Provisioning request sent to $Device:$Port"
