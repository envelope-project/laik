#ifndef SHMEM_H
#define SHMEM_H

#define SHMEM_SUCCESS 1

int shmem_init();

int shmem_comm_size(int *sizePtr);

int shmem_comm_rank(int *rankPtr);

int shmem_send(const void* buffer, int count, int datatype, int recipient);

int shmem_recv(void* buffer, int count, int datatype, int sender);

int shmem_isend();

int shmem_irecv();

int shmem_comm_split();

int shmem_get_count();

int shmem_reduce();

int shmem_allreduce();

int shmem_wait();

int shmem_finalize();
#endif