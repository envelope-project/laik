#pragma once

#include <glib.h>    // for G_DEFINE_AUTOPTR_CLEANUP_FUNC
#include <stddef.h>  // for size_t
#include "socket.h"  // for Laik_Tcp_Socket

typedef struct Laik_Tcp_SocketQueue Laik_Tcp_SocketQueue;

void laik_tcp_socket_queue_cancel (Laik_Tcp_SocketQueue* this);

void laik_tcp_socket_queue_free (Laik_Tcp_SocketQueue* this);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (Laik_Tcp_SocketQueue, laik_tcp_socket_queue_free)

__attribute__ ((warn_unused_result))
size_t laik_tcp_socket_queue_get_size (Laik_Tcp_SocketQueue* this);

__attribute__ ((warn_unused_result))
Laik_Tcp_Socket* laik_tcp_socket_queue_get_socket (Laik_Tcp_SocketQueue* this, size_t index);

__attribute__ ((warn_unused_result))
Laik_Tcp_SocketQueue* laik_tcp_socket_queue_new (void);

__attribute__ ((warn_unused_result))
Laik_Tcp_Socket* laik_tcp_socket_queue_pop (Laik_Tcp_SocketQueue* this);

void laik_tcp_socket_queue_push (Laik_Tcp_SocketQueue* this, Laik_Tcp_Socket* socket, short events);

void laik_tcp_socket_queue_remove (Laik_Tcp_SocketQueue* this, size_t index);
