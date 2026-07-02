# Build + Flash firmware ESP32 via esptool
# Rulat de self-hosted runner Windows cu ESP32 conectat USB

$ErrorActionPreference = 'Stop'

$env:ZEPHYR_BASE = 'C:\zephyr-workspace\zephyr'
$env:PATH = 'C:\zephyr-workspace\.venv\Scripts;' + $env:PATH

$python = 'C:\zephyr-workspace\.venv\Scripts\python.exe'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$AppDir = Join-Path $RepoRoot 'application\esp32_app'

if ($env:RUNNER_TEMP) {
    $RunId = if ($env:GITHUB_RUN_ID) { $env:GITHUB_RUN_ID } else { 'local' }
    $RunAttempt = if ($env:GITHUB_RUN_ATTEMPT) { $env:GITHUB_RUN_ATTEMPT } else { '0' }
    $BuildDir = Join-Path $env:RUNNER_TEMP "gloveassist-$RunId-$RunAttempt-esp32"
} else {
    $BuildDir = 'C:\zephyr-workspace\build-glove-esp32'
}

$Board = 'esp32_devkitc_wroom/esp32/procpu'

if (Test-Path 'C:\zephyr-workspace\zephyr\boards\espressif\esp32_devkitc\esp32_devkitc_procpu.yaml') {
    $Board = 'esp32_devkitc/esp32/procpu'
}

Write-Host "Repo root: $RepoRoot"
Write-Host "App dir: $AppDir"
Write-Host "Build dir: $BuildDir"
Write-Host "Zephyr base: $env:ZEPHYR_BASE"
Write-Host "Board: $Board"

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Label,
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    Write-Host $Label
    Write-Host ("> {0} {1}" -f $FilePath, ($Arguments -join ' '))
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

Invoke-Checked "Building ESP32..." $python @(
    '-m', 'west', 'build',
    '-p', 'always',
    '-b', $Board,
    $AppDir,
    '--build-dir', $BuildDir
)

Invoke-Checked "Flashing ESP32..." $python @(
    '-m', 'west', 'flash',
    '--build-dir', $BuildDir
)
Write-Host "ESP32 flash OK"
