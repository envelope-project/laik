#pragma once

#include <glib.h>     // for GBytes, GPtrArray, G_DEFINE_AUTOPTR_CLEANUP_FUNC
#include <stdbool.h>  // for bool
#include <stdint.h>   // for int64_t, uint64_t
#include "errors.h"   // for Laik_Tcp_Errors

typedef struct Laik_Tcp_Socket Laik_Tcp_Socket;

typedef enum {
    LAIK_TCP_SOCKET_TYPE_CLIENT = 0,
    LAIK_TCP_SOCKET_TYPE_SERVER = 1,
} Laik_Tcp_SocketType;

__attribute__ ((warn_unused_result))
Laik_Tcp_Socket* laik_tcp_socket_accept (Laik_Tcp_Socket* this);

void laik_tcp_socket_destroy (void* this);

void laik_tcp_socket_free (Laik_Tcp_Socket* this);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (Laik_Tcp_Socket, laik_tcp_socket_free)

__attribute__ ((warn_unused_result))
bool laik_tcp_socket_get_listening (const Laik_Tcp_Socket* this);

__attribute__ ((warn_unused_result))
int64_t laik_tcp_socket_get_timestamp (const Laik_Tcp_Socket* this);

__attribute__ ((warn_unused_result))
Laik_Tcp_Socket* laik_tcp_socket_new (Laik_Tcp_SocketType type, const char* address, Laik_Tcp_Errors* errors);

__attribute__ ((warn_unused_result))
Laik_Tcp_Socket* laik_tcp_socket_poll (GPtrArray* sockets, short events, int64_t microseconds);

__attribute__ ((warn_unused_result))
GBytes* laik_tcp_socket_receive_bytes (Laik_Tcp_Socket* this);

__attribute__ ((warn_unused_result))
bool laik_tcp_socket_receive_uint64 (Laik_Tcp_Socket* this, uint64_t* value);

__attribute__ ((warn_unused_result))
bool laik_tcp_socket_send_bytes (Laik_Tcp_Socket* this, GBytes* bytes);

__attribute__ ((warn_unused_result))
bool laik_tcp_socket_send_uint64 (Laik_Tcp_Socket* this, uint64_t value);

__attribute__ ((warn_unused_result))
Laik_Tcp_Socket* laik_tcp_socket_touch (Laik_Tcp_Socket* this);
