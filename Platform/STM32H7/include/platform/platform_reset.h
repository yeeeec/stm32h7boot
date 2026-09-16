#ifndef PLATFORM_RESET_H
#define PLATFORM_RESET_H

#ifdef __cplusplus
extern "C"
{
#endif

    typedef void (*platform_reset_fn)(void *context);
    void PlatformReset_Bind(platform_reset_fn reset, void *context);
    void PlatformReset_Request(void);

#ifdef __cplusplus
}
#endif

#endif
