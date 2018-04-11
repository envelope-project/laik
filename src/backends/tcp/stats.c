#include "stats.h"
#include <glib.h>    // for g_strdup_vprintf, g_hash_table_lookup, GMutexLoc...
#include <stdarg.h>  // for va_end, va_list, va_start
#include <string.h>  // for NULL, strcmp
#include "debug.h"   // for laik_tcp_always
#include "lock.h"    // for LAIK_TCP_LOCK, Laik_Tcp_Lock

static GHashTable*   hash = NULL;
static Laik_Tcp_Lock lock;

static int laik_tcp_stats_compare (const void* a, const void* b) {
    laik_tcp_always (a);
    laik_tcp_always (b);

    return strcmp (a, b);
}

void laik_tcp_stats_change_real (double change, const char* format, ...) {
    laik_tcp_always (format);

    LAIK_TCP_LOCK (&lock);

    // If necessary, create the hash
    if (!hash) {
        hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    }

    // Calculate the key
    va_list arguments;
    va_start (arguments, format);
    g_autofree char* key = g_strdup_vprintf (format, arguments);
    va_end (arguments);

    // Look up the current value...
    double* current = g_hash_table_lookup (hash, key);

    // ... or add it if it's missing
    if (!current) {
        current = g_new0 (double, 1);
        g_hash_table_insert (hash, g_steal_pointer (&key), current);
    }

    // Make the requested change
    *current += change;
}

void laik_tcp_stats_remove_real (const char* format, ...) {
    laik_tcp_always (format);

    LAIK_TCP_LOCK (&lock);

    // Calculate the key
    va_list arguments;
    va_start (arguments, format);
    g_autofree char* key = g_strdup_vprintf (format, arguments);
    va_end (arguments);

    // If we have a hash table, remove the requested key
    if (hash) {
        g_hash_table_remove (hash, key);
    }
}

void laik_tcp_stats_reset_real (void) {
    LAIK_TCP_LOCK (&lock);

    // If we have a hash table, free it
    if (hash) {
        g_hash_table_unref (hash);
    }
}

void laik_tcp_stats_store_real (const char* format, ...) {
    laik_tcp_always (format);

    LAIK_TCP_LOCK (&lock);

    // Get the list of all keys
    g_autoptr (GList) keys = hash ? g_hash_table_get_keys (hash) : NULL;

    // Sort the keys
    keys = g_list_sort (keys, laik_tcp_stats_compare);

    // Create a new string
    g_autoptr (GString) content = g_string_new ("");

    // Append all the data gathered
    for (GList* current = keys; current; current = current->next) {
        const char*   key   = current->data;
        const double* value = g_hash_table_lookup (hash, key);
        g_string_append_printf (content, "%-64s %15lf\n", key, *value);
    }

    // Calculate the path
    va_list arguments;
    va_start (arguments, format);
    g_autofree const char* path = g_strdup_vprintf (format, arguments);
    va_end (arguments);

    // Try to store the data
    g_file_set_contents (path, content->str, content->len, NULL);
}
