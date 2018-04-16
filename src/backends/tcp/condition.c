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

#include "condition.h"
#include <glib.h>    // for g_cond_broadcast, g_cond_clear, g_cond_init, g_c...
#include <stdint.h>  // for int64_t
#include "debug.h"   // for laik_tcp_always
#include "lock.h"    // for Laik_Tcp_Lock

void laik_tcp_condition_broadcast (Laik_Tcp_Condition* this) {
    laik_tcp_always (this);

    g_cond_broadcast (this);
}

void laik_tcp_condition_free (Laik_Tcp_Condition* this) {
    if (!this) {
        return;
    }

    g_cond_clear (this);

    g_free (this);
}

Laik_Tcp_Condition* laik_tcp_condition_new (void) {
    Laik_Tcp_Condition* this = g_new (Laik_Tcp_Condition, 1);

    g_cond_init (this);

    return this;
}

void laik_tcp_condition_wait_forever (Laik_Tcp_Condition* this, Laik_Tcp_Lock* lock) {
    laik_tcp_always (this);
    laik_tcp_always (lock);

    g_cond_wait (this, lock);
}

bool laik_tcp_condition_wait_seconds (Laik_Tcp_Condition* this, Laik_Tcp_Lock* lock, double seconds) {
    laik_tcp_always (this);
    laik_tcp_always (lock);

    const int64_t microseconds = seconds * G_TIME_SPAN_SECOND;

    return g_cond_wait_until (this, lock, g_get_monotonic_time () + microseconds);
}
