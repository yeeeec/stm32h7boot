$toolDirs = @(
    (Join-Path $env:USERPROFILE "tools\\cmake-4.2.3-windows-x86_64\\bin"),
    (Join-Path $env:USERPROFILE "tools\\xpack-arm-none-eabi-gcc-15.2.1-1.1\\bin"),
    (Join-Path $env:LOCALAPPDATA "Microsoft\\WinGet\\Packages\\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe"),
    (Join-Path $env:LOCALAPPDATA "Microsoft\\WinGet\\Packages\\xpack-dev-tools.openocd-xpack_Microsoft.Winget.Source_8wekyb3d8bbwe\\xpack-openocd-0.12.0-7\\bin"),
    (Join-Path $env:LOCALAPPDATA "Programs\\Python\\Python313"),
    (Join-Path $env:LOCALAPPDATA "Programs\\Python\\Python313\\Scripts"),
    "C:\\Program Files\\Git\\cmd"
)

$existingParts = @($env:Path -split ';' | Where-Object { $_ -and $_.Trim() -ne "" })
$mergedParts = @()

foreach ($dir in $toolDirs + $existingParts) {
    if (($mergedParts -notcontains $dir) -and (Test-Path $dir)) {
        $mergedParts += $dir
    }
}

$env:Path = ($mergedParts -join ';')

Write-Host "STM32 build environment loaded for this PowerShell session."
Write-Host ("cmake  : " + (cmake --version | Select-Object -First 1))
Write-Host ("ninja  : " + (ninja --version))
Write-Host ("gcc    : " + ((arm-none-eabi-gcc --version | Select-Object -First 1)))
Write-Host ("gdb    : " + ((arm-none-eabi-gdb --version | Select-Object -First 1)))
Write-Host ("openocd: " + ((openocd --version | Select-Object -First 1)))
Write-Host ("python : " + (python --version))
Write-Host ("git    : " + (git --version))
