param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$PioArgs
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$localCoreDir = Join-Path $repoRoot ".pio-core-build"
if (-not $env:PLATFORMIO_CORE_DIR -and (Test-Path -LiteralPath $localCoreDir)) {
    $env:PLATFORMIO_CORE_DIR = $localCoreDir
}

$candidates = @()

if ($env:PIO_EXE) {
    $candidates += $env:PIO_EXE
}

$pathCommand = Get-Command pio -ErrorAction SilentlyContinue
if ($pathCommand) {
    $candidates += $pathCommand.Source
}

if ($env:USERPROFILE) {
    $candidates += (Join-Path $env:USERPROFILE ".platformio\penv\Scripts\platformio.exe")
}

$pio = $null
foreach ($candidate in $candidates) {
    if ($candidate -and (Test-Path -LiteralPath $candidate)) {
        $pio = $candidate
        break
    }
}

if (-not $pio) {
    Write-Error "PlatformIO was not found. Install PlatformIO or set PIO_EXE to platformio.exe."
    exit 127
}

& $pio @PioArgs
exit $LASTEXITCODE
