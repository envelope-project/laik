#include "socketqueue.h"
#include <glib.h>        // for GPtrArray, GArray, g_array_new, g_array_remo...
#include <poll.h>        // for pollfd, poll, POLLIN, POLLNVAL
#include <stdbool.h>     // for false, true
#include <stddef.h>      // for size_t, NULL
#include <sys/types.h>   // for ssize_t
#include "debug.h"       // for laik_tcp_always
#include "socket.h"      // for Laik_Tcp_Socket, laik_tcp_socket_get_pollfd
#include "socketpair.h"  // for laik_tcp_socket_pair_new, Laik_Tcp_SocketPair

static char laik_tcp_socket_queue_dummy[1024];

struct Laik_Tcp_SocketQueue {
    GPtrArray*           sockets;
    GArray*              pollfds;
    Laik_Tcp_SocketPair* signal;
};

void laik_tcp_socket_queue_cancel (Laik_Tcp_SocketQueue* this) {
    laik_tcp_always (this);

    __attribute__ ((unused)) ssize_t result = laik_tcp_socket_try_send (this->signal->primary, laik_tcp_socket_queue_dummy, 1);
}

void laik_tcp_socket_queue_free (Laik_Tcp_SocketQueue* this) {
    if (!this) {
        return;
    }

    // Free the contained objects
    g_ptr_array_unref         (this->sockets);
    g_array_unref             (this->pollfds);
    laik_tcp_socket_pair_free (this->signal);

    // Free ourselves
    g_free (this);
}

size_t laik_tcp_socket_queue_get_size (Laik_Tcp_SocketQueue* this) {
    laik_tcp_always (this);
    laik_tcp_always (this->sockets->len >= 1);

    // Return the size, but remember that index 0 is special
    return this->sockets->len - 1;
}

Laik_Tcp_Socket* laik_tcp_socket_queue_get_socket (Laik_Tcp_SocketQueue* this, size_t index) {
    laik_tcp_always (this);
    laik_tcp_always (index + 1 < this->sockets->len);

    // Return the socket, but remember that index 0 is special
    return g_ptr_array_index (this->sockets, index + 1);
}

Laik_Tcp_SocketQueue* laik_tcp_socket_queue_new (void) {
    // Create the object
    Laik_Tcp_SocketQueue* this = g_new (Laik_Tcp_SocketQueue, 1);

    // Initialize the object
    *this = (Laik_Tcp_SocketQueue) {
        .sockets = g_ptr_array_new (),
        .pollfds = g_array_new (false, false, sizeof (struct pollfd)),
        .signal  = laik_tcp_socket_pair_new (),
    };

    // Push the signal socket pair as a special first element into the queue
    laik_tcp_socket_queue_push (this, this->signal->secondary, POLLIN);

    // Return the object
    return this;
}

Laik_Tcp_Socket* laik_tcp_socket_queue_pop (Laik_Tcp_SocketQueue* this) {
    laik_tcp_always (this);
    laik_tcp_always (this->sockets->len == this->pollfds->len);

    while (true) {
        // Iterate over the queue and try to find the first socket with an event
        for (size_t index = 0; index < this->pollfds->len; index++) {
            // Get a pointer to the current pollfd structure
            struct pollfd* pollfd = &g_array_index (this->pollfds, struct pollfd, index);

            // Make sure we didn't call poll(2) with invalid values
            laik_tcp_always (!(pollfd->revents & POLLNVAL));

            // Check if the socket has an event ready
            if (pollfd->events & pollfd->revents) {
                if (index == 0) {
                    // The signal socket pair has an event ready, which means
                    // we got cancelled, so drain all the data, reset the
                    // "revents" field and return NULL
                    while (laik_tcp_socket_try_receive (this->signal->secondary, laik_tcp_socket_queue_dummy, sizeof (laik_tcp_socket_queue_dummy)) > 0);
                    pollfd->revents = 0;
                    return NULL;
                } else {
                    // A regular socket has an event, so remove and return it
                    Laik_Tcp_Socket* result = g_ptr_array_index (this->sockets, index);
                    g_ptr_array_remove_index (this->sockets, index);
                    g_array_remove_index     (this->pollfds, index);
                    return result;
                }
            }
        }

        // No luck yet, do a poll(2) call to block until there is an event
        __attribute__ ((unused)) int ready = poll ((struct pollfd*) this->pollfds->data, this->pollfds->len, -1);
        laik_tcp_always (ready >= 1);
    }
}

void laik_tcp_socket_queue_push (Laik_Tcp_SocketQueue* this, Laik_Tcp_Socket* socket, const short events) {
    laik_tcp_always (this);
    laik_tcp_always (socket);
    laik_tcp_always (this->sockets->len == this->pollfds->len);

    // Create a suitable pollfd structure
    struct pollfd pollfd = laik_tcp_socket_get_pollfd (socket, events);

    // Append the socket and the pollfd structure
    g_ptr_array_add     (this->sockets, socket);
    g_array_append_vals (this->pollfds, &pollfd, 1);
}

void laik_tcp_socket_queue_remove (Laik_Tcp_SocketQueue* this, const size_t index) {
    laik_tcp_always (this);
    laik_tcp_always (index + 1 < this->sockets->len);
    laik_tcp_always (this->sockets->len == this->pollfds->len);

    // Remove the specified index, but remember that index 0 is special
    g_ptr_array_remove_index (this->sockets, index + 1);
    g_array_remove_index     (this->pollfds, index + 1);
}
