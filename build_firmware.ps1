param(
    [string]$ArduinoCli = "arduino-cli",
    [string]$ConfigFile = ""
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$Sketch = Join-Path $Root "firmware"
$Output = Join-Path $Sketch "build"
$CliArgs = @()
if ($ConfigFile) {
    if (-not (Test-Path -LiteralPath $ConfigFile -PathType Leaf)) {
        throw "Configuration Arduino CLI introuvable : $ConfigFile"
    }
    $CliArgs += @("--config-file", $ConfigFile)
}
if (-not (Get-Command $ArduinoCli -ErrorAction SilentlyContinue)) {
    $LocalCli = Join-Path $env:LOCALAPPDATA "Temp\opencode\arduino-cli\arduino-cli.exe"
    if ($ArduinoCli -eq "arduino-cli" -and (Test-Path -LiteralPath $LocalCli)) {
        $ArduinoCli = $LocalCli
    } else {
        throw "Arduino CLI introuvable. Utiliser -ArduinoCli avec le chemin de arduino-cli.exe."
    }
}
$Cores = & $ArduinoCli @CliArgs core list --format json
if ($LASTEXITCODE -ne 0) { throw "Impossible de lire les plateformes Arduino." }
$CoreInfo = $Cores -join "`n" | ConvertFrom-Json
$TeensyCore = @($CoreInfo.platforms | Where-Object id -EQ "teensy:avr")
if ($TeensyCore.Count -ne 1 -or $TeensyCore[0].installed_version -ne "1.62.0") {
    throw "Installer teensy:avr 1.62.0 avant de compiler ce firmware."
}
$BuildCache = Join-Path $env:LOCALAPPDATA ("Temp\opencode\amen-sampler-" + [guid]::NewGuid().ToString("N"))
try {
    & $ArduinoCli @CliArgs compile --fqbn "teensy:avr:teensy41:usb=serial" --warnings all --build-path $BuildCache --output-dir $Output $Sketch
    if ($LASTEXITCODE -ne 0) { throw "Compilation echouee. Ne pas utiliser un ancien HEX." }
    $Hex = Join-Path $Output "firmware.ino.hex"
    if (-not (Test-Path -LiteralPath $Hex -PathType Leaf)) { throw "Compilation terminee sans HEX." }
    Write-Host "Firmware compile : $Hex" -ForegroundColor Green
    Write-Host "20 pads | 4 voix | Shift + pad : assigner | E1 : naviguer | clic E1 : confirmer | E7 : volume casque (muet au demarrage)"
} finally {
    if (Test-Path -LiteralPath $BuildCache) {
        Remove-Item -LiteralPath $BuildCache -Recurse -Force
    }
}
