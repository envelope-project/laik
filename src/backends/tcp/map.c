#include "map.h"
#include <glib.h>     // for g_bytes_ref, g_bytes_hash, g_mutex_locker_new
#include <stdbool.h>  // for bool, true, false
#include <stddef.h>   // for NULL, size_t
#include <stdint.h>   // for int64_t, SIZE_MAX, intmax_t
#include "debug.h"    // for laik_tcp_debug
#include "errors.h"   // for laik_tcp_always

struct Laik_Tcp_Map {
    GMutex* mutex;
    GCond*  changed;

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

    __attribute__ ((unused))
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (this->mutex);

    laik_tcp_debug ("Adding key 0x%08X", g_bytes_hash (key));

    if (g_hash_table_contains (this->hash, key)) {
        laik_tcp_debug ("Key already exists, aborting");
        return;
    }

    this->size = laik_tcp_map_plus (this->size, g_bytes_get_size (value));

    g_hash_table_insert (this->hash, g_bytes_ref (key), g_bytes_ref (value));

    g_cond_broadcast (this->changed);
}

void laik_tcp_map_block (Laik_Tcp_Map* this) {
    laik_tcp_always (this);

    __attribute__ ((unused))
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (this->mutex);

    laik_tcp_debug ("Waiting for mapping to be within its limits, currently %zu/%zu bytes occupied", this->size, this->limit);

    while (this->size > this->limit) {
        g_cond_wait (this->changed, this->mutex);
    }

    laik_tcp_debug ("Mapping is now within its limits, now %zu/%zu bytes occupied", this->size, this->limit);
}

void laik_tcp_map_discard (Laik_Tcp_Map* this, GBytes* key) {
    laik_tcp_always (this);
    laik_tcp_always (key);

    __attribute__ ((unused))
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (this->mutex);

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

    g_cond_broadcast (this->changed);
}

void laik_tcp_map_free (Laik_Tcp_Map* this) {
    if (!this) {
        return;
    }

    // Free the lock
    g_mutex_clear (this->mutex);
    g_free (this->mutex);

    // Free the condition variable
    g_cond_clear (this->changed);
    g_free (this->changed);

    // Free the hash tables
    g_hash_table_unref (this->hash);

    // Free ourself
    g_free (this);
}

GBytes* laik_tcp_map_get (Laik_Tcp_Map* this, const GBytes* key, const int64_t microseconds) {
    laik_tcp_always (this);
    laik_tcp_always (key);

    __attribute__ ((unused))
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (this->mutex);

    laik_tcp_debug ("Looking up key 0x%08X with a time limit of %jd microseconds", g_bytes_hash (key), (intmax_t) microseconds);

    const int64_t timeout = g_get_monotonic_time () + microseconds;

    GBytes* value = NULL;

    while (!g_hash_table_lookup_extended (this->hash, key, NULL, (void**) &value)) {
        if (!g_cond_wait_until (this->changed, this->mutex, timeout)) {
            return NULL;
        }
    }

    return value ? g_bytes_ref (value) : NULL;
}

Laik_Tcp_Map* laik_tcp_map_new (size_t limit) {
    // Create a mutex which all object methods need to lock before beginning
    g_autofree GMutex* mutex = g_new0 (GMutex, 1);
    g_mutex_init (mutex);

    // Create a condition variable which means "the mapping has changed somehow"
    g_autofree GCond* changed = g_new0 (GCond, 1);
    g_cond_init (changed);

    Laik_Tcp_Map* this = g_new0 (Laik_Tcp_Map, 1);

    *this = (Laik_Tcp_Map) {
        .mutex   = g_steal_pointer (&mutex),
        .changed = g_steal_pointer (&changed),

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

    __attribute__ ((unused))
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (this->mutex);

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

    g_cond_broadcast (this->changed);

    return true;
}
