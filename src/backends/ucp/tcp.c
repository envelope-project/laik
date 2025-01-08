//*********************************************************************************
#include "tcp.h"

#include <errno.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <unistd.h>
#include <string.h>

//*********************************************************************************
// forward declaration
bool check_local(char *host);

//*********************************************************************************
static inline void send_instance_data(InstData *d, int fd, int lid)
{
    write(fd, &lid, sizeof(int));
    write(fd, &d->world_size, sizeof(int));
    write(fd, &d->phase, sizeof(int));
    write(fd, &d->epoch, sizeof(int));

    for (int k = 0; k < d->world_size; k++)
    {
        write(fd, &(d->peer[k].addrlen), sizeof(size_t));
        write(fd, d->peer[k].address, d->peer[k].addrlen);
    }
}

//*********************************************************************************
static inline void receive_instance_data(InstData *d, int fd)
{
    read(fd, &(d->mylid), sizeof(int));
    read(fd, &(d->world_size), sizeof(int));
    read(fd, &d->phase, sizeof(int));
    read(fd, &d->epoch, sizeof(int));

    d->peer = (Peer *)malloc(d->world_size * sizeof(Peer));
    if (d->peer == NULL)
    {
        laik_panic("Could not malloc heap for non master\n");
    }

    for (int i = 0; i < d->world_size; i++)
    {
        read(fd, &d->peer[i].addrlen, sizeof(d->peer[i].addrlen));
        d->peer[i].address = (ucp_address_t *)malloc(d->peer[i].addrlen);
        if (d->peer[i].address == NULL)
        {
            laik_panic("Could not allocate heap for peer address\n");
        }
        read(fd, d->peer[i].address, d->peer[i].addrlen);
    }
}

//*********************************************************************************
void initialize_setup_connection(char *home_host, const int home_port, InstData *d)
{ //
    // create listening socket and determine who is master
    //

    // create socket to listen for incoming TCP connections
    //  if <home_host> is not set, try to aquire local port <home_port>
    // we may need to try creating the listening socket twice
    struct sockaddr_in sin;
    socket_fd = -1;
    // if home host is localhost, try to become master
    bool try_master = check_local(home_host);
    struct addrinfo sock_hints = {0}, *res;
    // get address of home node
    sock_hints.ai_family = AF_INET;
    sock_hints.ai_socktype = SOCK_STREAM;
    getaddrinfo(home_host, HOME_PORT_STR, &sock_hints, &res);

    socket_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        laik_panic("UCP cannot create listening socket");
    }

    if (try_master)
    {
        // mainly for development: avoid wait time to bind to same port
        if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR,
                       &(int){1}, sizeof(int)) < 0)
        {
            laik_panic("UCP cannot set SO_REUSEADDR");
        }

        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = htonl(INADDR_ANY);
        sin.sin_port = htons(home_port);
        // bind() is a race condition, listening after the bind will only work on one process though
        if (bind(socket_fd, (struct sockaddr *)&sin, sizeof(sin)) == 0)
        {
            // listen on successfully bound socket
            // if this fails, another process started listening first
            // and we need to open another socket, as we cannot unbind
            /// TODO: how many processes should be queued?
            if (listen(socket_fd, 5) < 0)
            {
                close(socket_fd);
                laik_log(1, "Another process is already master, opening new socket\n");
            }
            else
            {
                // we successfully became master: my LID is 0
                d->mylid = 0;
            }
        }
    }

    // now we know if we are master: init peer with id 0
    bool is_master = (d->mylid == 0);

    if (is_master)
    {
        laik_log(1, "I am master!\n");

        // add LID tag to my location
        // copy my data also to d->peer[0]
        d->peer = (Peer *)calloc(d->world_size, sizeof(Peer));
        if (d->peer == NULL)
        {
            laik_panic("Could not malloc heap for peers\n");
        }

        d->peer[0].address = d->address;
        d->peer[0].addrlen = d->addrlen;

        fds = (int *)calloc(d->world_size, sizeof(int));
        if (fds == NULL)
        {
            laik_panic("Could not malloc heap for fds\n");
        }
        for (int i = 1; i < d->world_size; i++)
        {
            fds[i] = accept(socket_fd, NULL, NULL);
            laik_log(1, "%d out of %d is connecting...\n", i, d->world_size - 1);
            if (fds[i] < 0)
                laik_log(LAIK_LL_Panic, "Failed to accept connection: %s\n",
                         strerror(errno));
                         
            // the length of the ucx worker addresses does not have to be the same across the nodes
            laik_log(1, "Master accepted initial Rank [%d]\n", i);
            read(fds[i], &d->peer[i].addrlen, sizeof(d->peer[i].addrlen));

            d->peer[i].address = (ucp_address_t *)malloc(d->peer[i].addrlen);
            if (d->peer[i].address == NULL)
            {
                laik_panic("Could not allocate heap for peer address\n");
            }
            read(fds[i], d->peer[i].address, d->peer[i].addrlen);
        }
        // send assigned number and address list to every non-master node
        for (int i = 1; i < d->world_size; i++)
        {
            send_instance_data(d, fds[i], i);
        }
    }
    else
    {
        socket_fd = socket(PF_INET, SOCK_STREAM, 0);
        if (socket_fd < 0)
        {
            laik_panic("UCP cannot create listening socket");
        }

        if (connect(socket_fd, res->ai_addr, res->ai_addrlen) != 0)
        {
            laik_log(LAIK_LL_Error, "Could not connect to socket: %s\n", strerror(errno));
            exit(1);
        }

        write(socket_fd, &d->addrlen, sizeof(size_t));
        write(socket_fd, d->address, d->addrlen);

        receive_instance_data(d, socket_fd);

        if (d->mylid < 0)
        {
            laik_log(LAIK_LL_Error, "In non master happened something bad id: %d world size %d phase %d and epoch %d\n Last state of errno %s",
                     d->mylid, d->world_size, d->phase, d->epoch, strerror(errno));
        }
    }
}