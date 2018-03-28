#include "socket.h"
#include <errno.h>        // for errno
#include <glib.h>         // for g_malloc0_n, GPtrArray, g_autofree, g_new0
#include <netdb.h>        // for getaddrinfo
#include <netinet/in.h>   // for IPPROTO_TCP
#include <netinet/tcp.h>  // for TCP_KEEPCNT, TCP_KEEPIDLE, TCP_KEEPINTVL
#include <poll.h>         // for pollfd, poll, POLLNVAL
#include <stdbool.h>      // for bool, true, false
#include <stddef.h>       // for NULL, size_t
#include <stdio.h>        // for snprintf
#include <string.h>       // for strerror, strsep
#include <sys/socket.h>   // for setsockopt, sockaddr, SOL_SOCKET, accept, bind
#include <sys/un.h>       // for sockaddr_un, sa_family_t
#include <unistd.h>       // for close, ssize_t
#include "addressinfo.h"  // for Laik_Tcp_AddressInfo, Laik_Tcp_AddressInfo_...
#include "config.h"       // for Laik_Tcp_Config, laik_tcp_config, Laik_Tcp_...
#include "debug.h"        // for laik_tcp_always, laik_tcp_debug
#include "errors.h"       // for laik_tcp_errors_push, Laik_Tcp_Errors
#include "time.h"         // for laik_tcp_time

struct Laik_Tcp_Socket {
    int    fd;
    short  events;
    double timestamp;
};

__attribute__ ((warn_unused_result))
static bool laik_tcp_socket_receive_raw (Laik_Tcp_Socket* this, void* data, size_t size) {
    laik_tcp_always (this);
    laik_tcp_always (data || !size);

    if (size) {
        return recv (this->fd, data, size, MSG_WAITALL) == (ssize_t) size;
    } else {
        return true;
    }
}

__attribute__ ((warn_unused_result))
static bool laik_tcp_socket_send_raw (Laik_Tcp_Socket* this, const void* data, size_t size) {
    laik_tcp_always (this);
    laik_tcp_always (data || !size);

    if (size) {
        #ifdef MSG_NOSIGNAL
            return send (this->fd, data, size, MSG_NOSIGNAL) == (ssize_t) size;
        #else
            return send (this->fd, data, size, 0) == (ssize_t) size;
        #endif
    } else {
        return true;
    }
}

Laik_Tcp_Socket* laik_tcp_socket_accept (Laik_Tcp_Socket* this) {
    laik_tcp_always (this);
    laik_tcp_always (laik_tcp_socket_get_listening (this));

    // Accept the connection
    int fd = accept (this->fd, NULL, NULL);
    laik_tcp_always (fd >= 0);

    // Create the object
    Laik_Tcp_Socket* new = g_new0 (Laik_Tcp_Socket, 1);

    // Initialize the object
    *new = (Laik_Tcp_Socket) {
        .fd        = fd,
        .events    = 0,
        .timestamp = laik_tcp_time (),
    };

    // Return the object
    return new;
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

bool laik_tcp_socket_get_listening (const Laik_Tcp_Socket* this) {
    int       optval = 0;
    socklen_t optlen = sizeof (optval);

    int result = getsockopt (this->fd, SOL_SOCKET, SO_ACCEPTCONN, &optval, &optlen);
    laik_tcp_always (result == 0);
    (void) result;

    return optval;
}

double laik_tcp_socket_get_timestamp (const Laik_Tcp_Socket* this) {
    return this->timestamp;
}

Laik_Tcp_Socket* laik_tcp_socket_new (Laik_Tcp_SocketType type, const char* address, Laik_Tcp_Errors* errors) {
    laik_tcp_always (address);
    laik_tcp_always (errors);

    g_autoptr (Laik_Tcp_Config) config = laik_tcp_config ();

    // Create variables to store the sockaddr struct and its size
    g_autofree struct sockaddr* socket_address_data = NULL;
    socklen_t socket_address_size = 0;

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
        socket_address_data = g_memdup (addresses->ai_addr, addresses->ai_addrlen);
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
        socket_address_data = g_steal_pointer (&sockaddr_un);
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
        return NULL;
    }
    #endif

    // On TCP server sockets, set some extra options 
    if (socket_address_data->sa_family == AF_INET || socket_address_data->sa_family == AF_INET6) {
        // Enable reuse of recently freed ports
        if (setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, & (int) { 1 }, sizeof (int)) != 0) {
            laik_tcp_errors_push (errors, __func__, 4, "Failed to set SO_REUSEADDR on socket: %s", strerror (errno));
            return NULL;
        }

        // Enable TCP keep alives
        if (setsockopt (fd, SOL_SOCKET, SO_KEEPALIVE, & (int) { 1 }, sizeof (int)) != 0) {
            laik_tcp_errors_push (errors, __func__, 5, "Failed to set SO_KEEPALIVETCP_KEEPCNT on socket: %s", strerror (errno));
            return NULL;
        }

        // Disable Nagle's algorithm so messages are sent without delays
        if (setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, & (int) { 1 }, sizeof (int)) != 0) {
            laik_tcp_errors_push (errors, __func__, 6, "Failed to set TCP_NODELAY on socket: %s", strerror (errno));
            return NULL;
        }

        // Sent up to three keep alive probes before dropping the connection
        #ifdef TCP_KEEPCNT
        if (setsockopt (fd, IPPROTO_TCP, TCP_KEEPCNT, & (int) { config->socket_keepcnt }, sizeof (int)) != 0) {
            laik_tcp_errors_push (errors, __func__, 7, "Failed to set TCP_KEEPCNT on socket: %s", strerror (errno));
            return NULL;
        }
        #endif 

        // Consider the connection idle after 1 second if inactivity
        #ifdef TCP_KEEPIDLE
        if (setsockopt (fd, IPPROTO_TCP, TCP_KEEPIDLE, & (int) { config->socket_keepidle }, sizeof (int)) != 0) {
            laik_tcp_errors_push (errors, __func__, 8, "Failed to set TCP_KEEPIDLE on socket: %s", strerror (errno));
            return NULL;
        }
        #endif

        // On idle connections, send a keep alive probe every second
        #ifdef TCP_KEEPINTVL
        if (setsockopt (fd, IPPROTO_TCP, TCP_KEEPINTVL, & (int) { config->socket_keepintvl }, sizeof (int)) != 0) {
            laik_tcp_errors_push (errors, __func__, 9, "Failed to set TCP_KEEPINTVL on socket: %s", strerror (errno));
            return NULL;
        }
        #endif
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

        if (listen (fd, config->socket_backlog) != 0) {
            laik_tcp_errors_push (errors, __func__, 12, "Failed to listen on socket: %s", strerror (errno));
            close (fd);
            return NULL;
        }
    } else {
        laik_tcp_errors_push (errors, __func__, 13, "Invalid socket type %d", type);
        return NULL;
    }

    // Create the object
    Laik_Tcp_Socket* this = g_new0 (Laik_Tcp_Socket, 1);

    // Initialize the object
    *this = (Laik_Tcp_Socket) {
        .fd        = fd,
        .events    = 0,
        .timestamp = laik_tcp_time (),
    };

    // Return the object
    return this;
}

Laik_Tcp_Socket* laik_tcp_socket_poll (GPtrArray* sockets, const short events, const double seconds) {
    laik_tcp_always (sockets);

    // Check if any of the sockets in the array already has the requested event
    for (size_t i = 0; i < sockets->len; i++) {
        Laik_Tcp_Socket* current = g_ptr_array_index (sockets, i);
        if (current->events & events) {
            laik_tcp_debug ("Found a socket with the requested event already cached");
            current->events = 0;
            return current;
        }
    }

    // We have to do a poll(2) call, so create and fill a pollfd buffer
    g_autofree struct pollfd* pollfds = g_new0 (struct pollfd, sockets->len);
    for (size_t i = 0; i < sockets->len; i++) {
        const Laik_Tcp_Socket* current = g_ptr_array_index (sockets, i);
        pollfds[i] = (struct pollfd) { .fd = current->fd, .events = events, .revents = 0 };
    }

    // Call poll(2)
    int result = poll (pollfds, sockets->len, seconds * 1000);
    laik_tcp_always (result >= 0);
    (void) result;

    // Store the results of the poll(2) call back into the sockets
    for (size_t i = 0; i < sockets->len; i++) {
        laik_tcp_always (!(pollfds[i].revents & POLLNVAL));
        Laik_Tcp_Socket* current = g_ptr_array_index (sockets, i);
        current->events = pollfds[i].revents;
    }

    // Now, check again if we have a matching socket
    for (size_t i = 0; i < sockets->len; i++) {
        Laik_Tcp_Socket* current = g_ptr_array_index (sockets, i);
        if (current->events & events) {
            laik_tcp_debug ("Found a socket with the requested event after poll(2)-ing");
            current->events = 0;
            return current;
        }
    }

    // No luck, return failure
    laik_tcp_debug ("Found no socket with the requested event");
    return NULL;
}

GBytes* laik_tcp_socket_receive_bytes (Laik_Tcp_Socket* this) {
    laik_tcp_always (this);

    uint64_t size;

    if (!laik_tcp_socket_receive_uint64 (this, &size)) {
        return NULL;
    }

    g_autofree void* data = g_malloc (size);

    if (!laik_tcp_socket_receive_raw (this, data, size)) {
        return NULL;
    }

    return g_bytes_new_take (g_steal_pointer (&data), size);
}

bool laik_tcp_socket_receive_uint64 (Laik_Tcp_Socket* this, uint64_t* value) {
    laik_tcp_always (this);
    laik_tcp_always (value);

    if (!laik_tcp_socket_receive_raw (this, value, sizeof (*value))) {
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

    if (!laik_tcp_socket_send_raw (this, data, size)) {
        return false;
    }

    return true;
}

bool laik_tcp_socket_send_uint64 (Laik_Tcp_Socket* this, uint64_t value) {
    laik_tcp_always (this);

    value = GUINT64_TO_LE (value);

    return laik_tcp_socket_send_raw (this, &value, sizeof (value));
}

Laik_Tcp_Socket* laik_tcp_socket_touch (Laik_Tcp_Socket* this) {
    laik_tcp_always (this);

    this->timestamp = laik_tcp_time ();

    return this;
}
