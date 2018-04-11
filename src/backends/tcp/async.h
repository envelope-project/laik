#pragma once

#include "errors.h"  // for Laik_Tcp_Errors

typedef void* (Laik_Tcp_Async_Function) (void* input, Laik_Tcp_Errors* errors);

typedef struct Laik_Tcp_Async Laik_Tcp_Async;

Laik_Tcp_Async* laik_tcp_async_new (Laik_Tcp_Async_Function* function, void* input);

__attribute__ ((warn_unused_result))
void* laik_tcp_async_wait (Laik_Tcp_Async* this, Laik_Tcp_Errors* errors);
