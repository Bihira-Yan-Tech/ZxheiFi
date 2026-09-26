# Build-Firmware.ps1
# PowerShell script to build zxheifi_firmware.bin for NodeMCU ESP8266

# ---------------------------------------------
# Configuration
# ---------------------------------------------
$SketchPath = ".\nodemcu_firmware.ino"
$ConfigPath = ".\config.h"
$OutputBin = "zxheifi_firmware.bin"
$MaxSizeBytes = 900KB  # matches config.h's SIZE CONSTRAINTS ceiling (~1,044,464-byte app partition)

# ---------------------------------------------
# Functions
# ---------------------------------------------
function Check-FileExists {
    param([string]$Path)
    if (-Not (Test-Path $Path)) {
        Write-Error "File not found: $Path"
        exit 1
    }
}

function Get-ArduinoCliPath {
    # Check if arduino-cli is in PATH
    $cli = Get-Command arduino-cli -ErrorAction SilentlyContinue
    if ($cli) {
        return $cli.Source
    }
    # Check common installation paths
    $paths = @(
        "$env:USERPROFILE\.arduino15\bin\arduino-cli.exe",
        "C:\Program Files\Arduino\arduino-cli.exe",
        "C:\Arduino\arduino-cli.exe"
    )
    foreach ($p in $paths) {
        if (Test-Path $p) {
            return $p
        }
    }
    return $null
}

function Build-Firmware {
    Write-Host "=== Building zxheifi_firmware.bin ===" -ForegroundColor Cyan

    # Check required files
    Check-FileExists $SketchPath
    Check-FileExists $ConfigPath

    # Find arduino-cli
    $arduinoCli = Get-ArduinoCliPath
    if (-Not $arduinoCli) {
        Write-Error "arduino-cli not found. Please install it from https://arduino.github.io/arduino-cli/installation/"
        Write-Host "Alternatively, use Arduino IDE: Sketch > Verify/Compile" -ForegroundColor Yellow
        exit 1
    }

    Write-Host "Using arduino-cli: $arduinoCli" -ForegroundColor Green

    # Compile
    & $arduinoCli compile --fqbn esp8266:esp8266:nodemcuv2 `
        --libraries "ArduinoJson" `
        --build-properties "build.extra_flags=-D CORE_DEBUG_LEVEL=0" `
        --output-dir "./build" `
        $SketchPath

    if ($LASTEXITCODE -ne 0) {
        Write-Error "Compilation failed. Check the output above for errors."
        exit 1
    }

    # Locate the generated binary
    $binPath = Get-ChildItem -Path "./build" -Filter "*.bin" -File | Select-Object -First 1
    if (-Not $binPath) {
        Write-Error "Compilation succeeded but no .bin file found in ./build"
        exit 1
    }

    # Check size
    $size = $binPath.Length
    Write-Host "Binary size: $size bytes ($([math]::Round($size/1KB, 2)) KB)" -ForegroundColor Green

    if ($size -gt $MaxSizeBytes) {
        Write-Error "Binary size exceeds $MaxSizeBytes bytes limit!"
        Write-Host "Please optimize the firmware to reduce size." -ForegroundColor Yellow
        exit 1
    }

    # Copy and rename
    Copy-Item $binPath $OutputBin
    Write-Host "Binary copied to: $OutputBin" -ForegroundColor Green

    # Cleanup
    Remove-Item -Recurse -Force "./build" -ErrorAction SilentlyContinue

    Write-Host "=== Build successful! ===" -ForegroundColor Green
    Write-Host "Flash $OutputBin to your NodeMCU using NodeMCU-PyFlasher or ESP8266Flasher." -ForegroundColor Yellow
}

# ---------------------------------------------
# Main
# ---------------------------------------------
Build-Firmware