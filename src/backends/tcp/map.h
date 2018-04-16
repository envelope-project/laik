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

#include <glib.h>     // for GBytes, G_DEFINE_AUTOPTR_CLEANUP_FUNC
#include <stdbool.h>  // for bool
#include <stddef.h>   // for size_t

typedef struct Laik_Tcp_Map Laik_Tcp_Map;

void laik_tcp_map_add (Laik_Tcp_Map* this, GBytes* key, GBytes* value);

void laik_tcp_map_block (Laik_Tcp_Map* this);

void laik_tcp_map_free ();
G_DEFINE_AUTOPTR_CLEANUP_FUNC (Laik_Tcp_Map, laik_tcp_map_free)

void laik_tcp_map_discard (Laik_Tcp_Map* this, GBytes* key);

__attribute__ ((warn_unused_result))
GBytes* laik_tcp_map_get (Laik_Tcp_Map* this, const GBytes* key, double seconds);

__attribute__ ((warn_unused_result))
Laik_Tcp_Map* laik_tcp_map_new (size_t limit);

bool laik_tcp_map_try (Laik_Tcp_Map* this, GBytes* key, GBytes* value);
