# RTT RAM 配置

`SEGGER_RTT_Conf.h` 已将控制块和缓冲区分别放到下面两个 section：

```c
#define SEGGER_RTT_SECTION        ".rtt_cb"
#define SEGGER_RTT_BUFFER_SECTION ".rtt_buf"
```

`STM32H743XX_FLASH.ld` 将这两个 section 放在 AXI SRAM 的 non-cacheable 区域：

```ld
  .rtt_cb (NOLOAD) :
  {
    . = ALIGN(32);
    KEEP(*(.rtt_cb))
    . = ALIGN(32);
  } > RAM

  .rtt_buf (NOLOAD) :
  {
    . = ALIGN(32);
    KEEP(*(.rtt_buf))
    . = ALIGN(32);
  } > RAM
```

不要在调试工具中硬编码 `0x20000000`。实际地址由 linker 根据 ELF 布局决定；
调试器应从 ELF 中读取 `_SEGGER_RTT` 符号，或使用 map 文件中的 `.rtt_cb` 地址。
