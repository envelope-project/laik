#pragma once

#include <glib.h>    // for GBytes, G_DEFINE_AUTOPTR_CLEANUP_FUNC
#include <stddef.h>  // for size_t

typedef struct {
    int     type;
    size_t  peer;
    GBytes* header;
} Laik_Tcp_Task;

void laik_tcp_task_destroy (void* this);

void laik_tcp_task_free (Laik_Tcp_Task* this);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (Laik_Tcp_Task, laik_tcp_task_free)

__attribute__  ((warn_unused_result))
Laik_Tcp_Task* laik_tcp_task_new (int type, size_t peer, GBytes* header);
