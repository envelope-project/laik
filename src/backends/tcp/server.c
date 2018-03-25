#include "server.h"
#include <glib.h>    // for g_ptr_array_add, GPtrArray, g_free, g_malloc0_n
#include <poll.h>    // for POLLIN
#include <stddef.h>  // for size_t, NULL
#include "config.h"  // for laik_tcp_config, Laik_Tcp_Config, Laik_Tcp_Confi...
#include "debug.h"   // for laik_tcp_always, laik_tcp_debug
#include "socket.h"  // for laik_tcp_socket_free, laik_tcp_socket_get_listening
#include "time.h"    // for laik_tcp_time

struct Laik_Tcp_Server {
    GPtrArray* connections;
};

Laik_Tcp_Socket* laik_tcp_server_accept (Laik_Tcp_Server* this, double seconds) {
    laik_tcp_always (this);

    Laik_Tcp_Socket* socket = laik_tcp_socket_poll (this->connections, POLLIN, seconds);

    if (!socket) {
        return NULL;
    }

    if (laik_tcp_socket_get_listening (socket)) {
        laik_tcp_debug ("Server socket found to be ready, accepting");
        return laik_tcp_socket_accept (socket);
    } else {
        laik_tcp_debug ("Connection socket found to be ready, returning");
        g_ptr_array_remove (this->connections, socket);
        return socket;
    }
}

void laik_tcp_server_free (Laik_Tcp_Server* this) {
    if (!this) {
        return;
    }

    for (size_t i = 0; i < this->connections->len; i++) {
        laik_tcp_socket_free (g_ptr_array_index (this->connections, i));
    }

    g_ptr_array_unref (this->connections);

    g_free (this);
}

Laik_Tcp_Server* laik_tcp_server_new (Laik_Tcp_Socket* socket) {
    laik_tcp_always (socket);

    g_autoptr (GPtrArray) connections = g_ptr_array_new ();
    g_ptr_array_add (connections, socket);

    Laik_Tcp_Server* this = g_new0 (Laik_Tcp_Server, 1);

    *this = (Laik_Tcp_Server) {
        .connections = g_steal_pointer (&connections),
    };

    return this;
}

void laik_tcp_server_store (Laik_Tcp_Server* this, Laik_Tcp_Socket* socket) {
    laik_tcp_always (this);
    laik_tcp_always (socket);

    g_autoptr (Laik_Tcp_Config) config = laik_tcp_config ();

    laik_tcp_debug ("%u/%zu connections used before insertion", this->connections->len, config->server_connection_limit);

    // If we have reached the connection limit, remove all stale connections
    if (this->connections->len >= config->server_connection_limit) {
        const double timestamp = laik_tcp_time () - config->server_connection_timeout;
        for (size_t index = 0; index < this->connections->len;) {
            Laik_Tcp_Socket* candidate = g_ptr_array_index (this->connections, index);
            if (laik_tcp_socket_get_listening (candidate) || laik_tcp_socket_get_timestamp (candidate) > timestamp) {
                laik_tcp_debug ("Not removing socket");
                index++;
            } else {
                laik_tcp_debug ("Removing socket");
                laik_tcp_socket_free (candidate);
                g_ptr_array_remove_index (this->connections, index);
            }
        }
    }

    // If we have room left, add the connection, otherwise close it
    if (this->connections->len < config->server_connection_limit) {
        g_ptr_array_add (this->connections, laik_tcp_socket_touch (socket));
    } else {
        laik_tcp_socket_free (socket);
    }

    laik_tcp_debug ("%u/%zu connections used after insertion", this->connections->len, config->server_connection_limit);
}
