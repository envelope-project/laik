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

#include "errors.h"  // for Laik_Tcp_Errors

typedef void* (Laik_Tcp_Async_Function) (void* input, Laik_Tcp_Errors* errors);

typedef struct Laik_Tcp_Async Laik_Tcp_Async;

Laik_Tcp_Async* laik_tcp_async_new (Laik_Tcp_Async_Function* function, void* input);

__attribute__ ((warn_unused_result))
void* laik_tcp_async_wait (Laik_Tcp_Async* this, Laik_Tcp_Errors* errors);
