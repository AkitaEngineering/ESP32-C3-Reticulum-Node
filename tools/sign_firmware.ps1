param(
    [Parameter(Mandatory=$true)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [Parameter(Mandatory=$true)][string]$FirmwareFile,
    [Parameter(Mandatory=$true)][string]$PrivateKeyFile,
    [Parameter(Mandatory=$true)][string]$SignatureFile
)

if (-not (Test-Path $FirmwareFile)) {
    Write-Error "Firmware file not found: $FirmwareFile"
    exit 2
}
if (-not (Test-Path $PrivateKeyFile)) {
    Write-Error "Private key file not found: $PrivateKeyFile"
    exit 3
}
if (-not (Get-Command openssl -ErrorAction SilentlyContinue)) {
    Write-Error "openssl is required"
    exit 4
}

$tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("rns-sign-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tmpDir | Out-Null

try {
    $digestFile = Join-Path $tmpDir "firmware.sha512.bin"
    $signedInputFile = Join-Path $tmpDir "firmware.signed-input.bin"
    $sigBin = Join-Path $tmpDir "signature.bin"

    $output = [System.IO.File]::OpenWrite($signedInputFile)
    try {
        $prefix = [System.Text.Encoding]::ASCII.GetBytes("RNS-OTA-V1`0$Version`0")
        $output.Write($prefix, 0, $prefix.Length)
        $input = [System.IO.File]::OpenRead($FirmwareFile)
        try { $input.CopyTo($output) } finally { $input.Dispose() }
    }
    finally { $output.Dispose() }
    openssl dgst -sha512 -binary -out $digestFile $signedInputFile
    openssl pkeyutl -sign -rawin -inkey $PrivateKeyFile -in $digestFile -out $sigBin

    $sig = [System.BitConverter]::ToString([System.IO.File]::ReadAllBytes($sigBin)).Replace("-", "").ToLowerInvariant()
    if ($sig.Length -ne 128) {
        Write-Error "Unexpected signature length: $($sig.Length) hex characters"
        exit 5
    }

    Set-Content -NoNewline -Path $SignatureFile -Value $sig
    Write-Host "Wrote OTA signature to $SignatureFile"
}
finally {
    Remove-Item -Recurse -Force $tmpDir -ErrorAction SilentlyContinue
}
