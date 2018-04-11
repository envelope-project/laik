#pragma once

#include <glib.h>     // for GPtrArray, G_DEFINE_AUTOPTR_CLEANUP_FUNC
#include <stdbool.h>  // for bool
#include <stddef.h>   // for size_t

typedef struct {
    GPtrArray* addresses;
    bool       backend_async_send;
    size_t     client_connections;
    size_t     client_threads;
    size_t     server_connections;
    size_t     server_threads;
    size_t     socket_backlog;
    double     socket_timeout;
    size_t     inbox_size;
    size_t     outbox_size;
    size_t     send_attempts;
    double     send_delay;
    size_t     receive_attempts;
    double     receive_timeout;
    double     receive_delay;
    bool       minimpi_async_split;

    int references;
} Laik_Tcp_Config;

Laik_Tcp_Config* laik_tcp_config ();

__attribute__ ((warn_unused_result))
Laik_Tcp_Config* laik_tcp_config_ref (Laik_Tcp_Config* this);

void laik_tcp_config_unref (Laik_Tcp_Config* this);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (Laik_Tcp_Config, laik_tcp_config_unref)
