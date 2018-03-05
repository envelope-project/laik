#pragma once

#include <glib.h>  // for GPtrArray, G_DEFINE_AUTOPTR_CLEANUP_FUNC

typedef struct {
    GPtrArray* addresses;

    int references;
} Laik_Tcp_Config;

Laik_Tcp_Config* laik_tcp_config ();

__attribute__ ((warn_unused_result))
Laik_Tcp_Config* laik_tcp_config_ref (Laik_Tcp_Config* this);

void laik_tcp_config_unref (Laik_Tcp_Config* this);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (Laik_Tcp_Config, laik_tcp_config_unref)
