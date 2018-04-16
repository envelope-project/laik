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

#include "time.h"
#include <glib.h>  // for g_get_monotonic_time, g_usleep, G_TIME_SPAN_SECOND

void laik_tcp_sleep (double seconds) {
    g_usleep (seconds * G_TIME_SPAN_SECOND);
}

double laik_tcp_time (void) {
    return (double) g_get_monotonic_time () / (double) G_TIME_SPAN_SECOND;
}
