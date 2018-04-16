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

#include <glib.h>   // for G_DEFINE_AUTOPTR_CLEANUP_FUNC
#include <netdb.h>  // for freeaddrinfo, addrinfo

typedef struct addrinfo Laik_Tcp_AddressInfo;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Laik_Tcp_AddressInfo, freeaddrinfo)
