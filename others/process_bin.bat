<# :
@echo off
setlocal

if not exist "bin" mkdir "bin"
if not exist "bin\crc" mkdir "bin\crc"
if not exist "bin\log" mkdir "bin\log"

echo [1/3] Building CRC-wrapped upgrade images...
powershell -noprofile "iex (${%~f0} | out-string)"
echo.
echo [3/3] Task finished. Press any key to exit.
pause > nul
exit /b
#>

$ErrorActionPreference = "Stop"

$binDir = "bin"
$manifestPath = Join-Path $binDir "manifest.json"
$crcDir = Join-Path $binDir "crc"
$logDir = Join-Path $binDir "log"
$dateStr = Get-Date -Format "yyyyMMdd_HHmmss"
$logFile = Join-Path $logDir "process_$dateStr.log"

function Write-Log($message, $level = "INFO") {
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $entry = "[$timestamp] [$level] $message"
    $color = "White"

    if ($level -eq "SUCCESS") { $color = "Green" }
    elseif ($level -eq "WARN") { $color = "Yellow" }
    elseif ($level -eq "ERROR") { $color = "Red" }

    Write-Host $entry -ForegroundColor $color
    Add-Content -Path $logFile -Value $entry
}

function Get-UInt32Value($value, [uint32]$defaultValue = 0) {
    if ($null -eq $value) {
        return $defaultValue
    }

    if ($value -is [string]) {
        $text = $value.Trim()
        if ($text.Length -eq 0) {
            return $defaultValue
        }

        if ($text.StartsWith("0x") -or $text.StartsWith("0X")) {
            return [Convert]::ToUInt32($text.Substring(2), 16)
        }

        return [Convert]::ToUInt32($text, 10)
    }

    return [uint32]$value
}

function Format-Hex32([uint32]$value) {
    return ("0x{0:X8}" -f $value)
}

function Ensure-Utf8Json($path, $object) {
    $object | ConvertTo-Json -Depth 10 | Set-Content -Path $path -Encoding UTF8
}

function Update-Mpeg2Crc([uint32]$currentCrc, [byte[]]$bytes) {
    [uint32]$crc = $currentCrc
    [uint32]$poly = 0x04C11DB7
    $offset = 0

    while ($offset -lt $bytes.Length) {
        [byte[]]$wordBuffer = 0, 0, 0, 0
        $chunkSize = $bytes.Length - $offset
        if ($chunkSize -gt 4) {
            $chunkSize = 4
        }

        [Array]::Copy($bytes, $offset, $wordBuffer, 0, $chunkSize)
        [uint32]$word = [BitConverter]::ToUInt32($wordBuffer, 0)
        $crc = ($crc -bxor $word) -band 0xFFFFFFFF

        for ($bit = 0; $bit -lt 32; $bit++) {
            if (($crc -band 0x80000000) -ne 0) {
                $crc = ((($crc -shl 1) -band 0xFFFFFFFF) -bxor $poly) -band 0xFFFFFFFF
            } else {
                $crc = ($crc -shl 1) -band 0xFFFFFFFF
            }
        }

        $offset += $chunkSize
    }

    return $crc
}

function Get-PaddedBytes([byte[]]$bytes) {
    $remainder = $bytes.Length % 4
    if ($remainder -eq 0) {
        return $bytes
    }

    $paddingCount = 4 - $remainder
    [byte[]]$newBytes = New-Object byte[] ($bytes.Length + $paddingCount)
    [Array]::Copy($bytes, $newBytes, $bytes.Length)
    Write-Log "  - Alignment: added $paddingCount trailing zero byte(s)"
    return $newBytes
}

Write-Log "------------------------------------------"
Write-Log "Session started. Log file: process_$dateStr.log"

if (-not (Test-Path $manifestPath)) {
    Write-Log "Critical error: $manifestPath not found" "ERROR"
    exit 1
}

$manifestRaw = Get-Content $manifestPath -Raw
$manifestRaw = $manifestRaw -replace '=', ':'
$manifestRaw = $manifestRaw -replace '(?<!")0x([0-9a-fA-F]+)(?!")', '"0x$1"'

try {
    $manifest = $manifestRaw | ConvertFrom-Json
} catch {
    Write-Log "Manifest parse error in $manifestPath" "ERROR"
    exit 1
}

if (-not $manifest.PSObject.Properties["operations"]) {
    Write-Log "Manifest does not contain operations[]" "ERROR"
    exit 1
}

if (-not $manifest.PSObject.Properties["crc_config"]) {
    $manifest | Add-Member -NotePropertyName crc_config -NotePropertyValue "0xA5A55A5A"
}

[uint32]$flag = Get-UInt32Value $manifest.crc_config 0xA5A55A5A
$manifest.crc_config = Format-Hex32 $flag
Write-Log "Packet flag: $($manifest.crc_config)"
Write-Log "Dual-slot note: boot always writes app.bin into the inactive slot; package itself is slot-agnostic."

foreach ($op in @($manifest.operations)) {
    $fileName = [string]$op.file
    if ([string]::IsNullOrWhiteSpace($fileName)) {
        Write-Log "Skip operation without file field" "WARN"
        continue
    }

    $sourcePath = Join-Path $binDir $fileName
    if (-not (Test-Path $sourcePath)) {
        Write-Log "File skip: $fileName not found in $binDir" "WARN"
        continue
    }

    Write-Log "Processing: $fileName"
    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($sourcePath)
    $bytes = Get-PaddedBytes $bytes

    [uint32]$crc = 0xFFFFFFFF
    $crc = Update-Mpeg2Crc $crc $bytes

    $op.size = Format-Hex32([uint32]$bytes.Length)
    $op.crc32 = Format-Hex32 $crc

    if ($op.PSObject.Properties["version"]) {
        [uint32]$version = Get-UInt32Value $op.version 0
        $op.version = Format-Hex32 $version
    } elseif ($fileName -ieq "app.bin") {
        $op | Add-Member -NotePropertyName version -NotePropertyValue "0x00000000"
        Write-Log "  - app.bin version missing, defaulted to 0x00000000" "WARN"
    }

    $outputPath = Join-Path $crcDir $fileName
    $outputDir = Split-Path $outputPath -Parent
    if (-not (Test-Path $outputDir)) {
        New-Item -ItemType Directory -Path $outputDir | Out-Null
    }

    $stream = [System.IO.File]::Create($outputPath)
    try {
        $stream.Write([BitConverter]::GetBytes($flag), 0, 4)
        $stream.Write([BitConverter]::GetBytes($crc), 0, 4)
        $stream.Write($bytes, 0, $bytes.Length)
    } finally {
        $stream.Dispose()
    }

    $versionText = ""
    if ($op.PSObject.Properties["version"]) {
        $versionText = " [Version: $($op.version)]"
    }

    Write-Log "Success: $fileName [CRC: $($op.crc32)] [Size: $($op.size)]$versionText" "SUCCESS"
}

Ensure-Utf8Json $manifestPath $manifest
Write-Log "Manifest updated."
Write-Log "Session finished."
