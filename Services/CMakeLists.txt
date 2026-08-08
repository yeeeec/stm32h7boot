add_library(firmware_services STATIC
    # 先列出用例层对象，再列出其能力层依赖，保证静态库消费者无需链接组也能
    # 正确拉取一个完整用例所需的对象文件。
    src/use_case/update_service.c
    src/use_case/active_validation_service.c
    src/use_case/launch_service.c
    src/capability/checked_arithmetic.c
    src/capability/json_document.c
    src/capability/manifest_service.c
    src/capability/update_request_service.c
    src/capability/boot_control_service.c
    src/capability/vector_validation.c
    src/capability/version_policy.c
    src/runtime_layout.c
)

target_include_directories(firmware_services PUBLIC
    # 对上层公开稳定 API、领域类型和纯能力声明。
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(firmware_services PRIVATE
    # 内部状态对象和 JSON 解析细节不得传播到 Application。
    ${CMAKE_CURRENT_SOURCE_DIR}/internal
)

target_link_libraries(firmware_services
    # 领域基础类型是 Services 对其调用者的公开依赖。
    PUBLIC firmware_types
    # 端口接口和日志实现只在 Services 内部使用。
    PRIVATE firmware_interfaces firmware_logger
)
