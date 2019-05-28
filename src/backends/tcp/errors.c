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

#include "errors.h"
#include <glib.h>    // for GError, g_quark_from_string, g_queue_push_head
#include <stdarg.h>  // for va_end, va_list, va_start
#include <stdio.h>   // for fflush, fprintf, NULL, stderr
#include <stdlib.h>  // for abort
#include <laik-backend-tcp.h>
#include "debug.h"   // for laik_tcp_always

LaikTCPErrorHandler abortErrorHandler;
int statusFlag = 0;
Laik_Tcp_Errors *errorTrace;

static void laik_tcp_errors_show1(void *data, void *userdata) {
    laik_tcp_always (data);
    laik_tcp_always (userdata);

    GError *error = data;
    GString *string = userdata;

    g_string_append_printf(string, " => Domain %s encountered error #%d: %s\n", g_quark_to_string(error->domain),
                           error->code, error->message);
}

void laik_tcp_errors_abort(Laik_Tcp_Errors *this) {
    laik_tcp_always (this);

    statusFlag = -1;
    errorTrace = this;

    if (abortErrorHandler != NULL) {
        fprintf(stderr, "[LAIK TCP Backend] Error handler found, attempting to handle error.\n");
        abortErrorHandler(this);
        fprintf(stderr, "[LAIK TCP Backend] Error handler exited, attempting to continue\n");
    } else {
        fprintf(stderr, "[LAIK TCP Backend] Error occurred with no handler set. "
                "Aborting, the contents of the error stack follow:\n%s",
                laik_tcp_errors_show(this));
        fflush(stderr);
        abort();
    }
}

void laik_tcp_errors_clear(Laik_Tcp_Errors *this) {
    laik_tcp_always (this);

    GError *error = NULL;

    while ((error = g_queue_pop_head(this))) {
        g_error_free(error);
    }
}

void laik_tcp_errors_free(Laik_Tcp_Errors *this) {
    if (!this) {
        return;
    }

    laik_tcp_errors_clear(this);

    g_queue_free(this);
}

Laik_Tcp_Errors *laik_tcp_errors_new(void) {
    return g_queue_new();
}

bool laik_tcp_errors_matches(Laik_Tcp_Errors *this, const char *domain, int code) {
    laik_tcp_always (this);
    laik_tcp_always (domain);

    return g_error_matches(g_queue_peek_head(this), g_quark_from_string(domain), code);
}

bool laik_tcp_errors_present(Laik_Tcp_Errors *this) {
    laik_tcp_always (this);

    return !g_queue_is_empty(this);
}

void laik_tcp_errors_push(Laik_Tcp_Errors *this, const char *domain, int code, const char *format, ...) {
    laik_tcp_always (this);
    laik_tcp_always (domain);
    laik_tcp_always (format);

    va_list arguments;

    va_start (arguments, format);
    g_queue_push_head(this, g_error_new_valist(g_quark_from_string(domain), code, format, arguments));
    va_end (arguments);
}

void laik_tcp_errors_push_direct(Laik_Tcp_Errors *this, GError *error) {
    laik_tcp_always (this);
    laik_tcp_always (error);

    g_queue_push_head(this, error);
}

void laik_tcp_errors_push_other(Laik_Tcp_Errors *this, Laik_Tcp_Errors *other) {
    laik_tcp_always (this);
    laik_tcp_always (other);

    size_t size = g_queue_get_length(other);

    for (ssize_t i = size - 1; i >= 0; i--) {
        GError *error = g_queue_peek_nth(other, i);
        laik_tcp_errors_push_direct(this, g_error_copy(error));
    }
}

char *laik_tcp_errors_show(Laik_Tcp_Errors *this) {
    GString *result = g_string_new(NULL);

    g_queue_foreach(this, laik_tcp_errors_show1, result);

    return g_string_free(result, false);
}

void laik_tcp_set_error_handler(LaikTCPErrorHandler newErrorHandler) {
    abortErrorHandler = newErrorHandler;
}

int laik_tcp_get_status() {
    return statusFlag;
}

Laik_Tcp_Errors *laik_tcp_get_error_trace() {
    return errorTrace;
}
