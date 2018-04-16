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

#include <glib.h>  // for GMutex, G_DEFINE_AUTOPTR_CLEANUP_FUNC, g_autoptr

typedef GMutex Laik_Tcp_Lock;

#define LAIK_TCP_LOCK(lock) __attribute__((unused)) g_autoptr (GMutexLocker) laik_tcp_lock_dummy = g_mutex_locker_new (lock)

void laik_tcp_lock_free (Laik_Tcp_Lock* this);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (Laik_Tcp_Lock, laik_tcp_lock_free)

Laik_Tcp_Lock* laik_tcp_lock_new (void);
