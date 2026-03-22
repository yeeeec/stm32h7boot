# 为指定rtt相关ram地址
使用：在SEGGER_RTT_Conf.h增加下面内容
#define SEGGER_RTT_SECTION        ".rtt_cb"
#define SEGGER_RTT_BUFFER_SECTION ".rtt_buf"

ld文件增加下面内容：
  .rtt_cb 0x20000000 (NOLOAD) :
  {
    KEEP(*(.rtt_cb))
  } > RAM

  .rtt_buf 0x20000100 (NOLOAD) :
  {
    KEEP(*(.rtt_buf))
  } > RAM

# 目的，使用DAP-LINK时使用RTT功能
RTTView.start(0x20000000,1024,0)