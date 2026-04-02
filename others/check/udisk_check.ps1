$ErrorActionPreference = 'Stop'

# ================= 配置区 =================
[uint32]$CustomFeatureCode = 0x12345678
$WriteLittleEndian = $true   # $true = 小端，$false = 大端
# ==========================================

function Write-Log {
    param(
        [string]$Message,
        [string]$Level = 'INFO'
    )
    $ts = Get-Date -Format 'HH:mm:ss'
    Write-Host "[$ts] [$Level] $Message"
}

function Get-SimpleChecksum {
    param(
        [string]$Serial,
        [string]$VolumeID,
        [uint32]$FeatureCode
    )

    [uint32]$sum = 0

    if (-not [string]::IsNullOrWhiteSpace($Serial)) {
        foreach ($char in $Serial.ToCharArray()) {
            $sum = [uint32]($sum + [int][char]$char)
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($VolumeID)) {
        try {
            [uint32]$vId = [System.Convert]::ToUInt32($VolumeID, 16)
            $sum = [uint32]($sum + $vId)
        }
        catch {
        }
    }

    $sum = [uint32]($sum + $FeatureCode)
    return $sum
}

function Get-UsbSerialFromCurrentDrive {
    param(
        [string]$DriveLetter
    )

    $partition = Get-Partition -DriveLetter $DriveLetter -ErrorAction Stop
    $disk = Get-Disk -Number $partition.DiskNumber -ErrorAction Stop
    $diskDrive = Get-CimInstance Win32_DiskDrive | Where-Object { $_.Index -eq $disk.Number }

    if (-not $diskDrive) {
        throw 'Cannot read Win32_DiskDrive info.'
    }

    $pnp = [string]$diskDrive.PNPDeviceID
    if ([string]::IsNullOrWhiteSpace($pnp)) {
        return ''
    }

    $parts = $pnp -split '\\'
    if ($parts.Count -lt 3) {
        return ''
    }

    return ($parts[-1] -replace '&\d+$', '').Trim()
}

function Convert-UInt32ToBytes {
    param(
        [uint32]$Value,
        [bool]$LittleEndian = $true
    )

    $bytes = [System.BitConverter]::GetBytes($Value)

    if (-not $LittleEndian) {
        [Array]::Reverse($bytes)
    }

    return $bytes
}

try {
    $scriptPath = $MyInvocation.MyCommand.Path
    if (-not $scriptPath) {
        throw 'Cannot get script path.'
    }

    $scriptDir = Split-Path -Parent $scriptPath
    $driveLetter = (Split-Path -Path $scriptPath -Qualifier).TrimEnd(':')

    $volume = Get-CimInstance Win32_LogicalDisk | Where-Object { $_.DeviceID -eq ($driveLetter + ':') }
    if (-not $volume) {
        throw 'Cannot read logical disk info.'
    }

    $volID = [string]$volume.VolumeSerialNumber
    $serial = Get-UsbSerialFromCurrentDrive -DriveLetter $driveLetter

    [uint32]$checkCode = Get-SimpleChecksum -Serial $serial -VolumeID $volID -FeatureCode $CustomFeatureCode
    $hexCode = '{0:X8}' -f $checkCode

    Write-Log "Serial: $serial"
    Write-Log "VolumeID: $volID"
    Write-Log ("FeatureCode: 0x{0:X8}" -f $CustomFeatureCode)
    Write-Log ("Final CheckCode: 0x{0}" -f $hexCode)

    $outputFile = Join-Path $scriptDir 'o.cck'
    [byte[]]$binaryData = Convert-UInt32ToBytes -Value $checkCode -LittleEndian $WriteLittleEndian

    [System.IO.File]::WriteAllBytes($outputFile, $binaryData)

    $attrs = [System.IO.FileAttributes]::Hidden -bor [System.IO.FileAttributes]::ReadOnly
    [System.IO.File]::SetAttributes($outputFile, $attrs)

    $byteView = ($binaryData | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
    Write-Log ("Binary bytes written: {0}" -f $byteView)
    Write-Log 'Success! Hidden + ReadOnly binary file o.cck created.'
}
catch {
    Write-Log "Error: $($_.Exception.Message)" 'ERROR'
    exit 1
}