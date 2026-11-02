# Build Swell Key on Windows (Standalone + VST3)
$ErrorActionPreference = 'Stop'

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $cmake = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter cmake.exe -ErrorAction SilentlyContinue |
             Select-Object -First 1 -ExpandProperty FullName
}
if (-not $cmake) { throw "CMake introuvable. Installe-le avec: winget install Kitware.CMake" }

# Reuse a JUCE checkout from another Swell project if there is one (saves the download)
$juceArgs = @()
$juceSrc = Join-Path $PSScriptRoot "..\SwellChordMotion\build\_deps\juce-src"
if (Test-Path $juceSrc) { $juceArgs = @("-DFETCHCONTENT_SOURCE_DIR_JUCE=$((Resolve-Path $juceSrc).Path)") }

& $cmake -S $PSScriptRoot -B "$PSScriptRoot\build" -G "Visual Studio 17 2022" -A x64 @juceArgs
if ($LASTEXITCODE -ne 0) { throw "La configuration CMake a echoue" }

& $cmake --build "$PSScriptRoot\build" --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "La compilation a echoue" }

$out = "$PSScriptRoot\build\SwellKey_artefacts\Release"
Write-Host ""
Write-Host "OK !"
Write-Host "Standalone : $out\Standalone\Swell Key.exe"
Write-Host "VST3       : $out\VST3\Swell Key.vst3"
