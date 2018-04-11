#pragma once

#include "time.h" // for laik_tcp_time

#ifdef LAIK_TCP_STATS
#define laik_tcp_stats_change(change, ...) laik_tcp_stats_change_real (change, __VA_ARGS__)
#define laik_tcp_stats_count(...)          laik_tcp_stats_change_real (1.0, __VA_ARGS__)
#define laik_tcp_stats_remove(...)         laik_tcp_stats_remove_real (__VA_ARGS__)
#define laik_tcp_stats_reset()             laik_tcp_stats_reset_real ()
#define laik_tcp_stats_start(variable)     double variable = laik_tcp_time ()
#define laik_tcp_stats_stop(variable, ...) laik_tcp_stats_change_real (laik_tcp_time () - variable, __VA_ARGS__)
#define laik_tcp_stats_store(...)          laik_tcp_stats_store_real (__VA_ARGS__)
#else
#define laik_tcp_stats_change(change, ...)
#define laik_tcp_stats_count(...)
#define laik_tcp_stats_remove(...)
#define laik_tcp_stats_reset()
#define laik_tcp_stats_start(variable)
#define laik_tcp_stats_stop(variable, ...)
#define laik_tcp_stats_store(...)
#endif

__attribute__ ((format (printf, 2, 3)))
void laik_tcp_stats_change_real (double change, const char* format, ...);

__attribute__ ((format (printf, 1, 2)))
void laik_tcp_stats_remove_real (const char* format, ...);

void laik_tcp_stats_reset_real (void);

__attribute__ ((format (printf, 1, 2)))
void laik_tcp_stats_store_real (const char* format, ...);
