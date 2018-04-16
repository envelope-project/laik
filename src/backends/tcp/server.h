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

#include <glib.h>     // for G_DEFINE_AUTOPTR_CLEANUP_FUNC
#include <stdbool.h>  // for bool
#include "socket.h"   // for Laik_Tcp_Socket

typedef bool (Laik_Tcp_ServerFunction) (Laik_Tcp_Socket* socket, void* userdata);

typedef struct Laik_Tcp_Server Laik_Tcp_Server;

void laik_tcp_server_free (Laik_Tcp_Server* this);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (Laik_Tcp_Server, laik_tcp_server_free)

__attribute__  ((warn_unused_result))
Laik_Tcp_Server* laik_tcp_server_new (Laik_Tcp_Socket* socket, Laik_Tcp_ServerFunction* function, void* userdata);
