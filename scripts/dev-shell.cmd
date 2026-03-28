@echo off
set "PATH=%USERPROFILE%\tools\cmake-4.2.3-windows-x86_64\bin;%USERPROFILE%\tools\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin;%LOCALAPPDATA%\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe;%LOCALAPPDATA%\Microsoft\WinGet\Packages\xpack-dev-tools.openocd-xpack_Microsoft.Winget.Source_8wekyb3d8bbwe\xpack-openocd-0.12.0-7\bin;%LOCALAPPDATA%\Programs\Python\Python313;%LOCALAPPDATA%\Programs\Python\Python313\Scripts;C:\Program Files\Git\cmd;%PATH%"
powershell -NoLogo -ExecutionPolicy Bypass -NoExit -Command "Write-Host 'STM32 build environment loaded.'"
