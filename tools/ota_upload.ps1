# Upload a signed firmware binary to the device OTA endpoint.
# SignatureFile must contain Ed25519(SHA-512(firmware.bin)); use sign_firmware.ps1.
param(
    [Parameter(Mandatory=$true)][string]$Device,
    [Parameter(Mandatory=$true)][int]$Port,
    [Parameter(Mandatory=$true)][string]$Token,
    [Parameter(Mandatory=$true)][string]$FirmwareFile,
    [Parameter(Mandatory=$true)][string]$SignatureFile
)

if (-not (Test-Path $FirmwareFile)) {
    Write-Error "Firmware file not found: $FirmwareFile"
    exit 2
}
if (-not (Test-Path $SignatureFile)) {
    Write-Error "Signature file not found: $SignatureFile"
    exit 3
}

$sig = (Get-Content -Raw $SignatureFile).Trim()

Invoke-RestMethod -Uri "http://$Device`:$Port/api/v1/ota" -Method Post -InFile $FirmwareFile -Headers @{ Authorization = "Bearer $Token"; 'X-Signature-Ed25519' = $sig } -ContentType 'application/octet-stream'
Write-Host "OTA upload attempted to $Device:$Port"
