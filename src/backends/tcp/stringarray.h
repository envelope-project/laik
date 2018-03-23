#pragma once

#include <glib.h>  // for g_strfreev, G_DEFINE_AUTOPTR_CLEANUP_FUNC

typedef char* Laik_Tcp_StringArray;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Laik_Tcp_StringArray, g_strfreev)
