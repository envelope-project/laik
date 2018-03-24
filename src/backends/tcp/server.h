#pragma once

#include <glib.h>    // for G_DEFINE_AUTOPTR_CLEANUP_FUNC
#include <stddef.h>  // for size_t
#include "socket.h"  // for Laik_Tcp_Socket

typedef struct Laik_Tcp_Server Laik_Tcp_Server;

__attribute__  ((warn_unused_result))
Laik_Tcp_Socket* laik_tcp_server_accept (Laik_Tcp_Server* this, double seconds);

void laik_tcp_server_free (Laik_Tcp_Server* this);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (Laik_Tcp_Server, laik_tcp_server_free)

__attribute__  ((warn_unused_result))
Laik_Tcp_Server* laik_tcp_server_new (Laik_Tcp_Socket* socket, size_t limit);

void laik_tcp_server_store (Laik_Tcp_Server* this, Laik_Tcp_Socket* socket);
