<# :
@echo off
title CCK 硬件锁_32位二进制版
setlocal

:: 自定义扰动值 (Salt)
set "SALT=MySecretSalt2024"
set "CUR_DRIVE=%~d0"

powershell -NoProfile -ExecutionPolicy Bypass -Command "iex ((Get-Content -LiteralPath '%~f0') -join [Environment]::NewLine)"

echo.
echo ----------------------------------------------------
pause
exit /b
#>

# --- PowerShell 代码开始 ---

$salt = $env:SALT
$target = $env:CUR_DRIVE

# 1. 获取逻辑盘信息 (修正了 Win32_LogicalDisk 的命名)
$logDisk = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='$target'"
if (-not $logDisk) { Write-Host "[错误] 无法定位盘符 $target" -ForegroundColor Red; return }

# 2. 找到该盘符对应的磁盘索引
$partition = Get-CimInstance Win32_DiskPartition | Where-Object { 
    $_.DeviceID -eq (Get-CimInstance -Query "ASSOCIATORS OF {Win32_LogicalDisk.DeviceID='$target'} WHERE AssocClass=Win32_LogicalDiskToPartition" -ErrorAction SilentlyContinue).DeviceID
}

# 兜底匹配方案
if (-not $partition) {
    $allParts = Get-CimInstance Win32_DiskPartition
    foreach($p in $allParts) {
        $checkLetter = (Get-CimInstance -Query "ASSOCIATORS OF {Win32_DiskPartition.DeviceID='$($p.DeviceID)'} WHERE AssocClass=Win32_LogicalDiskToPartition" -ErrorAction SilentlyContinue).DeviceID
        if($checkLetter -eq $target) { $partition = $p; break }
    }
}

# 3. 锁定物理驱动器
$drive = Get-CimInstance Win32_DiskDrive | Where-Object { $_.Index -eq $partition.DiskIndex }

# 4. 核心校验：必须是 USB 设备
if ($drive.InterfaceType -ne "USB") {
    Write-Host "----------------------------------------------------"
    Write-Host "[终止] 当前盘符 $target 属于 $($drive.InterfaceType) 硬盘。" -ForegroundColor Yellow
    Write-Host "[提示] 请将脚本移动到 U 盘内运行。" -ForegroundColor Gray
    return
}

# 5. 提取参数
$v_id="0000"; $p_id="0000"; $sn = $drive.SerialNumber.Trim(); $vol = $logDisk.VolumeSerialNumber

# 强力抓取 VID/PID
$hwIdString = ($drive.HardwareID -join " ") + (Get-CimInstance Win32_PnPEntity | Where-Object { $_.DeviceID -like "*$sn*" }).DeviceID
if ($hwIdString -match 'VID_([0-9A-F]{4})&PID_([0-9A-F]{4})') {
    $v_id = $Matches[1]; $p_id = $Matches[2]
}

# 6. 计算指纹 (CRC32)
$raw = "$v_id$p_id$sn$vol$salt"; $bytes = [Text.Encoding]::UTF8.GetBytes($raw)
$poly = [Convert]::ToUInt32("EDB88320", 16); $table = New-Object uint32[] 256
for ($i=0; $i-lt 256; $i++) { $c = [uint32]$i; for ($j=0; $j-lt 8; $j++) { $c = if ($c -band 1) { ($c -shr 1) -bxor $poly } else { $c -shr 1 } }; $table[$i] = $c }
$uMax = [Convert]::ToUInt32("FFFFFFFF", 16); $crc = $uMax
foreach ($b in $bytes) { $crc = ($crc -shr 8) -bxor $table[($crc -band 255) -bxor $b] }
$finalCrc = $crc -bxor $uMax

# 7. 二进制写入 (确保文件大小为 4 字节)
$f = Join-Path "$target\" "cck"
if (Test-Path $f) { (Get-Item $f -Force).Attributes = "Normal" }

# 将 uint32 转换为字节数组 (4字节)
$binData = [System.BitConverter]::GetBytes($finalCrc)
# 强制写入原始字节
[System.IO.File]::WriteAllBytes($f, $binData)

# 设置属性：隐藏 + 只读
(Get-Item $f -Force).Attributes = "ReadOnly, Hidden"

# 输出回显
Write-Host "----------------------------------------------------"
Write-Host "[当前盘符]: $target" -ForegroundColor Green
Write-Host "[VID/PID]:  $v_id / $p_id"
Write-Host "[硬件 SN]:  $sn"
Write-Host "[卷序列号]: $vol"
Write-Host "----------------------------------------------------"
Write-Host "[最终指纹]: $($finalCrc.ToString('X8'))" -ForegroundColor Cyan
Write-Host "[文件大小]: 4 Bytes (Raw Binary)" -ForegroundColor Yellow