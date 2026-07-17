# Hardware end-to-end smoke test for the self-hosted Windows runner.
#
# The STM32 firmware does not expose a UART console in the production build, so
# the end-to-end checks are driven from the ESP32 console log. A successful ESP32
# log proves that ESP32 booted, BLE became reachable, STM32 answered the UART
# session handshake, WiFi obtained IPv4, MQTT connected, and firmware published
# at least one MQTT status message.

[CmdletBinding()]
param(
    [string]$Esp32Port,
    [int]$Esp32Baud = 115200,
    [int]$TimeoutSec = 180,
    [string]$LogDir,
    [switch]$ListPorts,
    [switch]$ResetEsp32,
    [switch]$SkipBleClient,
    [switch]$RequireBleClient,
    [int]$BleTimeoutSec = 35,
    [string]$Python
)

$ErrorActionPreference = 'Stop'

if (-not $Esp32Port -and $env:ESP32_SERIAL_PORT) {
    $Esp32Port = $env:ESP32_SERIAL_PORT
}

if ($env:ESP32_SERIAL_BAUD) {
    $Esp32Baud = [int]$env:ESP32_SERIAL_BAUD
}

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if (-not $LogDir) {
    if ($env:RUNNER_TEMP) {
        $RunId = if ($env:GITHUB_RUN_ID) { $env:GITHUB_RUN_ID } else { 'local' }
        $RunAttempt = if ($env:GITHUB_RUN_ATTEMPT) { $env:GITHUB_RUN_ATTEMPT } else { '0' }
        $LogDir = Join-Path $env:RUNNER_TEMP "gloveassist-$RunId-$RunAttempt-e2e"
    } else {
        $LogDir = Join-Path $RepoRoot 'e2e-logs'
    }
}

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

function Get-SerialPortInventory {
    $items = @()

    try {
        $devices = Get-CimInstance Win32_PnPEntity |
            Where-Object { $_.Name -match '\((COM\d+)\)' }
    } catch {
        Write-Warning "Could not query Win32_PnPEntity; falling back to SerialPort.GetPortNames()."
        foreach ($portName in [System.IO.Ports.SerialPort]::GetPortNames()) {
            $items += [pscustomobject]@{
                Port = $portName
                Name = $portName
                PnpDeviceId = ''
            }
        }
        return $items | Sort-Object Port
    }

    foreach ($device in $devices) {
        if ($device.Name -match '\((COM\d+)\)') {
            $items += [pscustomobject]@{
                Port = $Matches[1]
                Name = $device.Name
                PnpDeviceId = $device.PNPDeviceID
            }
        }
    }

    return $items | Sort-Object Port
}

function Resolve-Esp32Port {
    param([string]$RequestedPort)

    if ($RequestedPort) {
        return $RequestedPort
    }

    $ports = @(Get-SerialPortInventory)
    $preferred = @($ports | Where-Object {
        $_.Name -match 'CP210|Silicon Labs|USB.*UART|USB Serial|CH340'
    })

    if ($preferred.Count -eq 1) {
        return $preferred[0].Port
    }

    if ($ports.Count -eq 1) {
        return $ports[0].Port
    }

    Write-Host "Serial ports detected:"
    $ports | Format-Table -AutoSize | Out-String | Write-Host
    throw "Set ESP32_SERIAL_PORT or pass -Esp32Port. Could not choose ESP32 port automatically."
}

function Save-Summary {
    param(
        [hashtable]$Checks,
        [string]$Path
    )

    $lines = @()
    foreach ($name in $Checks.Keys) {
        $state = if ($Checks[$name].Found) { 'PASS' } else { 'FAIL' }
        $lines += ("{0,-18} {1}  {2}" -f $name, $state, $Checks[$name].Description)
    }
    $lines | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Resolve-Python {
    param([string]$RequestedPython)

    if ($RequestedPython) {
        return $RequestedPython
    }
    if ($env:PYTHON) {
        return $env:PYTHON
    }
    $ZephyrPython = 'C:\zephyr-workspace\.venv\Scripts\python.exe'
    if (Test-Path -LiteralPath $ZephyrPython) {
        return $ZephyrPython
    }
    return 'python'
}

if ($ListPorts) {
    Get-SerialPortInventory | Format-Table -AutoSize
    exit 0
}

$Esp32Port = Resolve-Esp32Port $Esp32Port
$Esp32Log = Join-Path $LogDir 'esp32-serial.log'
$SummaryLog = Join-Path $LogDir 'e2e-summary.txt'
$BleLog = Join-Path $LogDir 'ble-client.log'

foreach ($path in @($Esp32Log, $SummaryLog, $BleLog)) {
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Force
    }
}

Write-Host "E2E log dir: $LogDir"
Write-Host "ESP32 serial: $Esp32Port @ $Esp32Baud"

$checks = [ordered]@{
    esp32_boot = @{
        Pattern = 'Init OK\. Waiting for STM32 frames'
        Description = 'ESP32 firmware booted and reached main loop'
        Found = $false
    }
    ble_advertising = @{
        Pattern = 'BLE ready for phone connection|BLE advertising as GloveAssist|BLE connected|BLE link authenticated'
        Description = 'BLE service advertised or accepted a phone connection'
        Found = $false
    }
    uart_handshake = @{
        Pattern = 'UART secure session established \(STM32 ACK received\)|Link: [1-9][0-9]* frames'
        Description = 'STM32 and ESP32 completed handshake or exchanged valid secure frames'
        Found = $false
    }
    wifi_ipv4 = @{
        Pattern = 'IPv4 address acquired - WiFi ready'
        Description = 'ESP32 connected to WiFi and obtained IPv4'
        Found = $false
    }
    mqtt_connected = @{
        Pattern = 'MQTT connected to '
        Description = 'ESP32 connected to MQTT broker'
        Found = $false
    }
    mqtt_publish = @{
        Pattern = 'MQTT pub (ok|qos1).*: .* <- '
        Description = 'ESP32 published at least one MQTT message'
        Found = $false
    }
}

$serial = [System.IO.Ports.SerialPort]::new(
    $Esp32Port,
    $Esp32Baud,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)
$serial.Encoding = [System.Text.Encoding]::UTF8
$serial.ReadTimeout = 200
$serial.WriteTimeout = 200
$serial.DtrEnable = $false
$serial.RtsEnable = $false

$buffer = New-Object System.Text.StringBuilder

try {
    $serial.Open()
    $serial.DiscardInBuffer()

    if ($ResetEsp32) {
        Write-Host "Resetting ESP32 through serial RTS pulse..."
        $serial.RtsEnable = $true
        Start-Sleep -Milliseconds 250
        $serial.RtsEnable = $false
        Start-Sleep -Milliseconds 500
    }

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $chunk = $serial.ReadExisting()
        } catch {
            Write-Warning "Serial read stopped: $($_.Exception.Message)"
            break
        }

        if ($chunk.Length -gt 0) {
            Add-Content -LiteralPath $Esp32Log -Value $chunk -NoNewline -Encoding UTF8
            [void]$buffer.Append($chunk)

            $text = $buffer.ToString()
            foreach ($name in $checks.Keys) {
                if (-not $checks[$name].Found -and ($text -match $checks[$name].Pattern)) {
                    $checks[$name].Found = $true
                    Write-Host ("PASS {0}: {1}" -f $name, $checks[$name].Description)
                }
            }

            $missing = @($checks.Keys | Where-Object { -not $checks[$_].Found })
            if ($missing.Count -eq 0) {
                break
            }
        }

        Start-Sleep -Milliseconds 200
    }
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
}

Save-Summary -Checks $checks -Path $SummaryLog

$missing = @($checks.Keys | Where-Object { -not $checks[$_].Found })
if ($missing.Count -gt 0) {
    Write-Host "E2E summary:"
    Get-Content -LiteralPath $SummaryLog | ForEach-Object { Write-Host $_ }
    throw "Hardware E2E failed. Missing checks: $($missing -join ', ')"
}

Write-Host "Serial hardware E2E checks passed."

if (-not $SkipBleClient) {
    $BleScript = Join-Path $RepoRoot 'scripts\e2e_ble_client.py'
    $PythonExe = Resolve-Python $Python

    Write-Host "Running optional BLE client check with $PythonExe..."
    & $PythonExe $BleScript --timeout $BleTimeoutSec --log $BleLog
    $BleExit = $LASTEXITCODE

    if ($BleExit -eq 0) {
        Write-Host "BLE client E2E check passed."
    } elseif (($BleExit -eq 2) -and (-not $RequireBleClient)) {
        Write-Warning "BLE client check skipped because Python dependency 'bleak' is not installed."
    } else {
        throw "BLE client E2E check failed with exit code $BleExit"
    }
}

Write-Host "Hardware E2E smoke test passed."
