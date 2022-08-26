/*
 * This file is part of the LAIK library.
 * Copyright (c) 2017, 2018 Josef Weidendorfer <Josef.Weidendorfer@gmx.de>
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

#include "shmem.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/shm.h>
#include <time.h>
#include <signal.h>
#include <stdatomic.h>
#include <laik.h>
#include <unistd.h>

#define SHM_KEY 0x33 // TODO über SHMEM_RUN dynamisch key generieren (prüfen ob besetzt und dann neuen erzeugen) und key dann als Umgebungsvariable verteilen
#define MAX_WAITTIME 8


struct shmInitSeg
{
    atomic_int rank;
    int colour;
    bool didInit;
};

struct groupInfo
{
    int size;
    int rank;
    int colour;
    int *colours;
    int *secondaryRanks;
};

struct shmList
{
    void *ptr;
    int shmid;
    struct shmList *next;
};

struct shmList *head;
struct shmList *tail;

struct groupInfo groupInfo;
int openShmid = -1;

void deleteOpenShmSeg()
{
    if (openShmid != -1)
        shmctl(openShmid, IPC_RMID, 0);

    struct shmList *l = head;
    while(l != NULL){
        shmdt(l->ptr);
        shmctl(l->shmid, IPC_RMID, 0);
        l = l->next;
    }
}

int shmem_init()
{
    signal(SIGINT, deleteOpenShmSeg);

    char *str = getenv("LAIK_SIZE");
    groupInfo.size = str ? atoi(str) : 1;

    struct shmInitSeg *shmp;
    int shmid = shmget(SHM_KEY, sizeof(struct shmInitSeg), IPC_EXCL | 0644 | IPC_CREAT);
    if (shmid == -1)
    {
        // Client initialization
        time_t t_0;

        // As long as it fails and three seconds haven't passed try again (wait for master)
        t_0 = time(NULL);
        while (time(NULL) - t_0 < MAX_WAITTIME && shmid == -1)
        {
            shmid = shmget(SHM_KEY, 0, IPC_CREAT | 0644); // TODO maybe remove IPC_Creat might not be needed
        }
        if (shmid == -1)
            return SHMEM_SHMGET_FAILED;

        // Attach to the segment to get a pointer to it.
        shmp = shmat(shmid, NULL, 0);
        if (shmp == (void *)-1)
            return SHMEM_SHMAT_FAILED;

        groupInfo.rank = ++shmp->rank;
        if (shmdt(shmp) == -1)
            return SHMEM_SHMDT_FAILED;
    }
    else
    {
        // Master initialization
        groupInfo.rank = 0;

        openShmid = shmid;
        shmp = shmat(shmid, NULL, 0);
        if (shmp == (void *)-1)
            return SHMEM_SHMAT_FAILED;

        while (shmp->rank != groupInfo.size - 1)
        {
        }

        if (shmdt(shmp) == -1)
            return SHMEM_SHMDT_FAILED;

        if (shmctl(shmid, IPC_RMID, 0) == -1)
            return SHMEM_SHMCTL_FAILED;

        openShmid = -1;
    }
    return SHMEM_SUCCESS;
}

int shmem_comm_size(int *sizePtr)
{
    *sizePtr = groupInfo.size;
    return SHMEM_SUCCESS;
}

int shmem_comm_rank(int *rankPtr)
{
    *rankPtr = groupInfo.rank;
    return SHMEM_SUCCESS;
}

int shmem_comm_colour(int *colourPtr)
{
    *colourPtr = groupInfo.colour;
    return SHMEM_SUCCESS;
}

int shmem_get_identifier(int *ident)
{
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
    int shmAddr = hash(recipient + hash(groupInfo.rank)) + 1000;

    int shmid = shmget(shmAddr, size + 1 + sizeof(int), 0644 | IPC_CREAT);
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
    openShmid = shmid;

    // Attach to the segment to get a pointer to it.
    shmp = shmat(shmid, NULL, 0);
    if (shmp == (void *)-1)
        return SHMEM_SHMAT_FAILED;

    shmp[0] = 'd';
    memcpy((shmp + 1 + sizeof(int)), buffer, size);
    memcpy(shmp + 1, &count, sizeof(int));
    shmp[0] = 'r';
    while (shmp[0] != 'd')
    {
    }

    if (shmdt(shmp) == -1)
        return SHMEM_SHMDT_FAILED;

    shmctl(shmid, IPC_RMID, 0);
    openShmid = -1;

    return SHMEM_SUCCESS;
}

int shmem_recv(void *buffer, int count, int datatype, int sender, int *received)
{
    char *shmp;
    int bufSize = datatype * count;
    int shmAddr = hash(groupInfo.rank + hash(sender)) + 1000;

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
    deleteOpenShmSeg();

    if (groupInfo.colours != NULL)
        free(groupInfo.colours);
    
    if (groupInfo.secondaryRanks != NULL)
        free(groupInfo.secondaryRanks);

    return SHMEM_SUCCESS;
}

int shmem_secondary_init(int primaryRank, int primarySize, int (*send)(int *, int, int),
                         int (*recv)(int *, int, int))
{
    signal(SIGINT, deleteOpenShmSeg);

    struct shmInitSeg *shmp;
    int shmid = shmget(SHM_KEY, sizeof(struct shmInitSeg), IPC_EXCL | 0644 | IPC_CREAT);
    if (shmid == -1)
    {
        // Client initialization
        time_t t_0;

        // As long as it fails and three seconds haven't passed try again (wait for master)
        t_0 = time(NULL);
        while (time(NULL) - t_0 < MAX_WAITTIME && shmid == -1)
        {
            shmid = shmget(SHM_KEY, 0, IPC_CREAT | 0644); // TODO maybe remove IPC_Creat might not be needed
        }
        if (shmid == -1)
            return SHMEM_SHMGET_FAILED;

        // Attach to the segment to get a pointer to it.
        shmp = shmat(shmid, NULL, 0);
        if (shmp == (void *)-1)
            return SHMEM_SHMAT_FAILED;

        while (!shmp->didInit)
        {
        }
        groupInfo.rank = ++shmp->rank;
        groupInfo.colour = shmp->colour;

        if (shmdt(shmp) == -1)
            return SHMEM_SHMDT_FAILED;
    }
    else
    {
        // Master initialization
        openShmid = shmid;

        groupInfo.rank = 0;
        groupInfo.colour = primaryRank;

        shmp = shmat(shmid, NULL, 0);
        if (shmp == (void *)-1)
            return SHMEM_SHMAT_FAILED;

        shmp->colour = primaryRank;
        shmp->didInit = true;

        if (shmdt(shmp) == -1)
            return SHMEM_SHMDT_FAILED;
    }

    // Get the colours of each process at master, calculate the groups and send each process their group.
    if (primaryRank == 0)
    {
        groupInfo.colours = malloc(primarySize * sizeof(int));
        groupInfo.colours[0] = groupInfo.colour;
        groupInfo.secondaryRanks = malloc(primarySize * sizeof(int));
        groupInfo.secondaryRanks[0] = groupInfo.rank;

        for (int i = 1; i < primarySize; i++)
        {
            (*recv)(&groupInfo.colours[i], 1, i);
            (*recv)(&groupInfo.secondaryRanks[i], 1, i);
        }

        int groupSizes[primarySize];
        memset(groupSizes, 0, primarySize * sizeof(int));
        for (int i = 0; i < primarySize; i++)
        {
            groupSizes[groupInfo.colours[i]]++;
        }

        for (int i = 1; i < primarySize; i++)
        {
            (*send)(&groupSizes[groupInfo.colours[i]], 1, i);
            (*send)(groupInfo.colours, primarySize, i);
            (*send)(groupInfo.secondaryRanks, primarySize, i);
        }
        groupInfo.size = groupSizes[groupInfo.colours[0]];
    }
    else
    {
        (*send)(&groupInfo.colour, 1, 0);
        (*send)(&groupInfo.rank, 1, 0);

        (*recv)(&groupInfo.size, 1, 0);
        groupInfo.colours = malloc(primarySize * sizeof(int));
        (*recv)(groupInfo.colours, groupInfo.size, 0);
        groupInfo.secondaryRanks = malloc(primarySize * sizeof(int));
        (*recv)(groupInfo.secondaryRanks, groupInfo.size, 0);
    }

    if (groupInfo.rank == 0 && shmctl(openShmid, IPC_RMID, 0) == -1)
        return SHMEM_SHMCTL_FAILED;
    openShmid = -1;

    return SHMEM_SUCCESS;
}

int shmem_get_colours(int *buf)
{
    memcpy(buf, groupInfo.colours, groupInfo.size * sizeof(int));
    return SHMEM_SUCCESS;
}

int shmem_get_secondaryRanks(int *buf)
{
    memcpy(buf, groupInfo.secondaryRanks, groupInfo.size * sizeof(int));
    return SHMEM_SUCCESS;
}

void register_shmSeg(void *ptr, int shmid)
{
    struct shmList *new = malloc(sizeof(struct shmList));
    new->ptr = ptr;
    new->shmid = shmid;

    if (head == NULL)
    {
        head = new;
        tail = new;
        return;
    }
    head->next = new;
    head = new;
}

int get_shmid(void *ptr, int *shmid)
{
    if(tail == NULL)
        return SHMEM_FAILURE;

    struct shmList *previous = NULL;
    struct shmList *current = tail;

    while(current != NULL)
    {
        if(ptr == current->ptr)
        {
            *shmid = current->shmid;
            if(previous == NULL)
            {
                tail = current->next;
            }
            else
            {
                previous->next = current->next;
            }

            if(current->next == NULL){
                head = previous;
            }

            return SHMEM_SUCCESS;
        }
        previous = current;
        current = current->next;
    }
    return SHMEM_FAILURE;
}

static int cnt = 0;

void* def_shmem_malloc(Laik_Data* d, size_t size){
    (void) d; // not used in this implementation of interface

    int shmAddr = hash(groupInfo.rank + hash(cnt++));
    int shmid = shmget(shmAddr, size, 0644 | IPC_CREAT | IPC_EXCL);
    if (shmid == -1)
    {
        laik_panic("def_shmem_malloc couldn't create the shared memory segment");
        return NULL;
    }

    // Attach to the segment to get a pointer to it.
    void *ptr = shmat(shmid, NULL, 0);
    if (ptr == (void *)-1)
    {
        laik_panic("def_shmem_malloc couldn't attach to the shared memory segment");
        return NULL;
    }

    register_shmSeg(ptr, shmid);

    return ptr;
}

void def_shmem_free(Laik_Data* d, void* ptr){
    (void) d; // not used in this implementation of interface

    int shmid;
    if(get_shmid(ptr, &shmid) == SHMEM_FAILURE)
        laik_panic("def_shmem_free couldn't find the given shared memory segment");

    if (shmdt(ptr) == -1)
        laik_panic("def_shmem_free couldn't detach from the given pointer");

    if (shmctl(shmid, IPC_RMID, 0) == -1)
        laik_panic("def_shmem_free couldn't destroy the shared memory segment");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void) argv;

    int a = 1, b = 2, c = 3;
    int *x = &a;
    int *y = &b;
    int *z = &c;

    register_shmSeg(x, a);
    register_shmSeg(y, b);
    register_shmSeg(z, c);

    int var = -1;
    get_shmid(y, &var);
    assert(var == 2);
    get_shmid(z, &var);
    assert(var == 3);
    get_shmid(x, &var);
    assert(var == 1);

    assert(tail == NULL);
    assert(head == NULL);

    return SHMEM_SUCCESS;
}