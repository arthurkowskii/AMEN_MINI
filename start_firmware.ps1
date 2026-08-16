# start_firmware.ps1
# Simule le demarrage du sampler AMEN_MINI : compile le player temps reel
# puis le lance avec un echantillon, comme le boot d'un instrument physique.
# Usage : powershell -ExecutionPolicy Bypass -File .\start_firmware.ps1 [[chemin.wav]]
param(
    [string]$Wav = "firmware/test_native/test.wav"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

Write-Host ""
Write-Host "  AMEN_MINI SAMPLER  v0.1" -ForegroundColor Cyan
Write-Host "  ---------------------------"

if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    Write-Error "g++ introuvable dans le PATH (MinGW/MSYS2 requis)."
    exit 1
}

$running = Get-Process -Name amen_rt -ErrorAction SilentlyContinue
if ($running) {
    Write-Host "arret de l'instance precedente (PID $($running.Id -join ', ')) ..."
    $running | Stop-Process
    Start-Sleep -Milliseconds 300
}

$srcs = @(
    Get-Item firmware/test_native/rt_player.cpp
    Get-Item firmware/test_native/screen_preview.cpp
    Get-Item firmware/test_native/screen_preview.h
    Get-Item firmware/test_native/sample_catalog_scanner.cpp
    Get-Item firmware/test_native/sample_catalog_scanner.h
    Get-ChildItem firmware/src/browser -Recurse -Include *.cpp, *.h
    Get-ChildItem firmware/src/engine -Recurse -Include *.cpp, *.h
    Get-ChildItem firmware/src/ui -Recurse -Include *.cpp, *.h
)
$needsBuild = -not (Test-Path "firmware/amen_rt.exe")
if (-not $needsBuild) {
    $exeTime = (Get-Item "firmware/amen_rt.exe").LastWriteTime
    $needsBuild = ($srcs | Where-Object { $_.LastWriteTime -gt $exeTime }).Count -gt 0
}
if ($needsBuild) {
    Write-Host "moteur audio : compilation ..." -NoNewline
    $libs = @()
    if ($env:OS -eq "Windows_NT") {
        $libs = @("-lole32", "-lwinmm", "-lgdi32", "-luser32")
    }
    $cppFiles = $srcs | Where-Object { $_.Extension -eq ".cpp" } | ForEach-Object { $_.FullName }
    $build = & g++ -std=c++17 -O2 -Wno-stringop-overflow -Wno-stringop-overread `
        -I firmware/src/browser -I firmware/src/engine -I firmware/src/ui -I firmware/test_native `
        -I firmware/test_native/third_party `
        @cppFiles `
        -o firmware/amen_rt.exe @libs 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Echec" -ForegroundColor Red
        $build | ForEach-Object { Write-Host $_ }
        exit 1
    }
    Write-Host "OK" -ForegroundColor Green
} else {
    Write-Host "moteur audio : deja compile" -ForegroundColor Green
}

if (-not (Test-Path $Wav)) {
    Write-Error "echantillon introuvable : $Wav"
    exit 1
}
$WavFull = (Resolve-Path $Wav).Path
Write-Host "echantillon charge : $WavFull" -ForegroundColor Green

Write-Host "sequenceur pret."
Write-Host ""
Write-Host "  CONTROLES" -ForegroundColor Yellow
Write-Host "  pads voix   numpad 1-6   appui = joue le break et devient la cible (tenir le pad pour editer)" -ForegroundColor Cyan
Write-Host "              pad tenu + E1 = navigateur SD | + E4 = vitesse | + E5 = mode" -ForegroundColor Cyan
Write-Host "  pads FX     numpad 7-9   maintien = active le FX assigne" -ForegroundColor Cyan
Write-Host "  encodeurs   F1-F7        selectionner l'encodeur actif" -ForegroundColor Cyan
Write-Host "              fleches H/B  tourner l'encodeur selectionne" -ForegroundColor Cyan
Write-Host "              entree       cliquer l'encodeur selectionne" -ForegroundColor Cyan
Write-Host "  roles       E1 NAV : voix = carte SD, FX = liste d'effets, clic = charger/assigner" -ForegroundColor Cyan
Write-Host "              E2 Repeat dry/wet | E3 division 1/4-1/32 + triplets 1/12, 1/24 | E7 BPM (recalcule Repeat en live)" -ForegroundColor Cyan
Write-Host "              E4 vitesse du pad TENU (clic = 100%) | E5 mode du pad TENU (clic = ONE SHOT)" -ForegroundColor Cyan
Write-Host "  divers      espace = retrigger dernier pad (sa propre vitesse) | retour = parent | q = quitter" -ForegroundColor Cyan
Write-Host ""

Set-Location firmware
& .\amen_rt.exe $WavFull
exit $LASTEXITCODE
