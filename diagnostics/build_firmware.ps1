param(
    [string]$ArduinoCli = "arduino-cli",
    [string]$ConfigFile = ""
)

$ErrorActionPreference = "Stop"
$Sketch = Join-Path $PSScriptRoot "teensy\amen_diagnostic"
$Output = Join-Path $Sketch "build"
$CliArgs = @()
if ($ConfigFile) {
    if (-not (Test-Path -LiteralPath $ConfigFile -PathType Leaf)) {
        throw "Configuration Arduino CLI introuvable : $ConfigFile"
    }
    $CliArgs += @("--config-file", $ConfigFile)
}
if (-not (Get-Command $ArduinoCli -ErrorAction SilentlyContinue)) {
    throw "Arduino CLI introuvable. Utiliser -ArduinoCli avec le chemin de arduino-cli.exe."
}

$Version = & $ArduinoCli @CliArgs version --format json
if ($LASTEXITCODE -ne 0) { throw "Impossible de lire la version Arduino CLI." }
$Cores = & $ArduinoCli @CliArgs core list --format json
if ($LASTEXITCODE -ne 0) { throw "Impossible de lire les plateformes Arduino installees." }
$CoreInfo = $Cores -join "`n" | ConvertFrom-Json
$TeensyCore = @($CoreInfo.platforms | Where-Object id -EQ "teensy:avr")
if ($TeensyCore.Count -ne 1 -or $TeensyCore[0].installed_version -ne "1.62.0") {
    throw "Ce diagnostic a ete verifie avec teensy:avr 1.62.0. Installer cette version avant de compiler."
}
$Libraries = & $ArduinoCli @CliArgs lib list --format json
if ($LASTEXITCODE -ne 0) { throw "Impossible de lire les bibliotheques installees." }

$BuildCache = Join-Path ([IO.Path]::GetTempPath()) ("amen-diagnostic-" + [guid]::NewGuid().ToString("N"))
try {
    & $ArduinoCli @CliArgs compile --fqbn "teensy:avr:teensy41:usb=serial" --warnings all --build-path $BuildCache --output-dir $Output $Sketch
    if ($LASTEXITCODE -ne 0) { throw "La compilation du diagnostic a echoue. Aucun nouveau binaire valide." }
    $Hex = Join-Path $Output "amen_diagnostic.ino.hex"
    if (-not (Test-Path -LiteralPath $Hex -PathType Leaf)) { throw "Compilation terminee sans fichier HEX attendu." }
    $Sources = @{}
    Get-ChildItem -LiteralPath $Sketch -File | Where-Object Extension -In ".ino", ".h", ".cpp" | ForEach-Object {
        $Sources[$_.Name] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
    $Manifest = @{
        built_at = [DateTime]::UtcNow.ToString("o")
        fqbn = "teensy:avr:teensy41:usb=serial"
        arduino_cli = ($Version -join "`n" | ConvertFrom-Json)
        core = @{ id = "teensy:avr"; version = "1.62.0" }
        libraries = ($Libraries -join "`n" | ConvertFrom-Json)
        sources = $Sources
        hardware_profile_sha256 = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot "hardware_profile.json") -Algorithm SHA256).Hash
        hex_sha256 = (Get-FileHash -LiteralPath $Hex -Algorithm SHA256).Hash
    }
    $Manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $Output "build_manifest.json") -Encoding UTF8
    Write-Host "Diagnostic compile : $Hex"
} finally {
    if (Test-Path -LiteralPath $BuildCache) {
        Remove-Item -LiteralPath $BuildCache -Recurse -Force
    }
}
