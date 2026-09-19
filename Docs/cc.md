flowchart TD
    A["Boot Application<br/>启动状态机和决策"] --> S["Services<br/>升级业务操作"]
    A --> P["Platform<br/>STM32H743具体能力"]
    S --> P
    P --> D["Drivers<br/>W25Q256 / AT24 / ROM Boot"]
    P --> B["BSP<br/>QSPI / I2C / UART / GPIO"]
    P --> F["FatFs"]
    D -. "Platform绑定回调" .-> B
    B --> H["HAL / CubeMX"]
    F --> H