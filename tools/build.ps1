param(
    [Parameter(Mandatory=$false)][string]$Env = 'esp32-c3-web'
)

if ($Env -eq 'all') {
    pio run -e esp32-c3-web -v
    pio run -e ttgo-minimal -v
} else {
    pio run -e $Env -v
}
