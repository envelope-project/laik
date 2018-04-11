#pragma once

#include <glib.h>  // for GMutex, G_DEFINE_AUTOPTR_CLEANUP_FUNC, g_autoptr

typedef GMutex Laik_Tcp_Lock;

#define LAIK_TCP_LOCK(lock) __attribute__((unused)) g_autoptr (GMutexLocker) laik_tcp_lock_dummy = g_mutex_locker_new (lock)

void laik_tcp_lock_free (Laik_Tcp_Lock* this);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (Laik_Tcp_Lock, laik_tcp_lock_free)

Laik_Tcp_Lock* laik_tcp_lock_new (void);
