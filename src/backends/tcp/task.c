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

#include "task.h"
#include <glib.h>   // for g_bytes_ref, g_bytes_unref, g_free, g_malloc0_n
#include "debug.h"  // for laik_tcp_always

void laik_tcp_task_destroy (void* this) {
    laik_tcp_task_free (this);
}

void laik_tcp_task_free (Laik_Tcp_Task* this) {
    if (!this) {
        return;
    }

    g_bytes_unref (this->header);

    g_free (this);
}

Laik_Tcp_Task* laik_tcp_task_new (int type, size_t peer, GBytes* header) {
    laik_tcp_always (header);

    Laik_Tcp_Task* this = g_new0 (Laik_Tcp_Task, 1);

    *this = (Laik_Tcp_Task) {
        .type   = type,
        .peer   = peer,
        .header = g_bytes_ref (header),
    };

    return this;
}
