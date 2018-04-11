#include "config.h"
#include <gio/gio.h>      // for g_file_load_contents, g_file_new_for_comman...
#include <glib.h>         // for g_autoptr, g_ptr_array_add, g_strdup_printf
#include <stdbool.h>      // for false, bool, true
#include <stdint.h>       // for SIZE_MAX, int64_t
#include <stdlib.h>       // for getenv, atol
#include <unistd.h>       // for getppid, getpid
#include "debug.h"        // for laik_tcp_always, laik_tcp_debug
#include "errors.h"       // for laik_tcp_errors_push, laik_tcp_errors_new
#include "lock.h"         // for LAIK_TCP_LOCK, Laik_Tcp_Lock
#include "stringarray.h"  // for Laik_Tcp_StringArray, Laik_Tcp_StringArray_...
#include "time.h"         // for laik_tcp_time, laik_tcp_sleep

static bool             running   = false;
static double           timestamp = 0;
static GThread*         thread    = NULL;
static Laik_Tcp_Config* config    = NULL;
static Laik_Tcp_Lock    lock;

__attribute__ ((warn_unused_result))
static bool laik_tcp_config_parse_addresses (GKeyFile* keyfile, const char* group, GPtrArray** result, Laik_Tcp_Errors* errors) {
    laik_tcp_always (keyfile);
    laik_tcp_always (group);
    laik_tcp_always (result);
    laik_tcp_always (errors);

    // If the keyfile doesn't have the specified group, do nothing
    g_autoptr (Laik_Tcp_StringArray) keys = g_key_file_get_keys (keyfile, group, NULL, NULL);
    if (keys && *keys) {
        // Initialiaze a GError variable for the various GKeyfile function calls
        g_autoptr (GError) error = NULL;

        // Get the value from the configuration file
        g_autoptr (GPtrArray) addresses = g_ptr_array_new_with_free_func (g_free);
        for (size_t i = 0; keys[i]; i++) {
            char* address = g_key_file_get_string (keyfile, group, keys[i], &error);
            if (error) {
                laik_tcp_errors_push (errors, __func__, 1, "Failed to parse the configuration file: (%s, %s) is not a valid address", group, keys[i]);
                return false;
            }
            g_ptr_array_add (addresses, address);
        }

        // Set the result variable
        if (*result) {
            g_ptr_array_unref (*result);
        }
        *result = g_steal_pointer (&addresses);
    }

    return true;
}

__attribute__ ((unused))
__attribute__ ((warn_unused_result))
static bool laik_tcp_config_parse_bool (GKeyFile* keyfile, const char* group, const char* key, bool* result, Laik_Tcp_Errors* errors) {
    laik_tcp_always (keyfile);
    laik_tcp_always (group);
    laik_tcp_always (key);
    laik_tcp_always (result);
    laik_tcp_always (errors);

    // If the keyfile doesn't have the specified key, do nothing
    if (g_key_file_has_key (keyfile, group, key, NULL)) {
        // Initialiaze a GError variable for the various GKeyfile function calls
        g_autoptr (GError) error = NULL;

        // Get the value from the configuration file
        bool boolean = g_key_file_get_boolean (keyfile, group, key, &error);
        if (error) {
            laik_tcp_errors_push (errors, __func__, 0, "Failed to parse the configuration file: (%s, %s) is not a valid boolean", group, key);
            return false;
        }

        // Set the result variable
        *result = boolean;
    }

    return true;
}

__attribute__ ((warn_unused_result))
static bool laik_tcp_config_parse_size (GKeyFile* keyfile, const char* group, const char* key, size_t* result, Laik_Tcp_Errors* errors) {
    laik_tcp_always (keyfile);
    laik_tcp_always (group);
    laik_tcp_always (key);
    laik_tcp_always (result);
    laik_tcp_always (errors);

    // If the keyfile doesn't have the specified key, do nothing
    if (g_key_file_has_key (keyfile, group, key, NULL)) {
        // Initialiaze a GError variable for the various GKeyfile function calls
        g_autoptr (GError) error = NULL;

        // Get the value from the configuration file
        int64_t size = g_key_file_get_int64 (keyfile, group, key, &error);
        if (error) {
            laik_tcp_errors_push (errors, __func__, 0, "Failed to parse the configuration file: (%s, %s) is not a valid size", group, key);
            return false;
        }

        // Set the result variable
        *result = size < 0 ? SIZE_MAX : (size_t) size;
    }

    return true;
}

__attribute__ ((warn_unused_result))
static bool laik_tcp_config_parse_time (GKeyFile* keyfile, const char* group, const char* key, double* result, Laik_Tcp_Errors* errors) {
    laik_tcp_always (keyfile);
    laik_tcp_always (group);
    laik_tcp_always (key);
    laik_tcp_always (result);
    laik_tcp_always (errors);

    // If the keyfile doesn't have the specified key, do nothing
    if (g_key_file_has_key (keyfile, group, key, NULL)) {
        // Initialiaze a GError variable for the various GKeyfile function calls
        g_autoptr (GError) error = NULL;

        // Get the value from the configuration file
        double time = g_key_file_get_double (keyfile, group, key, &error);
        if (error || time < 0) {
            laik_tcp_errors_push (errors, __func__, 0, "Failed to parse the configuration file: (%s, %s) is not a valid time", group, key);
            return false;
        }

        // Set the result variable
        *result = time;
    }

    return true;
}

__attribute__ ((warn_unused_result))
static Laik_Tcp_Config* laik_tcp_config_new_default (void) {
    // Determine the address mapping automatically
    g_autoptr (GPtrArray) addresses = g_ptr_array_new_with_free_func (g_free);
    if (getenv ("OMPI_COMM_WORLD_SIZE") && atol (getenv ("OMPI_COMM_WORLD_SIZE")) > 0) {
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
        .addresses                 = g_steal_pointer (&addresses),
        .backend_async_send        = true,
        .client_activation_timeout = 0.01,
        .client_connection_limit   = 10,
        .client_connection_timeout = 1.0,
        .server_activation_timeout = 0.01,
        .server_connection_limit   = 10,
        .server_connection_timeout = 1.0,
        .socket_backlog            = 10,
        .socket_keepcnt            = 3,
        .socket_keepidle           = 1.0,
        .socket_keepintvl          = 1.0,
        .inbox_size_limit          = 1<<24,
        .outbox_size_limit         = 1<<24,
        .add_retry_timeout         = 0.1,
        .get_first_timeout         = 0.0,
        .get_retry_timeout         = 0.1,

        .references = 1,
    };

    // Return the object
    return this;
}

__attribute__ ((warn_unused_result))
static Laik_Tcp_Config* laik_tcp_config_new_custom (Laik_Tcp_Errors* errors) {
    laik_tcp_always (errors);

    // Construct a default configuration object
    g_autoptr (Laik_Tcp_Config) this = laik_tcp_config_new_default ();

    // If the environment variable is set, load the configuration file
    const char* location = getenv ("LAIK_TCP_CONFIG");
    if (location) {
        // Initialiaze a GError variable for the various GKeyfile function calls
        g_autoptr (GError) error = NULL;

        // Load the configuration file from a path or URL via GIO auto-magic
        g_autoptr (GFile) file = g_file_new_for_commandline_arg (location);
        g_autofree char* data = NULL;
        size_t size = 0;
        g_file_load_contents (file, NULL, &data, &size, NULL, &error);
        if (error) {
            laik_tcp_errors_push_direct (errors, g_steal_pointer (&error));
            laik_tcp_errors_push (errors, __func__, 0, "Failed to load the configuration file from %s", location);
            return NULL;
        }

        // Parse the configuration file as a GKeyFile
        g_autoptr (GKeyFile) keyfile = g_key_file_new ();
        g_key_file_load_from_data (keyfile, data, size, G_KEY_FILE_NONE, &error);
        if (error) {
            laik_tcp_errors_push_direct (errors, g_steal_pointer (&error));
            laik_tcp_errors_push (errors, __func__, 0, "Failed to parse the configuration file from %s", location);
            return NULL;
        }

        // Load the individual settings
        if (!laik_tcp_config_parse_addresses (keyfile, "addresses",                             &this->addresses,                 errors)) { return NULL; };
        if (!laik_tcp_config_parse_bool      (keyfile, "general",  "backend_async_send",        &this->backend_async_send,        errors)) { return NULL; };
        if (!laik_tcp_config_parse_time      (keyfile, "general",  "client_activation_timeout", &this->client_activation_timeout, errors)) { return NULL; };
        if (!laik_tcp_config_parse_size      (keyfile, "general",  "client_connection_limit",   &this->client_connection_limit,   errors)) { return NULL; };
        if (!laik_tcp_config_parse_time      (keyfile, "general",  "client_connection_timeout", &this->client_connection_timeout, errors)) { return NULL; };
        if (!laik_tcp_config_parse_time      (keyfile, "general",  "server_activation_timeout", &this->server_activation_timeout, errors)) { return NULL; };
        if (!laik_tcp_config_parse_size      (keyfile, "general",  "server_connection_limit",   &this->server_connection_limit,   errors)) { return NULL; };
        if (!laik_tcp_config_parse_time      (keyfile, "general",  "server_connection_timeout", &this->server_connection_timeout, errors)) { return NULL; };
        if (!laik_tcp_config_parse_size      (keyfile, "general",  "socket_backlog",            &this->socket_backlog,            errors)) { return NULL; };
        if (!laik_tcp_config_parse_size      (keyfile, "general",  "socket_keepcnt",            &this->socket_keepcnt,            errors)) { return NULL; };
        if (!laik_tcp_config_parse_time      (keyfile, "general",  "socket_keepidle",           &this->socket_keepidle,           errors)) { return NULL; };
        if (!laik_tcp_config_parse_time      (keyfile, "general",  "socket_keepintvl",          &this->socket_keepintvl,          errors)) { return NULL; };
        if (!laik_tcp_config_parse_size      (keyfile, "general",  "inbox_size_limit",          &this->inbox_size_limit,          errors)) { return NULL; };
        if (!laik_tcp_config_parse_size      (keyfile, "general",  "outbox_size_limit",         &this->outbox_size_limit,         errors)) { return NULL; };
        if (!laik_tcp_config_parse_time      (keyfile, "general",  "add_retry_timeout",         &this->add_retry_timeout,         errors)) { return NULL; };
        if (!laik_tcp_config_parse_time      (keyfile, "general",  "get_first_timeout",         &this->get_first_timeout,         errors)) { return NULL; };
        if (!laik_tcp_config_parse_time      (keyfile, "general",  "get_retry_timeout",         &this->get_retry_timeout,         errors)) { return NULL; };
    }

    // Return the object
    return g_steal_pointer (&this);
}

static void* laik_tcp_config_update (void* data) {
    // Try to construct a new configuration object
    g_autoptr (Laik_Tcp_Errors) errors = laik_tcp_errors_new ();
    g_autoptr (Laik_Tcp_Config) update = laik_tcp_config_new_custom (errors);

    LAIK_TCP_LOCK (&lock);

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
    LAIK_TCP_LOCK (&lock);

    // If we don't have a configuration yet, try to get it a number of times
    for (size_t try = 0; config == NULL; try++) {
        g_autoptr (Laik_Tcp_Errors) errors = laik_tcp_errors_new ();

        config    = laik_tcp_config_new_custom (errors);
        timestamp = laik_tcp_time ();

        if (laik_tcp_errors_present (errors)) {
            if (try < 10) {
                laik_tcp_sleep (0.1);
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
    if (!running && laik_tcp_time () - timestamp > 1) {
        laik_tcp_debug ("Configuration outdated, starting update");
        running   = true;
        timestamp = laik_tcp_time ();
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
