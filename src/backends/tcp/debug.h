#pragma once

#include <assert.h>  // for assert

#ifdef LAIK_TCP_DEBUG
#define laik_tcp_always(...) assert (__VA_ARGS__)
#define laik_tcp_debug(...) laik_tcp_debug_real (__func__, __LINE__, __VA_ARGS__)
#else
#define laik_tcp_always(...)
#define laik_tcp_debug(...)
#endif

__attribute__ ((format (printf, 3, 4)))
void laik_tcp_debug_real (const char* function, int line, const char* format, ...);
