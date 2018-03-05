#include "task.h"
#include <glib.h>   // for g_bytes_ref, g_bytes_unref, g_free, g_malloc0_n
#include "debug.h"  // for laik_tcp_always

void laik_tcp_task_destroy (void* this) {
    laik_tcp_task_free (this);
}

void laik_tcp_task_free (Laik_Tcp_Task* this) {
    if (!this) {
        return;
    }

    g_bytes_unref (this->header);

    g_free (this);
}

Laik_Tcp_Task* laik_tcp_task_new (int type, size_t peer, GBytes* header) {
    laik_tcp_always (header);

    Laik_Tcp_Task* this = g_new0 (Laik_Tcp_Task, 1);

    *this = (Laik_Tcp_Task) {
        .type   = type,
        .peer   = peer,
        .header = g_bytes_ref (header),
    };

    return this;
}
