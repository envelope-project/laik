#include "server.h"
#include <glib.h>         // for g_async_queue_new_full, g_ptr_array_new
#include <poll.h>         // for POLLIN
#include <stdbool.h>      // for true, bool, false
#include <stddef.h>       // for size_t, NULL
#include <unistd.h>       // for ssize_t
#include "config.h"       // for laik_tcp_config, Laik_Tcp_Config, Laik_Tcp_...
#include "debug.h"        // for laik_tcp_always, laik_tcp_debug
#include "lock.h"         // for laik_tcp_lock_new, laik_tcp_lock_free, LAIK...
#include "socket.h"       // for laik_tcp_socket_free, Laik_Tcp_Socket, laik...
#include "socketqueue.h"  // for laik_tcp_socket_queue_push, laik_tcp_socket...

struct Laik_Tcp_Server {
    bool                     shutdown;
    GAsyncQueue*             returned;
    GPtrArray*               threads;
    Laik_Tcp_Lock*           lock;
    Laik_Tcp_ServerFunction* function;
    Laik_Tcp_Socket*         listener;
    Laik_Tcp_SocketQueue*    sockets;
    void*                    userdata;
};

static Laik_Tcp_Socket* laik_tcp_server_accept (Laik_Tcp_Server* this) {
    laik_tcp_always (this);

    LAIK_TCP_LOCK (this->lock);

    // Get the configuration
    g_autoptr (Laik_Tcp_Config) config = laik_tcp_config ();

    while (!this->shutdown) {
        // Add all returned sockets to the socket queue
        while (true) {
            Laik_Tcp_Socket* socket = g_async_queue_try_pop (this->returned);
            if (socket) {
                laik_tcp_socket_queue_push (this->sockets, socket, POLLIN);
            } else {
                break;
            }
        }

        // If we have exceeded the limit, drop all connection sockets
        const size_t size = laik_tcp_socket_queue_get_size (this->sockets);
        if (size > config->server_connections) {
            laik_tcp_debug ("Connection limit exceeded with %zu/%zu sockets, dropping all connections", size, config->server_connections);
            for (ssize_t index = (ssize_t) size - 1; index >= 0; index--) {
                Laik_Tcp_Socket* socket = laik_tcp_socket_queue_get_socket (this->sockets, index);
                if (socket != this->listener) {
                    laik_tcp_socket_queue_remove (this->sockets, index);
                    laik_tcp_socket_free (socket);
                }
            }
        }

        // Find the first socket in the queue which has input available
        Laik_Tcp_Socket* socket = laik_tcp_socket_queue_pop (this->sockets);
        if (socket) {
            if (socket == this->listener) {
                // Push the listener back into queue
                laik_tcp_socket_queue_push (this->sockets, socket, POLLIN);

                // Try to accept the connection
                Laik_Tcp_Socket* new = laik_tcp_socket_accept (socket);
                if (new) {
                    return new;
                } else {
                    // This shouldn't happen, but we can just continue here
                }
            } else {
                // Check if there is really input or just EOF
                if (laik_tcp_socket_get_closed (socket)) {
                    // EOF, close our side of the connection
                    laik_tcp_socket_free (socket);
                } else {
                    // input, return the socket
                    return socket;
                }
            }
        } else {
            // The blocking pop() got cancelled by another thread, try again
        }
    }

    return NULL;
}

static void* laik_tcp_server_run (void* data) {
    laik_tcp_always (data);

    Laik_Tcp_Server* this = data;

    while (true) {
        Laik_Tcp_Socket* socket = laik_tcp_server_accept (this);
        if (socket) {
            if (this->function (socket, this->userdata)) {
                // The function succeeded, return the socket and cancel any waiters
                g_async_queue_push (this->returned, socket);
                laik_tcp_socket_queue_cancel (this->sockets);
            } else {
                // The function failed, close the socket
                laik_tcp_socket_free (socket);
            }
        } else {
            return NULL;
        }
    }
}

void laik_tcp_server_free (Laik_Tcp_Server* this) {
    if (!this) {
        return;
    }

    // Signal the worker threads that we are shutting down
    this->shutdown = true;
    laik_tcp_socket_queue_cancel (this->sockets);

    // Wait for the worker threads to complete
    for (size_t index = 0; index < this->threads->len; index++) {
        laik_tcp_debug ("Waiting for worker thread #%zu", index);
        GThread* thread = g_ptr_array_index (this->threads, index);
        g_thread_join (thread);
    }

    // Close the connection FDs
    const size_t size = laik_tcp_socket_queue_get_size (this->sockets);
    for (size_t index = 0; index < size; index++) {
        Laik_Tcp_Socket* socket = laik_tcp_socket_queue_get_socket (this->sockets, index);
        if (socket != this->listener) {
            laik_tcp_socket_free (socket);
        }
    }

    // Free the contained objects
    g_async_queue_unref        (this->returned);
    g_ptr_array_unref          (this->threads);
    laik_tcp_lock_free         (this->lock);
    laik_tcp_socket_free       (this->listener);
    laik_tcp_socket_queue_free (this->sockets);

    // Free ourselves
    g_free (this);
}

Laik_Tcp_Server* laik_tcp_server_new (Laik_Tcp_Socket* socket, Laik_Tcp_ServerFunction* function, void* userdata) {
    laik_tcp_always (socket);

    // Get the configuration
    g_autoptr (Laik_Tcp_Config) config = laik_tcp_config ();

    // Create the object
    Laik_Tcp_Server* this = g_new (Laik_Tcp_Server, 1);

    // Initialiaze the object
    *this = (Laik_Tcp_Server) {
        .shutdown = false,
        .returned = g_async_queue_new_full (laik_tcp_socket_destroy),
        .threads  = g_ptr_array_new (),
        .lock     = laik_tcp_lock_new (),
        .function = function,
        .listener = socket,
        .sockets  = laik_tcp_socket_queue_new (),
        .userdata = userdata,
    };

    // Insert the listening socket into the socket queue
    laik_tcp_socket_queue_push  (this->sockets, this->listener, POLLIN);

    // Start the worker threads
    for (size_t index = 0; index < config->server_threads; index++) {
        laik_tcp_debug ("Starting worker thread #%zu", index);
        g_ptr_array_add (this->threads, g_thread_new ("server thread", laik_tcp_server_run, this));
    }

    // Return the object
    return this;
}
