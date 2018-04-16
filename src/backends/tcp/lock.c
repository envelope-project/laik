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

#include "lock.h"
#include <glib.h>  // for g_free, g_malloc0_n, g_mutex_clear, g_mutex_init

void laik_tcp_lock_free (Laik_Tcp_Lock* this) {
    if (!this) {
        return;
    }

    g_mutex_clear (this);

    g_free (this);
}

Laik_Tcp_Lock* laik_tcp_lock_new (void) {
    Laik_Tcp_Lock* this = g_new (Laik_Tcp_Lock, 1);

    g_mutex_init (this);

    return this;
}
