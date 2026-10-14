<#
.SYNOPSIS
Read Betaflight MSP identity/status/IMU and optionally back up CLI settings.
.DESCRIPTION
No setting, motor, arming or flashing commands are sent. -CliBackup enters CLI,
reads diagnostic commands and dump/diff, then exits CLI (which reboots Betaflight).
-RebootDfu explicitly requests ROM DFU; it requires an exact expected MCU UID.
-GyroRegisterSamples collects raw gyro/register snapshots in CLI without setting
debug_mode; leaving CLI reboots Betaflight, just as -CliBackup does.
Use only with propellers removed and USB-only power. OutputPath must not exist.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^COM[0-9]+$')][string]$Port,
    [ValidateRange(1, 500)][int]$Samples = 30,
    [ValidateRange(20, 2000)][int]$IntervalMs = 100,
    [ValidateRange(0, 200)][int]$GyroRegisterSamples = 0,
    [switch]$CliBackup,
    [switch]$RebootDfu,
    [string]$ExpectedUid,
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
if ($RebootDfu -and ($CliBackup -or $GyroRegisterSamples -or -not $ExpectedUid)) {
    throw '-RebootDfu requires -ExpectedUid and cannot be combined with CLI reads'
}
if ($OutputPath -and (Test-Path -LiteralPath $OutputPath)) {
    throw "Refusing to overwrite existing diagnostics: $OutputPath"
}

function Read-Msp([System.IO.Ports.SerialPort]$Serial, [byte]$Command, [byte[]]$Payload = @()) {
    $requestChecksum = $Payload.Length -bxor $Command
    foreach ($value in $Payload) { $requestChecksum = $requestChecksum -bxor $value }
    [byte[]]$request = @(36, 77, 60, $Payload.Length, $Command) + $Payload + @($requestChecksum)
    $Serial.DiscardInBuffer()
    $Serial.Write($request, 0, $request.Length)
    $deadline = [DateTime]::UtcNow.AddSeconds(3)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            if ($Serial.ReadByte() -ne 36) { continue }
            if ($Serial.ReadByte() -ne 77) { continue }
            $direction = $Serial.ReadByte()
            $length = $Serial.ReadByte()
            $replyCommand = $Serial.ReadByte()
            [byte[]]$data = New-Object byte[] $length
            $checksum = $length -bxor $replyCommand
            for ($i = 0; $i -lt $length; $i++) {
                $data[$i] = $Serial.ReadByte()
                $checksum = $checksum -bxor $data[$i]
            }
            $receivedChecksum = $Serial.ReadByte()
            if ($checksum -ne $receivedChecksum) { throw 'MSP checksum mismatch' }
            if ($replyCommand -ne $Command) { continue }
            if ($direction -ne 62) { throw "MSP command $Command rejected" }
            return ,$data
        } catch [TimeoutException] {
            continue
        }
    }
    throw "No valid MSP reply to command $Command on $Port"
}

function Read-Cli([System.IO.Ports.SerialPort]$Serial, [string]$Command) {
    $Serial.DiscardInBuffer()
    $Serial.Write($Command + "`r`n")
    $text = ''
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 30
        $text += $Serial.ReadExisting()
        if ($text -match '(?s)(?:\r?\n)# $') { return $text }
    }
    throw "CLI prompt not received after: $Command"
}

$serial = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
$serial.ReadTimeout = 200
$serial.WriteTimeout = 1500
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$cliEntered = $false
try {
    $serial.Open()
    Start-Sleep -Milliseconds 200
    $variant = Read-Msp $serial 2
    $version = Read-Msp $serial 3
    $board = Read-Msp $serial 4
    $build = Read-Msp $serial 5
    $uid = Read-Msp $serial 160
    $status = Read-Msp $serial 150
    if ($status.Length -lt 15 -or $board.Length -lt 9) { throw 'Short MSP status/board reply' }
    $targetLength = [int]$board[8]
    $boardNameLengthOffset = 9 + $targetLength
    if ($board.Length -le $boardNameLengthOffset) { throw 'Short MSP target name' }
    $boardNameLength = [int]$board[$boardNameLengthOffset]
    if ($board.Length -lt ($boardNameLengthOffset + 1 + $boardNameLength)) { throw 'Short MSP board name' }
    $identity = [ordered]@{
        port = $Port
        variant = [Text.Encoding]::ASCII.GetString($variant)
        version = ($version -join '.')
        boardId = [Text.Encoding]::ASCII.GetString($board, 0, 4)
        target = [Text.Encoding]::ASCII.GetString($board, 9, $board[8])
        boardName = [Text.Encoding]::ASCII.GetString($board, $boardNameLengthOffset + 1, $boardNameLength)
        boardPayload = [BitConverter]::ToString($board)
        buildPayload = [BitConverter]::ToString($build)
        uid = [BitConverter]::ToString($uid)
        pidPeriodUs = [BitConverter]::ToUInt16($status, 0)
        sensorMask = [BitConverter]::ToUInt16($status, 4)
        modeFlags = [BitConverter]::ToUInt32($status, 6)
        cpuLoadPercent = [BitConverter]::ToUInt16($status, 11)
        statusPayload = [BitConverter]::ToString($status)
    }
    if ($identity.variant -ne 'BTFL') { throw 'Connected device is not Betaflight' }
    if ($identity.modeFlags -band 1) { throw 'Refusing diagnostics on an armed flight controller' }
    if ($ExpectedUid -and $identity.uid -ne $ExpectedUid) { throw 'MCU UID does not match the selected flight controller' }
    if ($RebootDfu -and ($identity.target -ne 'STM32H743' -or $identity.boardName -ne 'MICOAIR743V2_BMI088')) {
        throw 'Refusing DFU reboot of a different MCU/board target'
    }
    Write-Output ('Identity: ' + ($identity | ConvertTo-Json -Compress))
    $samplesRead = @()
    $watch = [Diagnostics.Stopwatch]::StartNew()
    for ($i = 0; $i -lt $Samples; $i++) {
        $data = Read-Msp $serial 102
        if ($data.Length -ne 18) { throw 'Unexpected MSP_RAW_IMU length' }
        $axes = for ($axis = 0; $axis -lt 9; $axis++) { [BitConverter]::ToInt16($data, 2 * $axis) }
        # In this 4.5.2 tree gyroRateDps() divides by the sensor scale again:
        # MSP_RAW_IMU carries filtered sensor counts, NOT degrees per second.
        $sample = [ordered]@{ elapsedMs = $watch.ElapsedMilliseconds; acc = @($axes[0..2]); gyroMspCounts = @($axes[3..5]); mag = @($axes[6..8]) }
        if ($identity.boardName -eq 'MICOAIR743V2_BMI088') {
            $sample.gyroDps = @($axes[3..5] | ForEach-Object { [Math]::Round($_ * (2000.0 / 32768), 3) })
        }
        $samplesRead += $sample
        Write-Output ('IMU: ' + ($sample | ConvertTo-Json -Compress))
        Start-Sleep -Milliseconds $IntervalMs
    }
    $finalStatus = Read-Msp $serial 150
    if ($finalStatus.Length -lt 15) { throw 'Short final MSP status reply' }
    $finalTiming = [ordered]@{
        pidPeriodUs = [BitConverter]::ToUInt16($finalStatus, 0)
        cpuLoadPercent = [BitConverter]::ToUInt16($finalStatus, 11)
        statusPayload = [BitConverter]::ToString($finalStatus)
    }
    Write-Output ('Final timing: ' + ($finalTiming | ConvertTo-Json -Compress))
    $cli = [ordered]@{}
    $gyroRegisterRead = @()
    if ($CliBackup -or $GyroRegisterSamples) {
        $serial.DiscardInBuffer()
        $serial.Write('#')
        $cliEntered = $true
        Start-Sleep -Milliseconds 300
        $greeting = $serial.ReadExisting()
        if ($greeting -notmatch 'CLI|# ') { throw 'Cannot confirm CLI entry' }
        for ($i = 0; $i -lt $GyroRegisterSamples; $i++) {
            $text = Read-Cli $serial 'gyroregisters'
            $gyroRegisterRead += [ordered]@{ elapsedMs = $watch.ElapsedMilliseconds; registers = $text }
            Start-Sleep -Milliseconds $IntervalMs
        }
        if ($CliBackup) {
            foreach ($command in @('version', 'status', 'resource', 'tasks', 'get gyro', 'get acc', 'gyroregisters', 'diff all', 'dump all')) {
                $cli[$command] = Read-Cli $serial $command
                Write-Output ('CLI read: ' + $command)
            }
        }
    }
    $result = [ordered]@{ schemaVersion = 2; utcTime = [DateTime]::UtcNow.ToString('o'); identity = $identity; finalTiming = $finalTiming; samples = $samplesRead; gyroRegisterSamples = $gyroRegisterRead; cli = $cli }
    if ($OutputPath) {
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($OutputPath)) | Out-Null
        [IO.File]::WriteAllText($OutputPath, ($result | ConvertTo-Json -Depth 12), (New-Object Text.UTF8Encoding $false))
        Write-Output ('Saved: ' + $OutputPath)
    }
    if ($RebootDfu) {
        try {
            $reply = Read-Msp $serial 68 @(1)
            Write-Output ('Requested ROM DFU for verified UID ' + $identity.uid)
        } catch [IO.IOException] {
            # USB may detach before an MSP acknowledgement arrives. This alone
            # does not prove success: enumerate DFU and verify the UID again.
            Write-Warning 'USB detached during DFU reboot; verify DFU identity before flashing.'
        }
    }
} finally {
    if ($serial.IsOpen) {
        try {
            if ($cliEntered) { $serial.Write("exit`r`n") }
            $serial.Close()
        } catch [IO.IOException] {
            if (-not ($cliEntered -or $RebootDfu)) { throw }
            Write-Verbose 'USB detached during the requested reboot.'
        }
    }
    try {
        $serial.Dispose()
    } catch [IO.IOException] {
        if (-not ($cliEntered -or $RebootDfu)) { throw }
        Write-Verbose 'Serial device was removed during the requested reboot.'
    }
}
