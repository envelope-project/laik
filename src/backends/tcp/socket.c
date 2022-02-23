/*
 * This file is part of the LAIK library.
 * Copyright (c) 2018 Alexander Kurtz <alexander@kurtz.be>
 *
 * LAIK is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 3 or later.
 *
 * LAIK is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "socket.h"
#include <errno.h>        // for errno, EAGAIN, EWOULDBLOCK
#include <fcntl.h>        // for fcntl, F_GETFL, F_SETFL, O_NONBLOCK
#include <glib.h>         // for g_malloc0_n, g_autofree, g_autoptr, g_bytes...
#include <netdb.h>        // for getaddrinfo
#include <netinet/in.h>   // for IPPROTO_TCP
#include <netinet/tcp.h>  // for TCP_NODELAY
#include <poll.h>         // for poll, pollfd, POLLIN, POLLOUT
#include <stdbool.h>      // for false, bool, true
#include <stddef.h>       // for NULL, size_t
#include <stdio.h>        // for snprintf
#include <string.h>       // for strerror, strsep
#include <sys/socket.h>   // for recv, setsockopt, sockaddr, accept, bind
#include <sys/un.h>       // for sockaddr_un, sa_family_t
#include <unistd.h>       // for close, ssize_t
#include "addressinfo.h"  // for Laik_Tcp_AddressInfo, Laik_Tcp_AddressInfo_...
#include "config.h"       // for laik_tcp_config, Laik_Tcp_Config, Laik_Tcp_...
#include "debug.h"        // for laik_tcp_always, laik_tcp_debug
#include "errors.h"       // for laik_tcp_errors_push, Laik_Tcp_Errors

struct Laik_Tcp_Socket {
    int fd;
};

#ifdef MSG_NOSIGNAL
static const int laik_tcp_socket_send_flags = MSG_NOSIGNAL;
#else
static const int laik_tcp_socket_send_flags = 0;
#endif

Laik_Tcp_Socket* laik_tcp_socket_accept (Laik_Tcp_Socket* this) {
    laik_tcp_always (this);

    // Try to accept a new connection
    int fd = accept (this->fd, NULL, NULL);

    // If we succeeded, wrap and return the FD, otherwise return failure
    if (fd >= 0) {
        return laik_tcp_socket_new_from_fd (fd);
    } else {
        return NULL;
    }
}

void laik_tcp_socket_destroy (void* this) {
    laik_tcp_socket_free (this);
}

void laik_tcp_socket_free (Laik_Tcp_Socket* this) {
    if (!this) {
        return;
    }

    close (this->fd);

    g_free (this);
}

bool laik_tcp_socket_get_closed (Laik_Tcp_Socket* this) {
    laik_tcp_always (this);

    // Allocate a dummy buffer
    char dummy;

    // Try to peek 1 byte from the stream, but only if it's immediatly available
    const ssize_t result = recv (this->fd, &dummy, sizeof (dummy), MSG_PEEK | MSG_DONTWAIT);

    // If we successfully read data (result >= 0), but only 0 bytes, we have EOF
    return result == 0;
}

struct pollfd laik_tcp_socket_get_pollfd (Laik_Tcp_Socket* this, short events) {
    laik_tcp_always (this);

    const struct pollfd result = {
        .fd      = this->fd,
        .events  = events,
        .revents = 0,
    };

    return result;
}

Laik_Tcp_Socket* laik_tcp_socket_new (Laik_Tcp_SocketType type, const size_t rank, Laik_Tcp_Errors* errors) {
    laik_tcp_always (errors);

    g_autoptr (Laik_Tcp_Config) config = laik_tcp_config ();

    // Create variables to store the sockaddr struct and its size
    g_autofree struct sockaddr* socket_address_data = NULL;
    socklen_t socket_address_size = 0;

    // Get the requested address from the configuration
    if (rank >= config->addresses->len) {
        laik_tcp_errors_push (errors, __func__, -1, "Address for rank %zu not present in configuration", rank);
        return NULL;
    }
    const char* address = g_ptr_array_index (config->addresses, rank);

    // Split the address on the first white space
    g_autofree char* duplicate = g_strdup (address);
    char* remainder  = duplicate;
    char* first_word = strsep (&remainder, " \t\n");

    // Do we want a TCP socket with host and port or an abstract UNIX socket?
    if (first_word && remainder) {
        laik_tcp_debug ("Trying to create a TCP socket with host %s and port %s", first_word, remainder);

        // Create a hints struct for getaddrinfo
        Laik_Tcp_AddressInfo hints = {
            .ai_socktype = SOCK_STREAM,
        };

        // Create a result variable for getaddrinfo
        g_autoptr (Laik_Tcp_AddressInfo) addresses = NULL;

        // Call getaddrinfo with the host and port extracted from the address
        int result = getaddrinfo (first_word, remainder, &hints, &addresses);
        if (result != 0) {
            laik_tcp_errors_push (errors, __func__, 0, "getaddrinfo (%s, %s) failed: %s", first_word, remainder, strerror (errno));
            return NULL;
        }

        // Fill the socket address variables
        socket_address_size = addresses->ai_addrlen;
#if GLIB_VERSION_2_68
        socket_address_data = g_memdup2 (addresses->ai_addr, addresses->ai_addrlen);
#else
        socket_address_data = g_memdup (addresses->ai_addr, addresses->ai_addrlen);
#endif
    } else {
        laik_tcp_debug ("Trying to create an abstract UNIX socket with name %s", address);

        // Create a sockaddr_un struct
        g_autofree struct sockaddr_un* sockaddr_un = g_new0 (struct sockaddr_un, 1);

        // Fill the sockaddr_un struct
        sockaddr_un->sun_family = AF_UNIX;
        const int bytes = snprintf (sockaddr_un->sun_path, sizeof (sockaddr_un->sun_path), "%c%s", 0, address);

        // Check if snprintf failed
        if (bytes < 0) { 
            laik_tcp_errors_push (errors, __func__, 1, "snprintf failed while settin up an abstract UNIX socket for address '%s': %s", address, strerror (errno));
            return NULL;
        }

        // Check if we exceeded the size. Note that > is correct here, since
        // abstract UNIX sockets are *not* NUL-terminated. See also unix(7).
        if ((size_t) bytes > sizeof (sockaddr_un->sun_path)) { 
            laik_tcp_errors_push (errors, __func__, 2, "Address '%s' is too long for abstract UNIX socket", address);
            return NULL;
        }

        // Fill the socket address variables
        socket_address_size = sizeof (sockaddr_un->sun_family) + bytes;
        socket_address_data = (struct sockaddr*) g_steal_pointer (&sockaddr_un);
    }

    // Create a suitable socket
    int fd = socket (socket_address_data->sa_family, SOCK_STREAM, 0);
    if (fd < 0) {
        laik_tcp_errors_push (errors, __func__, 3, "Failed to create streaming socket in address family %d: %s", (int) socket_address_data->sa_family, strerror (errno));
        return NULL;
    }

    // If we can (e.g. on FreeBSD [0]), disable SIGPIPE for this socket entirely
    // [0] https://www.freebsd.org/cgi/man.cgi?query=setsockopt
    #ifdef SO_NOSIGPIPE
    if (setsockopt (fd, SOL_SOCKET, SO_NOSIGPIPE, & (int) { 1 }, sizeof (int)) != 0) {
        laik_tcp_errors_push (errors, __func__, 4, "Failed to set SO_NOSIGPIPE on socket: %s", strerror (errno));
        close (fd);
        return NULL;
    }
    #endif

    // On TCP server sockets, set some extra options 
    if (socket_address_data->sa_family == AF_INET || socket_address_data->sa_family == AF_INET6) {
        // Enable reuse of recently freed ports
        if (setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, & (int) { 1 }, sizeof (int)) != 0) {
            laik_tcp_errors_push (errors, __func__, 4, "Failed to set SO_REUSEADDR on socket: %s", strerror (errno));
            close (fd);
            return NULL;
        }

        // Disable Nagle's algorithm so messages are sent without delays
        if (setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, & (int) { 1 }, sizeof (int)) != 0) {
            laik_tcp_errors_push (errors, __func__, 6, "Failed to set TCP_NODELAY on socket: %s", strerror (errno));
            close (fd);
            return NULL;
        }
    }

    // Handle the different socket types
    if (type == LAIK_TCP_SOCKET_TYPE_CLIENT) {
        if (connect (fd, socket_address_data, socket_address_size) != 0) {
            laik_tcp_errors_push (errors, __func__, 10, "Failed to connect to %s: %s", address, strerror (errno));
            close (fd);
            return NULL;
        }
    } else if (type == LAIK_TCP_SOCKET_TYPE_SERVER) {
        if (bind (fd, socket_address_data, socket_address_size) != 0) {
            laik_tcp_errors_push (errors, __func__, 11, "Failed to bind socket to '%s': %s", address, strerror (errno));
            close (fd);
            return NULL;
        }

        if (listen (fd, config->socket_backlog) != 0 && listen (fd, config->socket_backlog) != 0) {
            laik_tcp_errors_push (errors, __func__, 12, "Failed to listen on socket bound to address '%s': %s", address, strerror (errno));
            close (fd);
            return NULL;
        }
    } else {
        laik_tcp_errors_push (errors, __func__, 13, "Invalid socket type %d", type);
        return NULL;
    }

    // Wrap the FD and return it
    return laik_tcp_socket_new_from_fd (fd);
}

Laik_Tcp_Socket* laik_tcp_socket_new_from_fd (int fd) {
    laik_tcp_always (fd >= 0);

    // Disable blocking
    int flags = fcntl (fd, F_GETFL, 0);
    laik_tcp_always (flags >= 0);
    __attribute__ ((unused)) int result = fcntl (fd, F_SETFL, flags | O_NONBLOCK);
    laik_tcp_always (result >= 0);

    // Create the object
    Laik_Tcp_Socket* this = g_new (Laik_Tcp_Socket, 1);

    // Initialize the object
    *this = (Laik_Tcp_Socket) {
        .fd = fd,
    };

    // Return the object
    return this;
}

GBytes* laik_tcp_socket_receive_bytes (Laik_Tcp_Socket* this) {
    laik_tcp_always (this);

    uint64_t size;

    if (!laik_tcp_socket_receive_uint64 (this, &size)) {
        return NULL;
    }

    g_autofree void* data = g_malloc (size);

    if (!laik_tcp_socket_receive_data (this, data, size)) {
        return NULL;
    }

    return g_bytes_new_take (g_steal_pointer (&data), size);
}

bool laik_tcp_socket_receive_data (Laik_Tcp_Socket* this, void* data, const size_t size) {
    laik_tcp_always (this);
    laik_tcp_always (data || !size);

    // Get the configuration
    g_autoptr (Laik_Tcp_Config) config = laik_tcp_config ();

    // Initialize the loop variables
    char*   pointer   = data;
    ssize_t remaining = size;

    // Receive until we are done or an error occurs
    while (remaining > 0) {
        ssize_t result = laik_tcp_socket_try_receive (this, pointer, remaining);
        if (result > 0) {
            pointer   += result;
            remaining -= result;
        } else if (result == 0) {
            return false;
        } else if (result == -EAGAIN || result == -EWOULDBLOCK) {
            if (!laik_tcp_socket_wait (this, POLLIN, config->socket_timeout)) {
                return false;
            }
        } else {
            return false;
        }
    }

    return true;
}

bool laik_tcp_socket_receive_uint64 (Laik_Tcp_Socket* this, uint64_t* value) {
    laik_tcp_always (this);
    laik_tcp_always (value);

    if (!laik_tcp_socket_receive_data (this, value, sizeof (*value))) {
        return false;
    }

    *value = GUINT64_FROM_LE (*value);

    return true;
}

bool laik_tcp_socket_send_bytes (Laik_Tcp_Socket* this, GBytes* bytes) {
    laik_tcp_always (this);
    laik_tcp_always (bytes);

    size_t size;
    const void* data = g_bytes_get_data (bytes, &size);

    if (!laik_tcp_socket_send_uint64 (this, size)) {
        return false;
    }

    if (!laik_tcp_socket_send_data (this, data, size)) {
        return false;
    }

    return true;
}

bool laik_tcp_socket_send_data (Laik_Tcp_Socket* this, const void* data, const size_t size) {
    laik_tcp_always (this);
    laik_tcp_always (data || !size);

    // Get the configuration
    g_autoptr (Laik_Tcp_Config) config = laik_tcp_config ();

    // Initialize the loop variables
    const char* pointer   = data;
    ssize_t     remaining = size;

    // Send until we are done or an error occurs
    while (remaining > 0) {
        ssize_t result = laik_tcp_socket_try_send (this, pointer, remaining);
        if (result >= 0) {
            pointer   += result;
            remaining -= result;
        } else if (result == -EAGAIN || result == -EWOULDBLOCK) {
            if (!laik_tcp_socket_wait (this, POLLOUT, config->socket_timeout)) {
                return false;
            }
        } else {
            return false;
        }
    }

    return true;
}

bool laik_tcp_socket_send_uint64 (Laik_Tcp_Socket* this, uint64_t value) {
    laik_tcp_always (this);

    value = GUINT64_TO_LE (value);

    return laik_tcp_socket_send_data (this, &value, sizeof (value));
}

ssize_t laik_tcp_socket_try_receive (Laik_Tcp_Socket* this, void* data, const size_t size) {
    laik_tcp_always (this);
    laik_tcp_always (data || !size);

    ssize_t result = recv (this->fd, data, size, 0);

    return result >= 0 ? result : -errno;
}

ssize_t laik_tcp_socket_try_send (Laik_Tcp_Socket* this, const void* data, const size_t size) {
    laik_tcp_always (this);
    laik_tcp_always (data || !size);

    ssize_t result = send (this->fd, data, size, laik_tcp_socket_send_flags);

    return result >= 0 ? result : -errno;
}

bool laik_tcp_socket_wait (Laik_Tcp_Socket* this, const short events, const double seconds) {
    laik_tcp_always (this);

    struct pollfd pollfd = laik_tcp_socket_get_pollfd (this, events);

    return poll (&pollfd, 1, seconds * 1000) == 1;
}
