#ifndef PLATFORM_RESET_H
#define PLATFORM_RESET_H

#if defined(__GNUC__)
#define PLATFORM_NORETURN __attribute__((noreturn))
#else
#define PLATFORM_NORETURN
#endif

PLATFORM_NORETURN void Platform_Reset(void);

#endif
