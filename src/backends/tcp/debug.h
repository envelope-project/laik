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
