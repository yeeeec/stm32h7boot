<# :
@echo off
setlocal
:: 初始化 bin 目录结构
if not exist "bin" mkdir "bin"
if not exist "bin\crc" mkdir "bin\crc"
if not exist "bin\log" mkdir "bin\log"

echo [1/3] Starting CRC process (Log with timestamp)...
powershell -noprofile "iex (${%~f0} | out-string)"
echo.
echo [3/3] Task finished. Press any key to exit.
pause > nul
exit /b
#>

# --- PowerShell 核心逻辑 ---

$binDir   = "bin"
$jsonPath = Join-Path $binDir "manifest.json"
$crcDir   = Join-Path $binDir "crc"
$logDir   = Join-Path $binDir "log"

# --- 关键修改：生成带时间戳的日志文件名 ---
$dateStr  = Get-Date -Format "yyyyMMdd_HHmmss"
$logFile  = Join-Path $logDir "process_$dateStr.log"

# 日志函数：控制台颜色 + 文件记录
function Write-Log($message, $level = "INFO") {
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $logEntry = "[$timestamp] [$level] $message"
    
    # 根据级别选择颜色
    $color = "White"
    if ($level -eq "SUCCESS") { $color = "Green" }
    elseif ($level -eq "ERROR") { $color = "Red" }
    elseif ($level -eq "WARN") { $color = "Yellow" }
    
    # 输出到终端
    Write-Host $logEntry -ForegroundColor $color
    
    # 写入日志文件 (如果不存在会自动创建)
    Add-Content -Path $logFile -Value $logEntry
}

Write-Log "------------------------------------------"
Write-Log "Session Started. Log file: process_$dateStr.log"

if (-not (Test-Path $jsonPath)) {
    Write-Log "Critical Error: $jsonPath not found!" "ERROR"
    return
}

# 1. 解析 JSON (兼容处理)
$jsonRaw = Get-Content $jsonPath -Raw
$jsonRaw = $jsonRaw -replace '=', ':'
$jsonRaw = $jsonRaw -replace '(?<!")0x([0-9a-fA-F]+)(?!")', '"0x$1"'

try {
    $json = $jsonRaw | ConvertFrom-Json
} catch {
    Write-Log "JSON Parse Error in $jsonPath" "ERROR"
    return
}

# 2. 迭代处理
foreach ($op in $json.operations) {
    $fileName = $op.file
    $sourcePath = Join-Path $binDir $fileName
    
    if (Test-Path $sourcePath) {
        Write-Log "Processing: $fileName"
        
        [byte[]]$bytes = [System.IO.File]::ReadAllBytes($sourcePath)
        
        # 4字节对齐
        $rem = $bytes.Length % 4
        if ($rem -ne 0) {
            $paddingCount = 4 - $rem
            $newBytes = New-Object byte[] ($bytes.Length + $paddingCount)
            [Array]::Copy($bytes, $newBytes, $bytes.Length)
            $bytes = $newBytes
            Write-Log "  - Alignment: Added $paddingCount bytes"
        }

        # 3. STM32 标准 CRC32 计算
        $crc = [Convert]::ToUInt32("FFFFFFFF", 16)
        $poly = [Convert]::ToUInt32("04C11DB7", 16)
        $mask = [Convert]::ToUInt32("80000000", 16)
        $fullMask = [Convert]::ToUInt32("FFFFFFFF", 16)

        for ($i = 0; $i -lt $bytes.Length; $i += 4) {
            $word = [BitConverter]::ToUInt32($bytes, $i)
            $crc = $crc -bxor $word
            for ($j = 0; $j -lt 32; $j++) {
                if (($crc -band $mask) -ne 0) {
                    $crc = (($crc -shl 1) -bxor $poly) -band $fullMask
                } else {
                    $crc = ($crc -shl 1) -band $fullMask
                }
            }
        }

        # 更新 JSON 字段
        $op.size = "0x$($bytes.Length.ToString('X8'))"
        $op.crc32 = "0x$($crc.ToString('X8'))"

        # 4. 获取 Magic Flag (if/else 结构兼容旧版 PS)
        $flagVal = $json.crc_config
        $flag = 0
        if ($flagVal -like "0x*") {
            $flag = [Convert]::ToUInt32($flagVal.Substring(2), 16)
        } else {
            $flag = [uint32]$flagVal
        }
        
        # 5. 写入 bin/crc/ 目录
        $outputPath = Join-Path $crcDir $fileName
        $stream = [System.IO.File]::Create($outputPath)
        $stream.Write([BitConverter]::GetBytes($flag), 0, 4)
        $stream.Write([BitConverter]::GetBytes($crc), 0, 4)
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Close()
        
        Write-Log "Success: $fileName [CRC: $($op.crc32)] [Size: $($op.size)]" "SUCCESS"
    } else {
        Write-Log "File Skip: $fileName not found in bin/" "WARN"
    }
}

# 6. 写回 Manifest
$json | ConvertTo-Json -Depth 10 | Set-Content $jsonPath
Write-Log "Manifest updated."
Write-Log "Session Finished."