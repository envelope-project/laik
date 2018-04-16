/*
 * This file is part of the LAIK library.
 * Copyright (c) 2018 Alexander Kurtz <alexander@kurtz.be>
 *
 * LAIK is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 3 or later.
 *
 * LAIK is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

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
