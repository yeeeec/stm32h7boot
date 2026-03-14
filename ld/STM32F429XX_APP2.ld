ENTRY(Reset_Handler)

MEMORY
{
  RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 192K
  CCMRAM (xrw) : ORIGIN = 0x10000000, LENGTH = 64K
  FLASH  (rx)  : ORIGIN = 0x08120000, LENGTH = 896K
}

INCLUDE "common_sections.ld"
