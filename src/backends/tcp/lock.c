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
