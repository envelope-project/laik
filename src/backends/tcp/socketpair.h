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
