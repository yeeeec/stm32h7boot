# STM32H7 Bootloader 开发环境记录

验证日期：2026-03-25

本文档记录当前机器上 `stm32h7boot` 工程的开发环境检查结果。

## 1. 必需构建工具

以下工具已经安装，并已在当前工程中验证可用：

| 工具 | 版本 | 状态 | 预期位置 |
| --- | --- | --- | --- |
| CMake | 4.2.3 | 已安装可用 | `C:\Users\eqcn\tools\cmake-4.2.3-windows-x86_64\bin\cmake.exe` |
| Ninja | 1.13.2 | 已安装可用 | `C:\Users\eqcn\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe\ninja.exe` |
| GNU Arm Embedded GCC | 15.2.1 | 已安装可用 | `C:\Users\eqcn\tools\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin\arm-none-eabi-gcc.exe` |
| GNU Arm Embedded GDB | 16.3 | 已安装可用 | `C:\Users\eqcn\tools\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin\arm-none-eabi-gdb.exe` |
| GNU `objcopy` | 2.45.1 | 已安装可用 | `C:\Users\eqcn\tools\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin\arm-none-eabi-objcopy.exe` |
| GNU `size` | 2.45.1 | 已安装可用 | `C:\Users\eqcn\tools\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin\arm-none-eabi-size.exe` |
| OpenOCD | 0.12.0 | 已安装可用 | `C:\Users\eqcn\AppData\Local\Microsoft\WinGet\Packages\xpack-dev-tools.openocd-xpack_Microsoft.Winget.Source_8wekyb3d8bbwe\xpack-openocd-0.12.0-7\bin\openocd.exe` |
| Python | 3.13.12 | 已安装可用 | `C:\Users\eqcn\AppData\Local\Programs\Python\Python313\python.exe` |
| Git | 2.53.0.windows.2 | 已安装可用 | `C:\Program Files\Git\cmd\git.exe` |

## 2. 工程验证结果

已实际执行以下命令验证工程可正常配置和构建：

```powershell
cmake --preset Debug
cmake --build --preset Debug --target clean
cmake --build --preset Debug -j 8
```

验证结果：

- `CMake configure` 成功。
- `clean + rebuild` 成功。
- 已生成输出文件：`build/Debug/stm32h7boot.elf`

说明：

- 当前工程构建链路完整可用。
- 构建过程中存在若干编译警告，但不会导致构建失败。

## 3. VS Code 相关环境

工程推荐安装的扩展：

- `ms-vscode.cpptools`
- `ms-vscode.cmake-tools`
- `marus25.cortex-debug`

当前机器已确认安装：

- `ms-vscode.cpptools`
- `ms-vscode.cmake-tools`
- `marus25.cortex-debug`
- `mcu-debug.debug-tracker-vscode`
- `mcu-debug.memory-view`
- `mcu-debug.peripheral-viewer`
- `mcu-debug.rtos-views`

## 4. 当前未安装但属于可选项的组件

以下组件不是当前 CMake/Ninja 构建所必需，但在某些场景下会用到：

| 组件 | 状态 | 影响 |
| --- | --- | --- |
| STM32CubeMX | 未安装 | 如果需要根据 `stm32h7boot.ioc` 重新生成代码，则需要安装 |
| SEGGER J-Link GDB Server | 未安装 | `.vscode/launch.json` 中的 J-Link 调试配置当前不可用 |
| `clang-format` | 未安装 | 只影响代码格式化，不影响编译 |
| `make` | 未安装 | 当前工程未使用，不影响构建 |

## 5. PowerShell 说明

当前 PowerShell 执行策略会阻止直接 `source` 本地的 `scripts\dev-env.ps1`。

可使用以下方式之一：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\dev-env.ps1
```

或者：

```bat
scripts\dev-shell.cmd
```

## 6. 工程中的环境配置文件

当前工程的开发环境配置主要来自以下文件：

- `CMakePresets.json`
- `scripts/dev-env.ps1`
- `scripts/dev-shell.cmd`
- `.vscode/tasks.json`
- `.vscode/launch.json`
- `.vscode/extensions.json`

## 7. 结论

`stm32h7boot` 当前所需的主开发环境已经安装完成，并且已经在本机成功通过构建验证。

当前状态总结：

- 主构建工具链完整可用。
- VS Code 下的 `ST-Link + OpenOCD` 调试链路具备基础条件。
- J-Link 相关调试组件未安装。
- STM32CubeMX 未安装，因此暂不具备 `.ioc` 重新生成代码的能力。
