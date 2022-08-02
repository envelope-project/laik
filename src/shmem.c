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
#include <signal.h>

#include <errno.h>

#define SHM_KEY 0x33 // TODO über SHMEM_RUN dynamisch key generieren (prüfen ob besetzt und dann neuen erzeugen) und key dann als Umgebungsvariable verteilen
#define PORT 8080
#define MAX_WAITTIME 8

struct shmClient
{
    bool didInit;
    int socket_fd;
};

struct shmMaster
{
    bool didInit;
    int server_fd;
    int *sockets;
};

struct groupInfo
{
    int size;
    int rank;
};

struct groupInfo groupInfo;
struct shmClient client;
struct shmMaster master;

struct shmSegList
{
    int shmid;
    struct shmSegList *next;
};
struct shmSegList *current;
struct shmSegList *head;

int addShmSeg(int shmid)
{
    struct shmSegList *new = malloc(sizeof(struct shmSegList));
    new->shmid = shmid;

    if (head == NULL)
    {
        head = new;
        current = new;
    }
    else
    {
        current->next = new;
        current = new;
    }

    return SHMEM_SUCCESS;
}

void deleteShmSegs()
{
    struct shmSegList *elem = head;

    while (elem != NULL)
    {
        shmctl(elem->shmid, IPC_RMID, 0);
        elem = elem->next;
    }
}

int shmem_init()
{
    signal(SIGINT, deleteShmSegs);

    int *shmp;
    int shmid = shmget(SHM_KEY, sizeof(int), IPC_EXCL | 0644 | IPC_CREAT);
    if (shmid == -1)
    {
        // Client initialization
        int port, sock = 0;
        time_t t_0;
        struct sockaddr_in serv_addr;

        // As long as it fails and three seconds haven't passed try again (wait for master)
        t_0 = time(NULL);
        while (time(NULL) - t_0 < MAX_WAITTIME && shmid == -1)
        {
            shmid = shmget(SHM_KEY, 0, IPC_CREAT | 0644);
        }
        if (shmid == -1)
            return SHMEM_SHMGET_FAILED;

        // Attach to the segment to get a pointer to it.
        shmp = shmat(shmid, NULL, 0);
        if (shmp == (void *)-1)
            return SHMEM_SOCKET_ACCEPT_FAILED;

        port = *shmp;

        if (shmdt(shmp) == -1)
            return SHMEM_SHMDT_FAILED;

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);

        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
            return SHMEM_SOCKET_CREATION_FAILED;

        // Convert IPv4 and IPv6 addresses from text to binary form
        if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
            return SHMEM_INVALID_OR_UNSUPPORTED_ADRESS;

        // As long as it fails and MAX_WAITTIME haven't passed try again (wait for master)
        t_0 = time(NULL);
        client.socket_fd = -1;
        while (time(NULL) - t_0 < MAX_WAITTIME && client.socket_fd < 0)
        {
            client.socket_fd = connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
        }
        if (client.socket_fd < 0)
            return SHMEM_SOCKET_CONNECTION_FAILED;

        read(sock, &groupInfo.size, sizeof(int));
        read(sock, &groupInfo.rank, sizeof(int));

        client.didInit = true;
        master.didInit = false;
    }
    else
    {
        // Master initialization
        addShmSeg(shmid);

        // Attach to the segment to get a pointer to it.
        shmp = shmat(shmid, NULL, 0);
        if (shmp == (void *)-1)
            return SHMEM_SHMAT_FAILED;

        *shmp = PORT;

        if (shmdt(shmp) == -1)
            return SHMEM_SHMDT_FAILED;

        char *str = getenv("LAIK_SIZE");
        groupInfo.size = str ? atoi(str) : 1;
        groupInfo.rank = 0;
        master.sockets = malloc(sizeof(int) * (groupInfo.size - 1));

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

        for (int i = 0; i < groupInfo.size - 1; i++)
        {
            if ((master.sockets[i] = accept(master.server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
                return SHMEM_SOCKET_ACCEPT_FAILED;
        }

        // Assign processes their ranks and tell them the group size.
        for (int i = 0; i < groupInfo.size - 1; i++)
        {
            int rank = i + 1;
            send(master.sockets[i], &groupInfo.size, sizeof(int), 0);
            send(master.sockets[i], &rank, sizeof(int), 0);
        }

        // TODO: Memory fence for this operation
        if (shmctl(shmid, IPC_RMID, 0) == -1)
            return SHMEM_SHMCTL_FAILED;

        master.didInit = true;
        client.didInit = false;
    }
    return SHMEM_SUCCESS;
}

int shmem_comm_size(int *sizePtr)
{
    *sizePtr = groupInfo.size;
    return SHMEM_SUCCESS;
}

int shmem_set_comm_size(int size){
    groupInfo.size = size;
    return SHMEM_SUCCESS;
}

int shmem_comm_rank(int *rankPtr)
{
    *rankPtr = groupInfo.rank;
    return SHMEM_SUCCESS;
}

int shmem_set_comm_rank(int rank){
    groupInfo.rank = rank;
    return SHMEM_SUCCESS;
}

int shmem_get_identifier(int *ident){
    *ident = 1;
    return SHMEM_SUCCESS;
}

int hash(int x)
{
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

int shmem_send(const void *buffer, int count, int datatype, int recipient)
{
    char *shmp;
    int size = datatype * count;
    int shmAddr = hash(recipient + hash(groupInfo.rank));

    int shmid = shmget(shmAddr, size + 1 + sizeof(int), 0644 | IPC_CREAT);
    addShmSeg(shmid);
    if (shmid == -1)
    {
        // TODO eventuell rauskürzen da nicht gebraucht
        shmid = shmget(shmAddr, 0, 0644 | IPC_CREAT);
        if (shmid == -1)
            return SHMEM_SHMGET_FAILED;

        if (shmctl(shmid, IPC_RMID, 0) == -1)
            return SHMEM_SHMCTL_FAILED;

        shmid = shmget(shmAddr, size + 1 + sizeof(int), 0644 | IPC_CREAT);
        if (shmid == -1)
            return SHMEM_SHMGET_FAILED;
    }

    // Attach to the segment to get a pointer to it.
    shmp = shmat(shmid, NULL, 0);
    if (shmp == (void *)-1)
        return SHMEM_SHMAT_FAILED;

    shmp[0] = 'd';
    memcpy((shmp + 1 + sizeof(int)), buffer, size);
    memcpy(shmp + 1, &count, sizeof(int));
    shmp[0] = 'r';
    while(shmp[0] != 'd'){
    }

    if (shmdt(shmp) == -1)
        return SHMEM_SHMDT_FAILED;

    shmctl(shmid, IPC_RMID, 0);

    return SHMEM_SUCCESS;
}

int shmem_recv(void *buffer, int count, int datatype, int sender, int *received)
{
    char *shmp;
    int bufSize = datatype * count;
    int shmAddr = hash(groupInfo.rank + hash(sender));

    time_t t_0 = time(NULL);
    int shmid = shmget(shmAddr, 0, 0644);
    while (shmid == -1 && time(NULL) - t_0 < MAX_WAITTIME)
        shmid = shmget(shmAddr, 0, 0644);
    if (shmid == -1)
        return SHMEM_SHMGET_FAILED;

    // Attach to the segment to get a pointer to it.
    shmp = shmat(shmid, NULL, 0);
    if (shmp == (void *)-1)
        return SHMEM_SHMAT_FAILED;

    while (shmp[0] != 'r')
    {
    }
    memcpy(received, shmp + 1, sizeof(int));
    int receivedSize = *received * datatype;
    if (bufSize < receivedSize)
    {
        memcpy(buffer, (shmp + 1 + sizeof(int)), bufSize);
        return SHMEM_RECV_BUFFER_TOO_SMALL;
    }
    else
    {
        memcpy(buffer, (shmp + 1 + sizeof(int)), receivedSize);
    }
    shmp[0] = 'd';
    
    if (shmdt(shmp) == -1)
        return SHMEM_SHMDT_FAILED;

    shmctl(shmid, IPC_RMID, 0);
    
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
    case SHMEM_RECV_BUFFER_TOO_SMALL:
        strcpy(str, "recv was given a too small buffer");
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
        for (int i = 0; i < groupInfo.size - 1; i++)
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

    if (size >= 2)
    {
        int bufSize = 10, err, received;
        int msg1[] = {0,1,2,3,4,5,6,7,8,9};
        int msg2[] = {9,8,7,6,5,4,3,2,1,0};
        char errMsg[SHMEM_MAX_ERROR_STRING];
        int buf[] = {0,0,0,0,0,0,0,0,0,0};
        switch (rank)
        {
        case 0:
            err = shmem_send(msg1, 10, sizeof(int), 1);
            if (err != SHMEM_SUCCESS)
            {
                shmem_error_string(err, errMsg);
                printf("%s\n", errMsg);
                return SHMEM_FAILURE;
            }
            err = shmem_send(msg2, 10, sizeof(int), 1);
            if (err != SHMEM_SUCCESS)
            {
                shmem_error_string(err, errMsg);
                printf("%s\n", errMsg);
                return SHMEM_FAILURE;
            }
            printf("Master sent 2 arrays to process 1 ({0,1,2,3,4,5,6,7,8,9} and {9,8,7,6,5,4,3,2,1,0})\n");
            break;
        case 1:
            err = shmem_recv(buf, bufSize, sizeof(int), 0, &received);
            if (err != SHMEM_SUCCESS)
            {
                shmem_error_string(err, errMsg);
                printf("%s\n", errMsg);
                return SHMEM_FAILURE;
            }
            printf("Process 1 received {%d,%d,%d,%d,%d,%d,%d,%d,%d,%d}", buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7],buf[8],buf[9]);
            err = shmem_recv(buf, bufSize, sizeof(int), 0, &received);
            if (err != SHMEM_SUCCESS)
            {
                shmem_error_string(err, errMsg);
                printf("%s\n", errMsg);
                return SHMEM_FAILURE;
            }
            printf(" and {%d,%d,%d,%d,%d,%d,%d,%d,%d,%d}\n", buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7],buf[8],buf[9]);
            break;
        }
    }

    shmem_finalize();
    return SHMEM_SUCCESS;
}