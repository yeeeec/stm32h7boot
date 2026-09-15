set(FIRMWARE_APPLICATION_SOURCES
    src/application.c
    src/application_ui.c
    src/messaging/application_channels.c
    src/state/hmi_device_state.c
    src/state/task_health.c
    src/tasks/system_task.c
    src/tasks/alarm_task.c
    src/tasks/comm_task.c
    src/tasks/app_ui_task.c
    src/tasks/touch_task.c
    src/tasks/update_task.c
    src/tasks/log_task.c
    src/tasks/storage_task.c
    src/tasks/parameters_task.c
)

add_library(firmware_application STATIC ${FIRMWARE_APPLICATION_SOURCES})

target_include_directories(firmware_application PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(firmware_application PRIVATE
    ${CMAKE_SOURCE_DIR}/Core/Inc
)

target_link_libraries(firmware_application PRIVATE
    communication
    update
    firmware_logger
    firmware_storage_common
    parameters
    platform_stm32h7
)

target_link_libraries(firmware_application PUBLIC
    firmware_types
)
