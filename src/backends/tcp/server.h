#pragma once

#include <glib.h>     // for G_DEFINE_AUTOPTR_CLEANUP_FUNC
#include <stdbool.h>  // for bool
#include "socket.h"   // for Laik_Tcp_Socket

typedef bool (Laik_Tcp_ServerFunction) (Laik_Tcp_Socket* socket, void* userdata);

typedef struct Laik_Tcp_Server Laik_Tcp_Server;

void laik_tcp_server_free (Laik_Tcp_Server* this);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (Laik_Tcp_Server, laik_tcp_server_free)

__attribute__  ((warn_unused_result))
Laik_Tcp_Server* laik_tcp_server_new (Laik_Tcp_Socket* socket, Laik_Tcp_ServerFunction* function, void* userdata);
