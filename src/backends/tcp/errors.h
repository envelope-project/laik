#pragma once

#include <glib.h>     // for GError, GQueue, G_DEFINE_AUTOPTR_CLEANUP_FUNC
#include <stdbool.h>  // for bool

typedef GQueue Laik_Tcp_Errors;

__attribute__ ((noreturn))
void laik_tcp_errors_abort (Laik_Tcp_Errors* this);

void laik_tcp_errors_clear (Laik_Tcp_Errors* this);

void laik_tcp_errors_free (Laik_Tcp_Errors* this);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (Laik_Tcp_Errors, laik_tcp_errors_free)

__attribute__ ((warn_unused_result))
Laik_Tcp_Errors* laik_tcp_errors_new (void);

__attribute__ ((warn_unused_result))
bool laik_tcp_errors_matches (Laik_Tcp_Errors* this, const char* domain, int code);

__attribute__ ((warn_unused_result))
bool laik_tcp_errors_present (Laik_Tcp_Errors* this);

__attribute__ ((format (printf, 4, 5)))
void laik_tcp_errors_push (Laik_Tcp_Errors* this, const char* domain, int code, const char* format, ...);

void laik_tcp_errors_push_direct (Laik_Tcp_Errors* this, GError* error);

void laik_tcp_errors_push_other (Laik_Tcp_Errors* this, Laik_Tcp_Errors* other);

__attribute__ ((warn_unused_result))
char* laik_tcp_errors_show (Laik_Tcp_Errors* this);
