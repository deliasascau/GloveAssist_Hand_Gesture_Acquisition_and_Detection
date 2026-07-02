# Build + Flash firmware STM32 via ST-Link
# Rulat de self-hosted runner Windows cu ST-Link conectat USB

$ErrorActionPreference = 'Stop'

$SdkDir = 'C:\zephyr-sdk-1.0.1'
$env:ZEPHYR_BASE = 'C:\zephyr-workspace\zephyr'
$env:ZEPHYR_SDK_INSTALL_DIR = $SdkDir
$env:ZEPHYR_TOOLCHAIN_VARIANT = 'zephyr'
$env:CMAKE_PREFIX_PATH = if ($env:CMAKE_PREFIX_PATH) { "$SdkDir;$env:CMAKE_PREFIX_PATH" } else { $SdkDir }
$env:PATH = 'C:\zephyr-workspace\.venv\Scripts;' + $env:PATH
$env:GIT_CONFIG_COUNT = '1'
$env:GIT_CONFIG_KEY_0 = 'safe.directory'
$env:GIT_CONFIG_VALUE_0 = '*'

$python = 'C:\zephyr-workspace\.venv\Scripts\python.exe'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$AppDir = Join-Path $RepoRoot 'application\stm32_app'

if ($env:RUNNER_TEMP) {
    $RunId = if ($env:GITHUB_RUN_ID) { $env:GITHUB_RUN_ID } else { 'local' }
    $RunAttempt = if ($env:GITHUB_RUN_ATTEMPT) { $env:GITHUB_RUN_ATTEMPT } else { '0' }
    $BuildDir = Join-Path $env:RUNNER_TEMP "gloveassist-$RunId-$RunAttempt-stm32"
} else {
    $BuildDir = 'C:\zephyr-workspace\build-glove-stm32'
}

Write-Host "Repo root: $RepoRoot"
Write-Host "App dir: $AppDir"
Write-Host "Build dir: $BuildDir"
Write-Host "Zephyr base: $env:ZEPHYR_BASE"
Write-Host "Zephyr SDK: $env:ZEPHYR_SDK_INSTALL_DIR"
Write-Host "Git safe.directory: process-local wildcard"

function Show-BuildDiagnostics {
    if (-not (Test-Path $BuildDir)) {
        return
    }

    $LogFiles = @(
        (Join-Path $BuildDir 'CMakeFiles\CMakeError.log'),
        (Join-Path $BuildDir 'CMakeFiles\CMakeOutput.log'),
        (Join-Path $BuildDir 'CMakeFiles\CMakeConfigureLog.yaml')
    )

    foreach ($LogFile in $LogFiles) {
        if (Test-Path $LogFile) {
            Write-Host "----- $LogFile -----"
            Get-Content -LiteralPath $LogFile -Tail 120
        }
    }
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Label,
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,
        [switch]$AcceptOpenOcdWriteSuccess
    )

    Write-Host $Label
    Write-Host ("> {0} {1}" -f $FilePath, ($Arguments -join ' '))
    $Output = & $FilePath @Arguments 2>&1
    $ExitCode = $LASTEXITCODE
    $Output | ForEach-Object { Write-Host $_ }

    if ($ExitCode -ne 0) {
        $OutputText = $Output -join "`n"
        if ($AcceptOpenOcdWriteSuccess -and $OutputText -match 'wrote\s+\d+\s+bytes\s+from file') {
            Write-Host "$Label wrote the image; ignoring OpenOCD reset/shutdown exit code $ExitCode"
            return
        }

        Show-BuildDiagnostics
        throw "$Label failed with exit code $ExitCode"
    }
}

Invoke-Checked "Building STM32..." $python @(
    '-m', 'west',
    '-z', $env:ZEPHYR_BASE,
    'build',
    '-p', 'always',
    '-b', 'stm32_min_dev@blue',
    $AppDir,
    '--build-dir', $BuildDir
)

Invoke-Checked "Flashing STM32..." $python @(
    '-m', 'west',
    '-z', $env:ZEPHYR_BASE,
    'flash',
    '--build-dir', $BuildDir
) -AcceptOpenOcdWriteSuccess
Write-Host "STM32 flash OK"
