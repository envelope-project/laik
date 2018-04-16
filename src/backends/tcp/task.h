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

#include <glib.h>    // for GBytes, G_DEFINE_AUTOPTR_CLEANUP_FUNC
#include <stddef.h>  // for size_t

typedef struct {
    int     type;
    size_t  peer;
    GBytes* header;
} Laik_Tcp_Task;

void laik_tcp_task_destroy (void* this);

void laik_tcp_task_free (Laik_Tcp_Task* this);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (Laik_Tcp_Task, laik_tcp_task_free)

__attribute__  ((warn_unused_result))
Laik_Tcp_Task* laik_tcp_task_new (int type, size_t peer, GBytes* header);
