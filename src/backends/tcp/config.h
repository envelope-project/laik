#pragma once

#include <glib.h>    // for GPtrArray, G_DEFINE_AUTOPTR_CLEANUP_FUNC
#include <stddef.h>  // for size_t

typedef struct {
    GPtrArray* addresses;
    double     client_activation_timeout;
    size_t     client_connection_limit;
    double     client_connection_timeout;
    double     server_activation_timeout;
    size_t     server_connection_limit;
    double     server_connection_timeout;
    size_t     socket_backlog;
    size_t     socket_keepcnt;
    double     socket_keepidle;
    double     socket_keepintvl;
    size_t     inbox_size_limit;
    size_t     outbox_size_limit;
    double     add_retry_timeout;
    double     get_first_timeout;
    double     get_retry_timeout;

    int references;
} Laik_Tcp_Config;

Laik_Tcp_Config* laik_tcp_config ();

__attribute__ ((warn_unused_result))
Laik_Tcp_Config* laik_tcp_config_ref (Laik_Tcp_Config* this);

void laik_tcp_config_unref (Laik_Tcp_Config* this);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (Laik_Tcp_Config, laik_tcp_config_unref)
