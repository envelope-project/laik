#include "laik-internal.h"
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

#define SHM_KEY 0x1
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

int laik_shmem_panic(int err)
{
    // TODO remove method
    return err;
}

int hash(int x)
{
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

int shm_client_init()
{
    int port, shmid = -1;
    time_t t_0;
    struct shmseg *shmp;

    // As long as it fails and three seconds haven't passed try again (wait for master)
    t_0 = time(NULL);
    while (time(NULL) - t_0 < 3 && shmid == -1)
    {
        shmid = shmget(SHM_KEY, sizeof(struct shmseg), 0644 | IPC_CREAT);
    }
    if (shmid == -1)
    {
        perror("Shared memory");
        laik_shmem_panic(0);
    }

    // Attach to the segment to get a pointer to it.
    shmp = shmat(shmid, NULL, 0);
    if (shmp == (void *)-1)
    {
        perror("Shared memory attach");
        laik_shmem_panic(0);
    }

    port = shmp->port;
    shmp->size++;

    if (shmdt(shmp) == -1)
    {
        perror("shmdt");
        laik_shmem_panic(0);
    }

    return port;
}

int shm_master_init(int shmid)
{
    int size;
    struct shmseg *shmp;

    // Attach to the segment to get a pointer to it.
    shmp = shmat(shmid, NULL, 0);
    if (shmp == (void *)-1)
    {
        perror("Shared memory attach");
        laik_shmem_panic(0);
    }

    shmp->port = PORT;
    shmp->size = 1;
    // Let the client processes notify the master about their existence by incrementing size
    // Wait until no more processes join
    sleep(1);
    size = shmp->size;

    if (shmdt(shmp) == -1)
    {
        perror("shmdt");
        laik_shmem_panic(0);
    }

    if (shmctl(shmid, IPC_RMID, 0) == -1)
    {
        perror("shmctl");
        laik_shmem_panic(-3);
    }

    return size;
}

int shmem_init()
{
    int shmid = shmget(SHM_KEY, sizeof(struct shmseg), IPC_EXCL | 0644 | IPC_CREAT);
    if (shmid == -1)
    {
        int sock = 0;
        time_t t_0;
        struct sockaddr_in serv_addr;

        int port = shm_client_init();

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);

        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
            printf("\n Socket creation error \n");
            laik_shmem_panic(0);
        }

        // Convert IPv4 and IPv6 addresses from text to binary form
        if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
        {
            printf("\nInvalid address/ Address not supported \n");
            laik_shmem_panic(0);
        }

        // As long as it fails and three seconds haven't passed try again (wait for master)
        t_0 = time(NULL);
        client.socket_fd = -1;
        while (time(NULL) - t_0 < 3 && client.socket_fd < 0)
        {
            client.socket_fd = connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
        }
        if (client.socket_fd < 0)
        {
            printf("\nConnection Failed \n");
            laik_shmem_panic(0);
        }

        read(sock, &client.size, sizeof(int));
        read(sock, &client.rank, sizeof(int));

        client.didInit = true;
        master.didInit = false;
        laik_log(2, "Client%d initialization completed", client.rank);
    }
    else
    {
        master.size = shm_master_init(shmid);
        master.rank = 0;
        master.sockets = malloc(sizeof(int) * (master.size - 1));

        struct sockaddr_in address;
        int opt = 1, addrlen = sizeof(address);

        // Creating socket file descriptor
        if ((master.server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
        {
            perror("socket failed");
            exit(EXIT_FAILURE); // TOFO change theese
            laik_shmem_panic(0);
        }

        // Forcefully attaching socket to the port 8080
        if (setsockopt(master.server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
        {
            perror("setsockopt");
            exit(EXIT_FAILURE);
            laik_shmem_panic(0);
        }
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(PORT);

        if (bind(master.server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
        {
            perror("bind failed");
            exit(EXIT_FAILURE);
            laik_shmem_panic(0);
        }
        if (listen(master.server_fd, 3) < 0)
        {
            perror("listen");
            exit(EXIT_FAILURE);
            laik_shmem_panic(0);
        }

        for (int i = 0; i < master.size - 1; i++)
        {
            if ((master.sockets[i] = accept(master.server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
            {
                perror("accept");
                exit(EXIT_FAILURE);
                laik_shmem_panic(0);
            }
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
        laik_log(2, "Master initialization completed");
    }

    laik_log(2, "SHMEM backend initialized\n");
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

    int shmid = shmget(shmAddr, size + 1, 0644 | IPC_CREAT);
    // Attach to the segment to get a pointer to it.
    shmp = shmat(shmid, NULL, 0);
    if (shmp == (void *)-1)
    {
        perror("Shared memory attach");
        laik_shmem_panic(0);
    }

    memcpy((shmp + 1), buffer, size);
    shmp[0] = 'r';

    if (shmdt(shmp) == -1)
    {
        perror("shmdt");
        laik_shmem_panic(0);
    }

    return 0;
}

int shmem_recv(void *buffer, int count, int datatype, int sender)
{
    char *shmp;
    int size = datatype * count;
    int rank = client.didInit ? client.rank : 0;
    int shmAddr = hash(rank + hash(sender));

    int shmid = shmget(shmAddr, size + 1, 0644 | IPC_CREAT);
    // Attach to the segment to get a pointer to it.
    shmp = shmat(shmid, NULL, 0);
    if (shmp == (void *)-1)
    {
        perror("Shared memory attach");
        laik_shmem_panic(0);
    }

    while (shmp[0] != 'r')
    {
    }
    memcpy(buffer, (shmp + 1), size);

    if (shmdt(shmp) == -1)
    {
        perror("shmdt");
        laik_shmem_panic(0);
    }

    if (shmctl(shmid, IPC_RMID, 0) == -1)
    {
        perror("shmctl");
        laik_shmem_panic(-3);
    }

    return 0;
}

int shmem_isend()
{
    return 0;
}

int shmem_irecv()
{
    return 0;
}

int shmem_comm_split()
{
    return 0;
}

int shmem_get_count()
{
    return 0;
}

int shmem_reduce()
{
    return 0;
}

int shmem_allreduce()
{
    return 0;
}

int shmem_wait()
{
    return 0;
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