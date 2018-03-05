#include "config.h"
#include <gio/gio.h>  // for g_file_load_contents, g_file_new_for_commandlin...
#include <glib.h>     // for g_ptr_array_add, g_strdup_printf, g_autoptr
#include <stdbool.h>  // for false, bool, true
#include <stdint.h>   // for int64_t
#include <stdlib.h>   // for getenv, NULL, atol, size_t
#include <unistd.h>   // for getppid, getpid
#include "debug.h"    // for laik_tcp_always, laik_tcp_debug
#include "errors.h"   // for laik_tcp_errors_present, laik_tcp_errors_new

static GMutex           mutex;
static Laik_Tcp_Config* config    = NULL;
static int64_t          timestamp = 0;
static GThread*         thread    = NULL;
static bool             running   = false;

__attribute__ ((warn_unused_result))
static GKeyFile* laik_tcp_config_keyfile (Laik_Tcp_Errors* errors) {
    laik_tcp_always (errors);

    GError* error = NULL;

    // If the environment variable is set, load the configuration file from it
    const char* location = getenv ("LAIK_TCP_CONFIG");
    if (!location) {
        return NULL;
    }

    // Load the configuration file from a path or URL via GIO auto-magic
    g_autoptr (GFile) file = g_file_new_for_commandline_arg (location);
    g_autofree char* data = NULL;
    size_t size = 0;
    g_file_load_contents (file, NULL, &data, &size, NULL, &error);
    if (error) {
        laik_tcp_errors_push_direct (errors, error);
        return NULL;
    }

    // Parse the configuration file as a GKeyFile
    g_autoptr (GKeyFile) keyfile = g_key_file_new ();
    g_key_file_load_from_data (keyfile, data, size, G_KEY_FILE_NONE, &error);
    if (error) {
        laik_tcp_errors_push_direct (errors, error);
        return NULL;
    }

    // Return the keyfile
    return g_steal_pointer (&keyfile);
}

__attribute__ ((warn_unused_result))
static Laik_Tcp_Config* laik_tcp_config_new (Laik_Tcp_Errors* errors) {
    laik_tcp_always (errors);

    // Try to load the configuration file
    g_autoptr (GKeyFile) keyfile = laik_tcp_config_keyfile (errors);
    if (laik_tcp_errors_present (errors)) {
        laik_tcp_errors_push (errors, __func__, 0, "Failed to parse the configuration file");
        return NULL;
    }

    // Try to read the address mapping from the configuration file
    g_autoptr (GPtrArray) addresses = g_ptr_array_new_with_free_func (g_free);
    if (keyfile && g_key_file_has_key (keyfile, "addresses", "0", NULL)) {
        for (size_t task = 0; ; task++) {
            g_autofree char* key = g_strdup_printf ("%zu", task);
            char* value = g_key_file_get_string (keyfile, "addresses", key, NULL);
            if (value) {
                g_ptr_array_add (addresses, value);
            } else {
                break;
            }
        }
    } else if (getenv ("OMPI_COMM_WORLD_SIZE") && atol (getenv ("OMPI_COMM_WORLD_SIZE")) > 0) {
        const size_t size = atol (getenv ("OMPI_COMM_WORLD_SIZE"));
        for (size_t task = 0; task < size; task++) {
           g_ptr_array_add (addresses, g_strdup_printf ("laik-tcp-auto-openmpi-%zu-%zu", (size_t) getppid (), task));
        }
    } else if (getenv ("PMI_SIZE") && atol (getenv ("PMI_SIZE")) > 0) {
        const size_t size = atol (getenv ("PMI_SIZE"));
        for (size_t task = 0; task < size; task++) {
           g_ptr_array_add (addresses, g_strdup_printf ("laik-tcp-auto-mpich-%zu-%zu", (size_t) getppid (), task));
        }
    } else {
        g_ptr_array_add (addresses, g_strdup_printf ("laik-tcp-auto-single-%zu", (size_t) getpid ()));
    }

    // Create the object
    Laik_Tcp_Config* this = g_new0 (Laik_Tcp_Config, 1);

    // Initialize the object
    *this = (Laik_Tcp_Config) {
        .addresses  = g_steal_pointer (&addresses),
        .references = 1,
    };

    // Return the object
    return this;
}

static void* laik_tcp_config_update (void* data) {
    // Try to construct a new configuration object
    g_autoptr (Laik_Tcp_Errors) errors = laik_tcp_errors_new ();
    g_autoptr (Laik_Tcp_Config) update = laik_tcp_config_new (errors);

    // Take the lock
    __attribute__ ((unused))
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&mutex);

    // If we got a new configuration object, replace the old one
    if (!laik_tcp_errors_present (errors)) {
        laik_tcp_config_unref (config);
        config = g_steal_pointer (&update);
    }

    // We are done and ready to be reaped
    running = false;

    // Avoid a warning about the "data" variable being unused
    return data;
}

Laik_Tcp_Config* laik_tcp_config () {
    // Take the lock
    __attribute__ ((unused))
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&mutex);

    // If we don't have a configuration yet, try to get it a number of times
    for (size_t try = 0; config == NULL; try++) {
        g_autoptr (Laik_Tcp_Errors) errors = laik_tcp_errors_new ();

        config = laik_tcp_config_new (errors);

        if (laik_tcp_errors_present (errors)) {
            if (try < 10) {
                g_usleep (100 * G_TIME_SPAN_MILLISECOND);
            } else {
                laik_tcp_errors_push (errors, __func__, 0, "Failed to construct the initial configuration object for the 10th time");
                laik_tcp_errors_abort (errors);
            }
        }
    }

    // If a thread was started and has finished in the mean time, reap it here
    if (thread && !running) {
        laik_tcp_debug ("Update thread completed, reaping its result value");
        (void) g_thread_join (thread);
        thread = NULL;
    }

    // If no configuration update is running but its overdue, start it
    if (!running && g_get_monotonic_time () - timestamp > G_TIME_SPAN_SECOND) {
        laik_tcp_debug ("Configuration outdated, starting update");
        running   = true;
        timestamp = g_get_monotonic_time ();
        thread    = g_thread_new ("Update Thread", laik_tcp_config_update, NULL);
    }

    // Return the current configuration
    return laik_tcp_config_ref (config);
}

Laik_Tcp_Config* laik_tcp_config_ref (Laik_Tcp_Config* this) {
    laik_tcp_always (this);

    g_atomic_int_inc (&this->references);

    return this;
}

void laik_tcp_config_unref (Laik_Tcp_Config* this) {
    if (!this) {
        return;
    }

    if (!g_atomic_int_dec_and_test (&this->references)) {
        return;
    }

    g_ptr_array_unref (this->addresses);

    g_free (this);
}
