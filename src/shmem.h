#ifndef SHMEM_H
#define SHMEM_H

#define SHMEM_FAILURE -1
#define SHMEM_SUCCESS 0
#define SHMEM_SHMGET_FAILED 1
#define SHMEM_SHMAT_FAILED 2
#define SHMEM_SHMDT_FAILED 3
#define SHMEM_SHMCTL_FAILED 4
#define SHMEM_INVALID_OR_UNSUPPORTED_ADRESS 5
#define SHMEM_SOCKET_CONNECTION_FAILED 6
#define SHMEM_SOCKET_CREATION_FAILED 7
#define SHMEM_SETSOCKOPT_FAILED 8
#define SHMEM_SOCKET_BIND_FAILED 9
#define SHMEM_SOCKET_LISTEN_FAILED 10
#define SHMEM_SOCKET_ACCEPT_FAILED 11
#define SHMEM_RECV_BUFFER_TOO_SMALL 12

#define SHMEM_MAX_ERROR_STRING 100

int shmem_init();

int shmem_comm_size(int *sizePtr);

int shmem_comm_rank(int *rankPtr);

int shmem_comm_colour(int *colourPtr);

int shmem_get_identifier(int *ident);

int shmem_send(const void *buffer, int count, int datatype, int recipient);

int shmem_recv(void *buffer, int count, int datatype, int sender, int *recieved);

int shmem_error_string(int error, char *str);

int shmem_secondary_init(int primaryRank, int primarySize, int (*send)(int *, int, int),
                         int (*recv)(int *, int, int));

int shmem_calculate_groups(int *colours, int **groups, int size);

int shmem_get_colours(int *buf);

int shmem_get_secondaryRanks(int *buf);

int shmem_finalize();

#endif