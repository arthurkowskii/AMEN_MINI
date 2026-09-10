param(
    [string]$Wav = "firmware/test_native/test.wav",
    [switch]$Smoke
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $Root
try {
    if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
        throw "g++ introuvable dans le PATH (MinGW/MSYS2 requis)."
    }
    $cppFiles = @(
        "firmware/test_native/rt_player.cpp"
        "firmware/src/engine/pcm_wav.cpp"
        "firmware/src/engine/stream_mixer.cpp"
    )
    $tracked = $cppFiles + @(
        "firmware/src/engine/pcm_wav.h"
        "firmware/src/engine/stream_mixer.h"
        "firmware/test_native/third_party/miniaudio.h"
        "start_firmware.ps1"
    )
    $exe = "firmware/amen_rt.exe"
    $needsBuild = -not (Test-Path $exe)
    if (-not $needsBuild) {
        $exeTime = (Get-Item $exe).LastWriteTimeUtc
        $needsBuild = @($tracked | Get-Item | Where-Object { $_.LastWriteTimeUtc -gt $exeTime }).Count -gt 0
    }
    if ($needsBuild) {
        Write-Host "Moteur streaming : compilation..." -ForegroundColor Cyan
        & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic `
            -I firmware/src/engine -isystem firmware/test_native/third_party `
            @cppFiles -o $exe -lole32 -lwinmm -lgdi32 -luser32
        if ($LASTEXITCODE -ne 0) { throw "Compilation echouee ($LASTEXITCODE)." }
    } else {
        Write-Host "Moteur streaming : deja compile" -ForegroundColor Green
    }
    $arguments = @()
    if ($Smoke) { $arguments += "--smoke" }
    if ($Wav) { $arguments += (Resolve-Path $Wav).Path }
    Write-Host "Diagnostic natif : assign <1..20> <chemin> | play <1..20> | stop | quit"
    & ".\$exe" @arguments
    $result = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $result
