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

#include <glib.h>     // for GCond, G_DEFINE_AUTOPTR_CLEANUP_FUNC
#include <stdbool.h>  // for bool
#include "lock.h"     // for Laik_Tcp_Lock

typedef GCond Laik_Tcp_Condition;

void laik_tcp_condition_broadcast (Laik_Tcp_Condition* this);

void laik_tcp_condition_free (Laik_Tcp_Condition* this);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (Laik_Tcp_Condition, laik_tcp_condition_free)

Laik_Tcp_Condition* laik_tcp_condition_new (void);

void laik_tcp_condition_wait_forever (Laik_Tcp_Condition* this, Laik_Tcp_Lock* lock);

__attribute__ ((warn_unused_result))
bool laik_tcp_condition_wait_seconds (Laik_Tcp_Condition* this, Laik_Tcp_Lock* lock, double seconds);
