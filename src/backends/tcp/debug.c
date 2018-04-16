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

#include "debug.h"
#include <glib.h>    // for g_strdup_vprintf, g_autofree
#include <stdarg.h>  // for va_end, va_list, va_start
#include <stdio.h>   // for fprintf, stderr
#include <unistd.h>  // for getpid

void laik_tcp_debug_real (const char* function, int line, const char* format, ...) {
    laik_tcp_always (function);
    laik_tcp_always (format);

    va_list arguments;

    va_start (arguments, format);
    g_autofree const char* message = g_strdup_vprintf (format, arguments);
    va_end (arguments);

    fprintf (stderr, "%5d\t%35s\t%5d\t%s\n", getpid (), function, line, message);
}
