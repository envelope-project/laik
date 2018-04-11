#include "socketpair.h"
#include <stddef.h>      // for NULL
#include <sys/socket.h>  // for socketpair, AF_UNIX, SOCK_STREAM
#include "socket.h"      // for laik_tcp_socket_new_from_fd, laik_tcp_socket...

void laik_tcp_socket_pair_free (Laik_Tcp_SocketPair* this) {
    if (!this) {
        return;
    }

    // Free the contained objects
    laik_tcp_socket_free (this->primary);
    laik_tcp_socket_free (this->secondary);

    // Free ourselves
    g_free (this);
}

Laik_Tcp_SocketPair* laik_tcp_socket_pair_new (void) {
    // Create the socket pair
    int sockets[2];
    if (socketpair (AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
        return NULL;
    }

    // Create the object
    Laik_Tcp_SocketPair* this = g_new (Laik_Tcp_SocketPair, 1);

    // Initialize the object
    *this = (Laik_Tcp_SocketPair) {
        .primary   = laik_tcp_socket_new_from_fd (sockets[0]),
        .secondary = laik_tcp_socket_new_from_fd (sockets[1]),
    };

    // Return the object
    return this;
}
