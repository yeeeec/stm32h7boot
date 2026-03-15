$ErrorActionPreference = 'Stop'

function Write-Log {
    param(
        [string]$Message,
        [string]$Level = 'INFO'
    )

    $ts = Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff'
    $line = "[{0}] [{1}] {2}" -f $ts, $Level, $Message
    Add-Content -Path $script:LogFile -Value $line -Encoding UTF8
    Write-Host $line
}

function Safe-String {
    param($Value)
    if ($null -eq $Value) { return '' }
    return [string]$Value
}

function Get-HashString {
    param(
        [Parameter(Mandatory = $true)]
        [string]$InputString
    )

    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($InputString)
        $hashBytes = $sha256.ComputeHash($bytes)
        return ([BitConverter]::ToString($hashBytes) -replace '-', '')
    }
    finally {
        $sha256.Dispose()
    }
}

function Get-LeUInt16 {
    param(
        [byte[]]$Bytes,
        [int]$Offset
    )
    return [BitConverter]::ToUInt16($Bytes, $Offset)
}

function Get-LeUInt32 {
    param(
        [byte[]]$Bytes,
        [int]$Offset
    )
    return [BitConverter]::ToUInt32($Bytes, $Offset)
}

function Get-AsciiString {
    param(
        [byte[]]$Bytes,
        [int]$Offset,
        [int]$Length
    )

    return ([System.Text.Encoding]::ASCII.GetString($Bytes, $Offset, $Length)).Trim([char]0).Trim()
}

function Read-Fat32BootSector {
    param(
        [Parameter(Mandatory = $true)]
        [string]$DriveLetter
    )

    $path = "\\.\{0}:" -f $DriveLetter
    $fs = $null
    $br = $null

    try {
        $fs = [System.IO.File]::Open($path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        $br = New-Object System.IO.BinaryReader($fs)
        $sector = $br.ReadBytes(512)

        if ($sector.Length -lt 512) {
            throw 'Cannot read 512 bytes from boot sector.'
        }

        $bytesPerSector    = Get-LeUInt16 $sector 11
        $sectorsPerCluster = $sector[13]
        $reservedSectors   = Get-LeUInt16 $sector 14
        $numFATs           = $sector[16]
        $fatSize16         = Get-LeUInt16 $sector 22
        $totalSectors16    = Get-LeUInt16 $sector 19
        $totalSectors32    = Get-LeUInt32 $sector 32
        $fatSize32         = Get-LeUInt32 $sector 36
        $rootCluster       = Get-LeUInt32 $sector 44
        $volumeId          = Get-LeUInt32 $sector 67
        $volumeLabel       = Get-AsciiString $sector 71 11
        $fsTypeText        = Get-AsciiString $sector 82 8

        $totalSectors = 0
        if ($totalSectors16 -ne 0) {
            $totalSectors = $totalSectors16
        }
        else {
            $totalSectors = $totalSectors32
        }

        if ($fatSize32 -eq 0 -and $fatSize16 -ne 0) {
            $fatSize32 = $fatSize16
        }

        [PSCustomObject]@{
            BytesPerSector    = [uint32]$bytesPerSector
            SectorsPerCluster = [uint32]$sectorsPerCluster
            ReservedSectors   = [uint32]$reservedSectors
            NumFATs           = [uint32]$numFATs
            FATSize32         = [uint32]$fatSize32
            RootCluster       = [uint32]$rootCluster
            VolumeID          = ('{0:X8}' -f $volumeId)
            VolumeLabel       = $volumeLabel
            FsTypeText        = $fsTypeText
            TotalSectors      = [uint64]$totalSectors
        }
    }
    finally {
        if ($br) { $br.Close() }
        if ($fs) { $fs.Close() }
    }
}

function Get-PnpDeviceSafe {
    param([string]$InstanceId)

    try {
        return Get-PnpDevice -InstanceId $InstanceId -ErrorAction Stop
    }
    catch {
        return $null
    }
}

function Get-PnpPropertyValue {
    param(
        [string]$InstanceId,
        [string]$KeyName
    )

    try {
        $prop = Get-PnpDeviceProperty -InstanceId $InstanceId -KeyName $KeyName -ErrorAction Stop
        if ($null -ne $prop.Data) {
            return [string]$prop.Data
        }
        return ''
    }
    catch {
        return ''
    }
}

function Get-ParentInstanceId {
    param([string]$InstanceId)

    return Get-PnpPropertyValue -InstanceId $InstanceId -KeyName 'DEVPKEY_Device_Parent'
}

function Parse-VidPidFromInstanceId {
    param([string]$InstanceId)

    $usbVid = ''
    $usbPid = ''

    if ($InstanceId -match 'VID_([0-9A-Fa-f]{4})') {
        $usbVid = $matches[1].ToUpper()
    }

    if ($InstanceId -match 'PID_([0-9A-Fa-f]{4})') {
        $usbPid = $matches[1].ToUpper()
    }

    [PSCustomObject]@{
        VID = $usbVid
        PID = $usbPid
    }
}

function Get-UsbDescriptorInfoFromDiskPnp {
    param(
        [Parameter(Mandatory = $true)]
        [string]$DiskInstanceId
    )

    $visited = @{}
    $currentId = $DiskInstanceId

    for ($i = 0; $i -lt 8; $i++) {
        if ([string]::IsNullOrWhiteSpace($currentId)) {
            break
        }

        if ($visited.ContainsKey($currentId)) {
            break
        }
        $visited[$currentId] = $true

        $dev = Get-PnpDeviceSafe -InstanceId $currentId
        $friendlyName = ''
        if ($dev) {
            $friendlyName = Safe-String $dev.FriendlyName
        }

        $parsed = Parse-VidPidFromInstanceId -InstanceId $currentId
        if (-not [string]::IsNullOrWhiteSpace($parsed.VID) -or -not [string]::IsNullOrWhiteSpace($parsed.PID)) {
            $manufacturer = Get-PnpPropertyValue -InstanceId $currentId -KeyName 'DEVPKEY_Device_Manufacturer'
            $busReportedDesc = Get-PnpPropertyValue -InstanceId $currentId -KeyName 'DEVPKEY_Device_BusReportedDeviceDesc'
            $serial = ''

            $parts = $currentId -split '\\'
            if ($parts.Count -ge 3) {
                $serial = ($parts[2]).Trim()
            }

            return [PSCustomObject]@{
                Found        = $true
                InstanceId   = $currentId
                VID          = $parsed.VID
                PID          = $parsed.PID
                Manufacturer = $manufacturer
                Product      = $(if (-not [string]::IsNullOrWhiteSpace($busReportedDesc)) { $busReportedDesc } else { $friendlyName })
                Serial       = $serial
            }
        }

        $parentId = Get-ParentInstanceId -InstanceId $currentId
        if ([string]::IsNullOrWhiteSpace($parentId)) {
            break
        }

        $currentId = $parentId
    }

    return [PSCustomObject]@{
        Found        = $false
        InstanceId   = ''
        VID          = ''
        PID          = ''
        Manufacturer = ''
        Product      = ''
        Serial       = ''
    }
}

try {
    $scriptPath = $MyInvocation.MyCommand.Path
    if (-not $scriptPath) {
        throw 'Cannot get script path.'
    }

    $scriptDir = Split-Path -Parent $scriptPath
    $driveRoot = [System.IO.Path]::GetPathRoot($scriptPath)
    $driveLetter = $driveRoot.TrimEnd('\').TrimEnd(':')

    $timeTag = Get-Date -Format 'yyyyMMdd_HHmmss'
    $script:LogFile = Join-Path $scriptDir ("udisk_check_{0}.log" -f $timeTag)
    $cckFile = Join-Path $scriptDir 'udisk.cck'

    Write-Log '========== Start =========='
    Write-Log ("ScriptPath={0}" -f $scriptPath)
    Write-Log ("DriveLetter={0}:" -f $driveLetter)

    $partition = Get-Partition -DriveLetter $driveLetter -ErrorAction Stop
    $disk = Get-Disk -Number $partition.DiskNumber -ErrorAction Stop
    $volume = Get-CimInstance Win32_LogicalDisk | Where-Object { $_.DeviceID -eq ($driveLetter + ':') }
    $diskDrive = Get-CimInstance Win32_DiskDrive | Where-Object { $_.Index -eq $disk.Number }

    if (-not $volume) {
        throw 'Cannot read logical volume info.'
    }

    if (-not $diskDrive) {
        throw 'Cannot read Win32_DiskDrive info.'
    }

    $fileSystem = Safe-String $volume.FileSystem
    $volumeSerial = Safe-String $volume.VolumeSerialNumber
    $diskPnpId = Safe-String $diskDrive.PNPDeviceID

    Write-Log ("DiskNumber={0}, FriendlyName={1}, BusType={2}" -f $disk.Number, (Safe-String $disk.FriendlyName), $disk.BusType)
    Write-Log ("FileSystem={0}, VolumeSerial={1}" -f $fileSystem, $volumeSerial)
    Write-Log ("DiskPNP={0}" -f $diskPnpId)

    $isUsb = $false
    if ($disk.BusType -eq 'USB') {
        $isUsb = $true
        Write-Log 'USB check passed by BusType=USB'
    }
    elseif ((Safe-String $diskDrive.InterfaceType) -eq 'USB') {
        $isUsb = $true
        Write-Log 'USB check passed by InterfaceType=USB'
    }

    if (-not $isUsb) {
        Write-Log 'Check failed: current drive is not USB.' 'ERROR'
        Write-Log '========== End: FAILED =========='
        exit 10
    }

    if ($fileSystem.ToUpper() -ne 'FAT32') {
        Write-Log ("Check failed: filesystem is not FAT32, actual={0}" -f $fileSystem) 'ERROR'
        Write-Log '========== End: FAILED =========='
        exit 20
    }

    Write-Log 'FAT32 check passed.'

    $fat = Read-Fat32BootSector -DriveLetter $driveLetter
    Write-Log ("BootSector BytesPerSector={0}, SectorsPerCluster={1}, ReservedSectors={2}, NumFATs={3}, FATSize32={4}, RootCluster={5}, VolumeID={6}, FsTypeText={7}" -f `
        $fat.BytesPerSector, $fat.SectorsPerCluster, $fat.ReservedSectors, $fat.NumFATs, $fat.FATSize32, $fat.RootCluster, $fat.VolumeID, $fat.FsTypeText)

    if ($fat.FsTypeText.ToUpper() -ne 'FAT32') {
        Write-Log ("Boot sector filesystem text is not FAT32, actual={0}" -f $fat.FsTypeText) 'ERROR'
        Write-Log '========== End: FAILED =========='
        exit 21
    }

    $usbInfo = Get-UsbDescriptorInfoFromDiskPnp -DiskInstanceId $diskPnpId

    if ($usbInfo.Found) {
        Write-Log ("USB descriptor source InstanceId={0}" -f $usbInfo.InstanceId)
        Write-Log ("USB VID={0}, PID={1}, Manufacturer={2}, Product={3}, Serial={4}" -f `
            $usbInfo.VID, $usbInfo.PID, $usbInfo.Manufacturer, $usbInfo.Product, $usbInfo.Serial)
    }
    else {
        Write-Log 'USB descriptor VID/PID node not found. VID/PID/Serial will be empty.' 'WARN'
    }

    $capacityBytes = [uint64]$disk.Size

    # 只保留 STM32 更稳定可读字段参与校验
    $fingerprintSource = @(
        ('USB_VID=' + (Safe-String $usbInfo.VID))
        ('USB_PID=' + (Safe-String $usbInfo.PID))
        ('USB_SERIAL=' + (Safe-String $usbInfo.Serial))
        ('CAPACITY_BYTES=' + $capacityBytes)
        ('FAT_BYTES_PER_SECTOR=' + $fat.BytesPerSector)
        ('FAT_SECTORS_PER_CLUSTER=' + $fat.SectorsPerCluster)
        ('FAT_RESERVED_SECTORS=' + $fat.ReservedSectors)
        ('FAT_NUM_FATS=' + $fat.NumFATs)
        ('FAT_SIZE_32=' + $fat.FATSize32)
        ('FAT_ROOT_CLUSTER=' + $fat.RootCluster)
        ('FAT_VOLUME_ID=' + $fat.VolumeID)
        ('FAT_FS_TYPE=' + $fat.FsTypeText.ToUpper())
    ) -join '|'

    Write-Log ("FingerprintSource={0}" -f $fingerprintSource)

    $uniqueCode = Get-HashString -InputString $fingerprintSource
    Write-Log ("UniqueCode_SHA256={0}" -f $uniqueCode)

    $cckLines = @(
        '# UDisk Check Code - STM32 Stable Fields'
        'FormatVersion=3'
        ('GenerateTime=' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
        ('DriveLetter=' + $driveLetter + ':')
        ('USB_VID=' + (Safe-String $usbInfo.VID))
        ('USB_PID=' + (Safe-String $usbInfo.PID))
        ('USB_SERIAL=' + (Safe-String $usbInfo.Serial))
        ('USB_MANUFACTURER=' + (Safe-String $usbInfo.Manufacturer))
        ('USB_PRODUCT=' + (Safe-String $usbInfo.Product))
        ('CAPACITY_BYTES=' + $capacityBytes)
        ('FAT_BYTES_PER_SECTOR=' + $fat.BytesPerSector)
        ('FAT_SECTORS_PER_CLUSTER=' + $fat.SectorsPerCluster)
        ('FAT_RESERVED_SECTORS=' + $fat.ReservedSectors)
        ('FAT_NUM_FATS=' + $fat.NumFATs)
        ('FAT_SIZE_32=' + $fat.FATSize32)
        ('FAT_ROOT_CLUSTER=' + $fat.RootCluster)
        ('FAT_VOLUME_ID=' + $fat.VolumeID)
        ('FAT_VOLUME_LABEL=' + $fat.VolumeLabel)
        ('FAT_FS_TYPE=' + $fat.FsTypeText.ToUpper())
        ('UNIQUE_CODE_SHA256=' + $uniqueCode)
    )

    Set-Content -Path $cckFile -Value $cckLines -Encoding UTF8

    Write-Log ("CCK file created: {0}" -f $cckFile)
    Write-Log '========== End: SUCCESS =========='
    exit 0
}
catch {
    if (-not $script:LogFile) {
        $fallbackDir = Split-Path -Parent $MyInvocation.MyCommand.Path
        if (-not $fallbackDir) { $fallbackDir = '.' }
        $script:LogFile = Join-Path $fallbackDir ("udisk_check_{0}.log" -f (Get-Date -Format 'yyyyMMdd_HHmmss'))
    }

    Write-Log ("Script exception: {0}" -f $_.Exception.Message) 'ERROR'
    Write-Log '========== End: EXCEPTION =========='
    exit 99
}