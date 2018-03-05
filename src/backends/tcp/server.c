#include "server.h"
#include <glib.h>    // for g_ptr_array_add, GPtrArray, g_free, g_get_monoto...
#include <poll.h>    // for POLLIN
#include <stddef.h>  // for size_t, NULL
#include "debug.h"   // for laik_tcp_debug
#include "errors.h"  // for laik_tcp_always, laik_tcp_errors_present, laik_t...
#include "socket.h"  // for laik_tcp_socket_free, laik_tcp_socket_get_listening

struct Laik_Tcp_Server {
    GPtrArray* connections;
    size_t     limit;
};

Laik_Tcp_Socket* laik_tcp_server_accept (Laik_Tcp_Server* this, int64_t microseconds) {
    laik_tcp_always (this);

    Laik_Tcp_Socket* socket = laik_tcp_socket_poll (this->connections, POLLIN, microseconds);

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

Laik_Tcp_Server* laik_tcp_server_new (Laik_Tcp_Socket* socket, const size_t limit) {
    laik_tcp_always (socket);

    g_autoptr (GPtrArray) connections = g_ptr_array_sized_new (1 + limit);
    g_ptr_array_add (connections, socket);

    Laik_Tcp_Server* this = g_new0 (Laik_Tcp_Server, 1);

    *this = (Laik_Tcp_Server) {
        .connections = g_steal_pointer (&connections),
        .limit       = limit,
    };

    return this;
}

void laik_tcp_server_store (Laik_Tcp_Server* this, Laik_Tcp_Socket* socket) {
    laik_tcp_always (this);
    laik_tcp_always (socket);

    // Make sure we don't exceed the connection limit
    if (this->limit && this->connections->len >= this->limit) {
        const int64_t timestamp = g_get_monotonic_time () - 10 * G_TIME_SPAN_MILLISECOND;
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

    if (this->limit && this->connections->len < this->limit) {
        g_ptr_array_add (this->connections, laik_tcp_socket_touch (socket));
    } else {
        laik_tcp_socket_free (socket);
    }
}
