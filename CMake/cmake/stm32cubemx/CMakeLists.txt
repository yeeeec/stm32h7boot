cmake_minimum_required(VERSION 3.22)
# Enable CMake support for ASM and C languages
enable_language(C ASM)
# STM32CubeMX generated symbols (macros)
set(MX_Defines_Syms 
	USE_PWR_LDO_SUPPLY 
	USE_HAL_DRIVER 
	STM32H743xx 
	STM32_THREAD_SAFE_STRATEGY=4
    $<$<CONFIG:Debug>:DEBUG>
)

# STM32CubeMX generated include paths
set(MX_Include_Dirs
    ${CMAKE_SOURCE_DIR}/../Core/Inc
    ${CMAKE_SOURCE_DIR}/../FATFS/Target
    ${CMAKE_SOURCE_DIR}/../FATFS/App
    ${CMAKE_SOURCE_DIR}/../USB_HOST/App
    ${CMAKE_SOURCE_DIR}/../USB_HOST/Target
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Inc
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy
    ${CMAKE_SOURCE_DIR}/../Middlewares/Third_Party/FreeRTOS/Source/include
    ${CMAKE_SOURCE_DIR}/../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2
    ${CMAKE_SOURCE_DIR}/../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F
    ${CMAKE_SOURCE_DIR}/../Middlewares/Third_Party/FatFs/src
    ${CMAKE_SOURCE_DIR}/../Middlewares/ST/STM32_USB_Host_Library/Core/Inc
    ${CMAKE_SOURCE_DIR}/../Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Inc
    ${CMAKE_SOURCE_DIR}/../Drivers/CMSIS/Device/ST/STM32H7xx/Include
    ${CMAKE_SOURCE_DIR}/../Drivers/CMSIS/Include
)

# STM32CubeMX generated application sources
set(MX_Application_Src
    ${CMAKE_SOURCE_DIR}/../USB_HOST/Target/usbh_conf.c
    ${CMAKE_SOURCE_DIR}/../USB_HOST/App/usb_host.c
    ${CMAKE_SOURCE_DIR}/../FATFS/App/fatfs.c
    ${CMAKE_SOURCE_DIR}/../FATFS/Target/usbh_diskio.c
    ${CMAKE_SOURCE_DIR}/../Core/Src/main.c
    ${CMAKE_SOURCE_DIR}/../Core/Src/freertos.c
    ${CMAKE_SOURCE_DIR}/../Core/Src/stm32h7xx_it.c
    ${CMAKE_SOURCE_DIR}/../Core/Src/stm32h7xx_hal_msp.c
    ${CMAKE_SOURCE_DIR}/../Core/Src/stm32h7xx_hal_timebase_tim.c
    ${CMAKE_SOURCE_DIR}/../Core/Src/sysmem.c
    ${CMAKE_SOURCE_DIR}/../Core/Src/syscalls.c
    ${CMAKE_SOURCE_DIR}/startup_stm32h743xx.s
)

# STM32 HAL/LL Drivers
set(STM32_Drivers_Src
    ${CMAKE_SOURCE_DIR}/../Core/Src/system_stm32h7xx.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim_ex.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_hcd.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc_ex.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash_ex.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_gpio.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_hsem.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma_ex.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_mdma.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr_ex.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c_ex.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_exti.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_crc.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_crc_ex.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma2d.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_fmc.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_nor.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_sram.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_nand.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_sdram.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_ltdc.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_ltdc_ex.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_qspi.c
    ${CMAKE_SOURCE_DIR}/../Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_delayblock.c
)

# Drivers Midllewares


set(FreeRTOS_Src
    ${CMAKE_SOURCE_DIR}/../Middlewares/Third_Party/FreeRTOS/Source/croutine.c
    ${CMAKE_SOURCE_DIR}/../Middlewares/Third_Party/FreeRTOS/Source/event_groups.c
    ${CMAKE_SOURCE_DIR}/../Middlewares/Third_Party/FreeRTOS/Source/list.c
    ${CMAKE_SOURCE_DIR}/../Middlewares/Third_Party/FreeRTOS/Source/queue.c
    ${CMAKE_SOURCE_DIR}/../Middlewares/Third_Party/FreeRTOS/Source/stream_buffer.c
    ${CMAKE_SOURCE_DIR}/../Middlewares/Third_Party/FreeRTOS/Source/tasks.c
    ${CMAKE_SOURCE_DIR}/../Middlewares/Third_Party/FreeRTOS/Source/timers.c
    ${CMAKE_SOURCE_DIR}/../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/cmsis_os2.c
    ${CMAKE_SOURCE_DIR}/../Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c
    ${CMAKE_SOURCE_DIR}/../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c
)
set(FatFs_Src
    ${CMAKE_SOURCE_DIR}/../Middlewares/Third_Party/FatFs/src/diskio.c
    ${CMAKE_SOURCE_DIR}/../Middlewares/Third_Party/FatFs/src/ff.c
    ${CMAKE_SOURCE_DIR}/../Middlewares/Third_Party/FatFs/src/ff_gen_drv.c
    ${CMAKE_SOURCE_DIR}/../Middlewares/Third_Party/FatFs/src/option/syscall.c
)
set(USB_Host_Library_Src
    ${CMAKE_SOURCE_DIR}/../Middlewares/ST/STM32_USB_Host_Library/Core/Src/usbh_core.c
    ${CMAKE_SOURCE_DIR}/../Middlewares/ST/STM32_USB_Host_Library/Core/Src/usbh_ctlreq.c
    ${CMAKE_SOURCE_DIR}/../Middlewares/ST/STM32_USB_Host_Library/Core/Src/usbh_ioreq.c
    ${CMAKE_SOURCE_DIR}/../Middlewares/ST/STM32_USB_Host_Library/Core/Src/usbh_pipes.c
    ${CMAKE_SOURCE_DIR}/../Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc.c
    ${CMAKE_SOURCE_DIR}/../Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc_bot.c
    ${CMAKE_SOURCE_DIR}/../Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Src/usbh_msc_scsi.c
)

# Link directories setup
set(MX_LINK_DIRS

)
# Project static libraries
set(MX_LINK_LIBS 
    STM32_Drivers
    FreeRTOS	FatFs	USB_Host_Library	
)
# Interface library for includes and symbols
add_library(stm32cubemx INTERFACE)
target_include_directories(stm32cubemx INTERFACE ${MX_Include_Dirs})
target_compile_definitions(stm32cubemx INTERFACE ${MX_Defines_Syms})

# Create STM32_Drivers static library
add_library(STM32_Drivers OBJECT)
target_sources(STM32_Drivers PRIVATE ${STM32_Drivers_Src})
target_link_libraries(STM32_Drivers PUBLIC stm32cubemx)


# Create FreeRTOS static library
add_library(FreeRTOS OBJECT)
target_sources(FreeRTOS PRIVATE ${FreeRTOS_Src})
target_link_libraries(FreeRTOS PUBLIC stm32cubemx)

# Create FatFs static library
add_library(FatFs OBJECT)
target_sources(FatFs PRIVATE ${FatFs_Src})
target_link_libraries(FatFs PUBLIC stm32cubemx)

# Create USB_Host_Library static library
add_library(USB_Host_Library OBJECT)
target_sources(USB_Host_Library PRIVATE ${USB_Host_Library_Src})
target_link_libraries(USB_Host_Library PUBLIC stm32cubemx)

# Add STM32CubeMX generated application sources to the project
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${MX_Application_Src})

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE ${MX_LINK_DIRS})

# Add libraries to the project
target_link_libraries(${CMAKE_PROJECT_NAME} ${MX_LINK_LIBS})

# Add the map file to the list of files to be removed with 'clean' target
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES ADDITIONAL_CLEAN_FILES ${CMAKE_PROJECT_NAME}.map)

# Validate that STM32CubeMX code is compatible with C standard
if((CMAKE_C_STANDARD EQUAL 90) OR (CMAKE_C_STANDARD EQUAL 99))
    message(ERROR "Generated code requires C11 or higher")
endif()
