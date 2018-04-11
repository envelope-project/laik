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
