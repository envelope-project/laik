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

#pragma once

#include <glib.h>       // for GBytes, GPtrArray, G_DEFINE_AUTOPTR_CLEANUP_FUNC
#include <stdbool.h>    // for bool
#include <stddef.h>     // for size_t
#include <stdint.h>     // for uint64_t
#include <sys/types.h>  // for ssize_t
#include "errors.h"     // for Laik_Tcp_Errors

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
bool laik_tcp_socket_get_closed (Laik_Tcp_Socket* this);

__attribute__ ((warn_unused_result))
struct pollfd laik_tcp_socket_get_pollfd (Laik_Tcp_Socket* this, short events);

__attribute__ ((warn_unused_result))
Laik_Tcp_Socket* laik_tcp_socket_new (Laik_Tcp_SocketType type, size_t rank, Laik_Tcp_Errors* errors);

__attribute__ ((warn_unused_result))
Laik_Tcp_Socket* laik_tcp_socket_new_from_fd (int fd);

__attribute__ ((warn_unused_result))
Laik_Tcp_Socket* laik_tcp_socket_poll (GPtrArray* sockets, short events, double seconds);

GBytes* laik_tcp_socket_receive_bytes (Laik_Tcp_Socket* this);

__attribute__ ((warn_unused_result))
bool laik_tcp_socket_receive_data (Laik_Tcp_Socket* this, void* data, size_t size);

__attribute__ ((warn_unused_result))
bool laik_tcp_socket_receive_uint64 (Laik_Tcp_Socket* this, uint64_t* value);

__attribute__ ((warn_unused_result))
bool laik_tcp_socket_send_bytes (Laik_Tcp_Socket* this, GBytes* bytes);

__attribute__ ((warn_unused_result))
bool laik_tcp_socket_send_data (Laik_Tcp_Socket* this, const void* data, size_t size);

__attribute__ ((warn_unused_result))
bool laik_tcp_socket_send_uint64 (Laik_Tcp_Socket* this, uint64_t value);

__attribute__ ((warn_unused_result))
ssize_t laik_tcp_socket_try_receive (Laik_Tcp_Socket* this, void* data, size_t size);

__attribute__ ((warn_unused_result))
ssize_t laik_tcp_socket_try_send (Laik_Tcp_Socket* this, const void* data, size_t size);

__attribute__ ((warn_unused_result))
bool laik_tcp_socket_wait (Laik_Tcp_Socket* this, short events, double seconds);
