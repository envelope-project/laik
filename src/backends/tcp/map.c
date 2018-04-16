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

#include "map.h"
#include <glib.h>       // for g_bytes_ref, g_bytes_get_size, GBytes, g_hash...
#include <stdbool.h>    // for true, bool, false
#include <stddef.h>     // for size_t, NULL
#include <stdint.h>     // for SIZE_MAX
#include "condition.h"  // for laik_tcp_condition_broadcast, laik_tcp_condit...
#include "debug.h"      // for laik_tcp_always, laik_tcp_debug
#include "lock.h"       // for laik_tcp_lock_new, LAIK_TCP_LOCK, laik_tcp_lo...

struct Laik_Tcp_Map {
    Laik_Tcp_Condition* condition;
    Laik_Tcp_Lock*      lock;

    GHashTable* hash;

    size_t size;
    size_t limit;
};

static void laik_tcp_map_destroy (void* bytes) {
    g_bytes_unref (bytes);
}

static size_t laik_tcp_map_minus (size_t x, size_t y) {
    return x >= y ? x - y : 0;
}

static size_t laik_tcp_map_plus (size_t x, size_t y) {
    return SIZE_MAX - x >= y ? x + y : SIZE_MAX;
}

void laik_tcp_map_add (Laik_Tcp_Map* this, GBytes* key, GBytes* value) {
    laik_tcp_always (this);
    laik_tcp_always (key);
    laik_tcp_always (value);

    LAIK_TCP_LOCK (this->lock);

    laik_tcp_debug ("Adding key 0x%08X", g_bytes_hash (key));

    if (g_hash_table_contains (this->hash, key)) {
        laik_tcp_debug ("Key already exists, aborting");
        return;
    }

    this->size = laik_tcp_map_plus (this->size, g_bytes_get_size (value));

    g_hash_table_insert (this->hash, g_bytes_ref (key), g_bytes_ref (value));

    laik_tcp_condition_broadcast (this->condition);
}

void laik_tcp_map_block (Laik_Tcp_Map* this) {
    laik_tcp_always (this);

    LAIK_TCP_LOCK (this->lock);

    laik_tcp_debug ("Waiting for mapping to be within its limits, currently %zu/%zu bytes occupied", this->size, this->limit);

    while (this->size > this->limit) {
        laik_tcp_condition_wait_forever (this->condition, this->lock);
    }

    laik_tcp_debug ("Mapping is now within its limits, now %zu/%zu bytes occupied", this->size, this->limit);
}

void laik_tcp_map_discard (Laik_Tcp_Map* this, GBytes* key) {
    laik_tcp_always (this);
    laik_tcp_always (key);

    LAIK_TCP_LOCK (this->lock);

    laik_tcp_debug ("Discarding key 0x%08X", g_bytes_hash (key));

    if (this->limit == SIZE_MAX) {
        laik_tcp_debug ("Mapping may grow to infinite size, aborting");
        return;
    }

    GBytes* value = g_hash_table_lookup (this->hash, key);

    if (!value) {
        laik_tcp_debug ("Key is missing or already discarded, aborting");
        return;
    }

    this->size = laik_tcp_map_minus (this->size, g_bytes_get_size (value));

    g_hash_table_insert (this->hash, g_bytes_ref (key), NULL);

    laik_tcp_condition_broadcast (this->condition);
}

void laik_tcp_map_free (Laik_Tcp_Map* this) {
    if (!this) {
        return;
    }

    // Free the condition variable
    laik_tcp_condition_free (this->condition);

    // Free the lock
    laik_tcp_lock_free (this->lock);

    // Free the hash tables
    g_hash_table_unref (this->hash);

    // Free ourself
    g_free (this);
}

GBytes* laik_tcp_map_get (Laik_Tcp_Map* this, const GBytes* key, const double seconds) {
    laik_tcp_always (this);
    laik_tcp_always (key);

    LAIK_TCP_LOCK (this->lock);

    laik_tcp_debug ("Looking up key 0x%08X with a time limit of %lf seconds", g_bytes_hash (key), seconds);

    GBytes* value = NULL;

    while (!g_hash_table_lookup_extended (this->hash, key, NULL, (void**) &value)) {
        if (!laik_tcp_condition_wait_seconds (this->condition, this->lock, seconds)) {
            return NULL;
        }
    }

    return value ? g_bytes_ref (value) : NULL;
}

Laik_Tcp_Map* laik_tcp_map_new (size_t limit) {
    Laik_Tcp_Map* this = g_new0 (Laik_Tcp_Map, 1);

    *this = (Laik_Tcp_Map) {
        // Create a condition variable which means "the mapping has changed somehow"
        .condition = laik_tcp_condition_new (),
        
        // Create a mutex which all object methods need to lock before beginning
        .lock = laik_tcp_lock_new (),

        // Create the hash table for the mapping
        .hash = g_hash_table_new_full (g_bytes_hash, g_bytes_equal, laik_tcp_map_destroy, laik_tcp_map_destroy),

        // Set up the initial and maximum value for the total size of all mappings
        .size  = 0,
        .limit = limit,
    };

    return this;
}

bool laik_tcp_map_try (Laik_Tcp_Map* this, GBytes* key, GBytes* value) {
    laik_tcp_always (this);
    laik_tcp_always (key);
    laik_tcp_always (value);

    LAIK_TCP_LOCK (this->lock);

    laik_tcp_debug ("Trying to add key 0x%08X", g_bytes_hash (key));

    if (g_hash_table_contains (this->hash, key)) {
        laik_tcp_debug ("Key already exists, aborting");
        return true;
    }

    if (laik_tcp_map_plus (this->size, g_bytes_get_size (value)) > this->limit) {
        laik_tcp_debug ("Value would exceed limit, aborting");
        return false;
    }

    this->size = laik_tcp_map_plus (this->size, g_bytes_get_size (value));

    g_hash_table_insert (this->hash, g_bytes_ref (key), g_bytes_ref (value));

    laik_tcp_condition_broadcast (this->condition);

    return true;
}
