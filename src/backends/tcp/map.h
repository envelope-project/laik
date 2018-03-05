#pragma once

#include <glib.h>     // for GBytes, G_DEFINE_AUTOPTR_CLEANUP_FUNC
#include <stdbool.h>  // for bool
#include <stddef.h>   // for size_t
#include <stdint.h>   // for int64_t


typedef struct Laik_Tcp_Map Laik_Tcp_Map;

void laik_tcp_map_add (Laik_Tcp_Map* this, GBytes* key, GBytes* value);

void laik_tcp_map_block (Laik_Tcp_Map* this);

void laik_tcp_map_free ();
G_DEFINE_AUTOPTR_CLEANUP_FUNC (Laik_Tcp_Map, laik_tcp_map_free)

void laik_tcp_map_discard (Laik_Tcp_Map* this, GBytes* key);

__attribute__ ((warn_unused_result))
GBytes* laik_tcp_map_get (Laik_Tcp_Map* this, const GBytes* key, int64_t microseconds);

__attribute__ ((warn_unused_result))
Laik_Tcp_Map* laik_tcp_map_new (size_t limit);

bool laik_tcp_map_try (Laik_Tcp_Map* this, GBytes* key, GBytes* value);
