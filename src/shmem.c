#include "shmem.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>

#define SHM_KEY 0x12
#define PORT 8080
#define BUFSIZE 1024

struct shmseg
{
    int port;
    int size;
};

struct shmClient
{
    bool didInit;
    int size;
    int rank;
    int socket_fd;
};

struct shmMaster
{
    bool didInit;
    int size;
    int rank;
    int server_fd;
    int *sockets;
};

struct shmClient client;
struct shmMaster master;

int hash(int x)
{
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

int shmem_init()
{
    int shmid = shmget(SHM_KEY, sizeof(struct shmseg), IPC_EXCL | 0644 | IPC_CREAT);
    if (shmid == -1)
    {
        int port, shmid = -1, sock = 0;
        time_t t_0;
        struct sockaddr_in serv_addr;
        struct shmseg *shmp;

        // As long as it fails and three seconds haven't passed try again (wait for master)
        t_0 = time(NULL);
        while (time(NULL) - t_0 < 3 && shmid == -1)
        {
            shmid = shmget(SHM_KEY, sizeof(struct shmseg), 0644 | IPC_CREAT);
        }
        if (shmid == -1)
            return SHMEM_SHMGET_FAILED;

        // Attach to the segment to get a pointer to it.
        shmp = shmat(shmid, NULL, 0);
        if (shmp == (void *)-1)
            return SHMEM_SOCKET_ACCEPT_FAILED;

        port = shmp->port;
        shmp->size++;

        if (shmdt(shmp) == -1)
            return SHMEM_SHMDT_FAILED;

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);

        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
            return SHMEM_SOCKET_CREATION_FAILED;

        // Convert IPv4 and IPv6 addresses from text to binary form
        if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
            return SHMEM_INVALID_OR_UNSUPPORTED_ADRESS;

        // As long as it fails and three seconds haven't passed try again (wait for master)
        t_0 = time(NULL);
        client.socket_fd = -1;
        while (time(NULL) - t_0 < 3 && client.socket_fd < 0)
        {
            client.socket_fd = connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
        }
        if (client.socket_fd < 0)
            return SHMEM_SOCKET_CONNECTION_FAILED;

        read(sock, &client.size, sizeof(int));
        read(sock, &client.rank, sizeof(int));

        client.didInit = true;
        master.didInit = false;
    }
    else
    {
        struct shmseg *shmp;

        // Attach to the segment to get a pointer to it.
        shmp = shmat(shmid, NULL, 0);
        if (shmp == (void *)-1)
            return SHMEM_SHMAT_FAILED;

        shmp->port = PORT;
        shmp->size = 1;
        // Let the client processes notify the master about their existence by incrementing size
        // Wait until no more processes join
        sleep(1);
        master.size = shmp->size;
        master.rank = 0;

        if (shmdt(shmp) == -1)
            return SHMEM_SHMDT_FAILED;

        if (shmctl(shmid, IPC_RMID, 0) == -1)
            return SHMEM_SHMCTL_FAILED;

        master.sockets = malloc(sizeof(int) * (master.size - 1));

        struct sockaddr_in address;
        int opt = 1, addrlen = sizeof(address);

        // Creating socket file descriptor
        if ((master.server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
            return SHMEM_SOCKET_CREATION_FAILED;

        // Forcefully attaching socket to the port 8080
        if (setsockopt(master.server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
            return SHMEM_SETSOCKOPT_FAILED;

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(PORT);

        if (bind(master.server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
            return SHMEM_SOCKET_BIND_FAILED;

        if (listen(master.server_fd, 3) < 0)
            return SHMEM_SOCKET_LISTEN_FAILED;

        for (int i = 0; i < master.size - 1; i++)
        {
            if ((master.sockets[i] = accept(master.server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
                return SHMEM_SOCKET_ACCEPT_FAILED;
        }

        // Assign processes their ranks and tell them the group size.
        for (int i = 0; i < master.size - 1; i++)
        {
            int rank = i + 1;
            send(master.sockets[i], &master.size, sizeof(int), 0);
            send(master.sockets[i], &rank, sizeof(int), 0);
        }

        master.didInit = true;
        client.didInit = false;
    }

    return SHMEM_SUCCESS;
}

int shmem_comm_size(int *sizePtr)
{
    if (client.didInit)
    {
        *sizePtr = client.size;
    }
    else
    {
        *sizePtr = master.size;
    }
    return SHMEM_SUCCESS;
}

int shmem_comm_rank(int *rankPtr)
{
    if (client.didInit)
    {
        *rankPtr = client.rank;
    }
    else
    {
        *rankPtr = 0;
    }
    return SHMEM_SUCCESS;
}

int shmem_send(const void *buffer, int count, int datatype, int recipient)
{
    char *shmp;
    int size = datatype * count;
    int rank = client.didInit ? client.rank : 0;
    int shmAddr = hash(recipient + hash(rank));

    int shmid = shmget(shmAddr, size + 1 + sizeof(int), 0644 | IPC_CREAT);
    // Attach to the segment to get a pointer to it.
    shmp = shmat(shmid, NULL, 0);
    if (shmp == (void *)-1)
        return SHMEM_SHMAT_FAILED;

    memcpy((shmp + 1 + sizeof(int)), buffer, size);
    shmp[0] = 'r';
    shmp[1] = count;

    if (shmdt(shmp) == -1)
        return SHMEM_SHMDT_FAILED;

    return SHMEM_SUCCESS;
}

int shmem_recv(void *buffer, int count, int datatype, int sender, int *received)
{
    char *shmp;
    int size = datatype * count;
    int rank = client.didInit ? client.rank : 0;
    int shmAddr = hash(rank + hash(sender));

    int shmid = shmget(shmAddr, size + 1, 0644 | IPC_CREAT);
    // Attach to the segment to get a pointer to it.
    shmp = shmat(shmid, NULL, 0);
    if (shmp == (void *)-1)
        return SHMEM_SHMAT_FAILED;

    while (shmp[0] != 'r')
    {
    }
    memcpy(buffer, (shmp + 1 + sizeof(int)), size);
    *received = shmp[1]; // TODO memcpy only the needed amount of data (first read received then only memcopy the received)

    if (shmdt(shmp) == -1)
        return SHMEM_SHMDT_FAILED;

    if (shmctl(shmid, IPC_RMID, 0) == -1)
        return SHMEM_SHMCTL_FAILED;

    return SHMEM_SUCCESS;
}

int shmem_comm_split()
{
    return SHMEM_SUCCESS;
}

int shmem_error_string(int error, char *str)
{
    switch (error)
    {
    case SHMEM_SUCCESS:
        strcpy(str, "not an error: shmem success");
        break;
    case SHMEM_SHMGET_FAILED:
        strcpy(str, "shmget failed");
        break;
    case SHMEM_SHMAT_FAILED:
        strcpy(str, "shmat failed");
        break;
    case SHMEM_SHMDT_FAILED:
        strcpy(str, "shmdt failed");
        break;
    case SHMEM_SHMCTL_FAILED:
        strcpy(str, "shmctl failed");
        break;
    case SHMEM_INVALID_OR_UNSUPPORTED_ADRESS:
        strcpy(str, "invalid or unsupported adress");
        break;
    case SHMEM_SOCKET_CONNECTION_FAILED:
        strcpy(str, "socket connection failed");
        break;
    case SHMEM_SOCKET_CREATION_FAILED:
        strcpy(str, "socked creation failed");
        break;
    case SHMEM_SETSOCKOPT_FAILED:
        strcpy(str, "setsockopt failed");
        break;
    case SHMEM_SOCKET_BIND_FAILED:
        strcpy(str, "socket bind failed");
        break;
    case SHMEM_SOCKET_LISTEN_FAILED:
        strcpy(str, "socket listen failed");
        break;
    case SHMEM_SOCKET_ACCEPT_FAILED:
        strcpy(str, "socket accept failed");
        break;
    default:
        strcpy(str, "error unknown to shmem");
        return SHMEM_FAILURE;
    }
    return SHMEM_SUCCESS;
}

int shmem_finalize()
{
    if (master.didInit)
    {
        // closing the connected sockets and shared memories
        for (int i = 0; i < master.size - 1; i++)
        {
            close(master.sockets[i]);
        }

        // closing the listening socket TODO: Maybe move to finalize
        shutdown(master.server_fd, SHUT_RDWR);

        free(master.sockets);
    }
    else
    {
        close(client.socket_fd);
    }
    return SHMEM_SUCCESS;
}

int main()
{
    shmem_init();

    int rank, size;
    shmem_comm_rank(&rank);
    shmem_comm_size(&size);
    printf("Hello from process %d of %d\n", rank, size);

    shmem_finalize();
    return SHMEM_SUCCESS;
}