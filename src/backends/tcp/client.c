#include "client.h"
#include <glib.h>     // for GPtrArray, g_async_queue_new_full, g_ptr_array_...
#include <stdbool.h>  // for true
#include <stddef.h>   // for size_t, NULL
#include <string.h>   // for strcmp
#include "config.h"   // for laik_tcp_config, Laik_Tcp_Config, Laik_Tcp_Conf...
#include "debug.h"    // for laik_tcp_always, laik_tcp_debug
#include "errors.h"   // for laik_tcp_errors_new, Laik_Tcp_Errors_autoptr
#include "lock.h"     // for laik_tcp_lock_new, laik_tcp_lock_free, LAIK_TCP...
#include "socket.h"   // for Laik_Tcp_Socket, laik_tcp_socket_free, laik_tcp...

struct Laik_Tcp_Client {
    Laik_Tcp_Lock* lock;
    GPtrArray*     sockets;
    GArray*        mapping;
    GThreadPool*   pool;
};

static void laik_tcp_client_add (Laik_Tcp_Client* this, size_t address, Laik_Tcp_Socket* socket) {
    laik_tcp_always (this);
    laik_tcp_always (socket);
    laik_tcp_always (this->sockets->len == this->mapping->len);

    g_ptr_array_add     (this->sockets, socket);
    g_array_append_vals (this->mapping, &address, 1);
}

static void laik_tcp_client_drop (Laik_Tcp_Client* this, const size_t index) {
    laik_tcp_always (this);
    laik_tcp_always (index < this->sockets->len);
    laik_tcp_always (this->sockets->len == this->mapping->len);

    g_ptr_array_remove_index (this->sockets, index);
    g_array_remove_index     (this->mapping, index);
}

static void laik_tcp_client_take (Laik_Tcp_Client* this, const size_t index) {
    laik_tcp_always (this);
    laik_tcp_always (index < this->sockets->len);

    // Prevent the socket from being closed by the GPtrArray's destroy function
    g_ptr_array_index (this->sockets, index) = NULL;

    // Remove the socket from the FIFO array
    laik_tcp_client_drop (this, index);
}

Laik_Tcp_Socket* laik_tcp_client_connect (Laik_Tcp_Client* this, size_t address) {
    laik_tcp_always (this);

    {
        LAIK_TCP_LOCK (this->lock);

        // Search for an existing socket to the requested address
        for (size_t index = 0; index < this->sockets->len;) {
            Laik_Tcp_Socket* socket = g_ptr_array_index (this->sockets, index);
            size_t           stored = g_array_index     (this->mapping, size_t, index);

            if (stored == address) {
                if (laik_tcp_socket_get_closed (socket)) {
                    // An established connection was closed, drop it.
                    laik_tcp_client_drop (this, index);
                } else {
                    // We have found an established connection, return it
                    laik_tcp_client_take (this, index);
                    return socket;
                }
            } else {
                index++;
            }
        }
    }

    // No established connection found, create a new connection
    g_autoptr (Laik_Tcp_Errors) errors = laik_tcp_errors_new ();
    return laik_tcp_socket_new (LAIK_TCP_SOCKET_TYPE_CLIENT, address, errors);
}

void laik_tcp_client_free (Laik_Tcp_Client* this) {
    if (!this) {
        return;
    }

    g_thread_pool_free (this->pool, false, true);

    laik_tcp_lock_free (this->lock);
    g_ptr_array_unref  (this->sockets);
    g_array_unref      (this->mapping);

    g_free (this);
}

Laik_Tcp_Client* laik_tcp_client_new (Laik_Tcp_ClientFunction* function, void* userdata) {
    laik_tcp_always (function);
    laik_tcp_always (userdata);

    // Get the configuration
    g_autoptr (Laik_Tcp_Config) config = laik_tcp_config ();

    // Create the object
    Laik_Tcp_Client* this = g_new (Laik_Tcp_Client, 1);

    // Initialiaze the object
    *this = (Laik_Tcp_Client) {
        .lock    = laik_tcp_lock_new (),
        .sockets = g_ptr_array_new_with_free_func (laik_tcp_socket_destroy),
        .mapping = g_array_new (false, false, sizeof (size_t)),
        .pool    = g_thread_pool_new (function, userdata, config->client_threads, false, NULL),

    };

    // Return the object
    return this;
}

void laik_tcp_client_push (Laik_Tcp_Client* this, void* data) {
    laik_tcp_always (this);
    laik_tcp_always (data);

    g_thread_pool_push (this->pool, data, NULL);
}

void laik_tcp_client_store (Laik_Tcp_Client* this, size_t address, Laik_Tcp_Socket* socket) {
    laik_tcp_always (this);
    laik_tcp_always (socket);

    LAIK_TCP_LOCK (this->lock);

    // Get the configuration
    g_autoptr (Laik_Tcp_Config) config = laik_tcp_config ();

    // Add the returned socket to the FIFO array
    laik_tcp_client_add (this, address, socket);

    // If we have exceeded the limit, drop all connections
    if (this->sockets->len > config->client_connections) {
        laik_tcp_debug ("Connection limit exceeded with %u/%zu sockets, dropping all connections", this->sockets->len, config->client_connections);
        for (size_t index = 0; index < this->sockets->len;) {
            laik_tcp_client_drop (this, index);
        }
    }
}
