#pragma once

#include <glib.h>    // for G_DEFINE_AUTOPTR_CLEANUP_FUNC
#include "socket.h"  // for Laik_Tcp_Socket

typedef struct {
    Laik_Tcp_Socket* primary;
    Laik_Tcp_Socket* secondary;
} Laik_Tcp_SocketPair;

void laik_tcp_socket_pair_free (Laik_Tcp_SocketPair* this);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (Laik_Tcp_SocketPair, laik_tcp_socket_pair_free)

__attribute__ ((warn_unused_result))
Laik_Tcp_SocketPair* laik_tcp_socket_pair_new (void);
