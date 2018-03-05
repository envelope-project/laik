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
