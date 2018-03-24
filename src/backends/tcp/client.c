#include "client.h"
#include <glib.h>     // for g_free, g_hash_table_new_full, g_hash_table_size
#include <stdbool.h>  // for false, true
#include <stddef.h>   // for NULL, size_t
#include "debug.h"    // for laik_tcp_always, laik_tcp_debug
#include "errors.h"   // for laik_tcp_errors_new, Laik_Tcp_Errors_autoptr
#include "socket.h"   // for Laik_Tcp_Socket, laik_tcp_socket_destroy, laik_...
#include "time.h"     // for laik_tcp_time

struct Laik_Tcp_Client {
    GHashTable* connections;
    size_t      limit;
};

static int laik_tcp_client_too_old (void* key, void* value, void* userdata) {
    laik_tcp_always (key);
    laik_tcp_always (value);
    laik_tcp_always (userdata);

    __attribute__ ((unused))
    const char*            peer       = key;
    const Laik_Tcp_Socket* connection = value;
    const double           timestamp  = * (double*) userdata;

    if (laik_tcp_socket_get_timestamp (connection) < timestamp) {
        laik_tcp_debug ("Removing connection to %s because its timestamp is too old", peer);
        return true;
    }

    return false;
}

__attribute__ ((warn_unused_result))
Laik_Tcp_Socket* laik_tcp_client_connect (Laik_Tcp_Client* this, const char* address) {
    laik_tcp_always (this);
    laik_tcp_always (address);

    char*            stored_address = NULL;
    Laik_Tcp_Socket* connection     = NULL;

    // Check if we already have a connection to this address
    if (g_hash_table_lookup_extended (this->connections, address, (void**) &stored_address, (void**) &connection)) {
        g_hash_table_steal (this->connections, address);
        g_free (stored_address);
        return connection;
    } else {
        g_autoptr (Laik_Tcp_Errors) errors = laik_tcp_errors_new ();
        return laik_tcp_socket_new (LAIK_TCP_SOCKET_TYPE_CLIENT, address, errors);
    }
}

void laik_tcp_client_free (Laik_Tcp_Client* this) {
    if (!this) {
        return;
    }

    g_hash_table_unref (this->connections);

    g_free (this);
}

Laik_Tcp_Client* laik_tcp_client_new (size_t limit) {
    Laik_Tcp_Client* this = g_new0 (Laik_Tcp_Client, 1);

    *this = (Laik_Tcp_Client) {
        .connections = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, laik_tcp_socket_destroy),
        .limit       = limit,
    };

    return this;
}

void laik_tcp_client_store (Laik_Tcp_Client* this, const char* address, Laik_Tcp_Socket* socket) {
    laik_tcp_always (this);
    laik_tcp_always (address);
    laik_tcp_always (socket);

    laik_tcp_debug ("Size before = %u/%zu", g_hash_table_size (this->connections), this->limit);

    if (g_hash_table_size (this->connections) >= this->limit) {
        double timestamp = laik_tcp_time () - 1;
        g_hash_table_foreach_remove (this->connections, laik_tcp_client_too_old, &timestamp);
    }

    if (g_hash_table_size (this->connections) < this->limit) {
        laik_tcp_debug ("Found a free slot insert connection to %s, keeping", address);
        g_hash_table_insert (this->connections, g_strdup (address), laik_tcp_socket_touch (socket));
    } else {
        laik_tcp_debug ("Could not find a free slot to insert connection to %s, releasing", address);
        laik_tcp_socket_free (socket);
    }

    laik_tcp_debug ("Size after = %u/%zu", g_hash_table_size (this->connections), this->limit);
}
