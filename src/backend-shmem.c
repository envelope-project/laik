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

#ifdef USE_SHMEM

#include "laik-internal.h"
#include "laik-backend-shmem.h"

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

#define SHM_KEY 0x1234567
#define PORT 8080

// forward decls, types/structs , global variables

static void laik_shmem_finalize(Laik_Instance *as);
static void laik_shmem_prepare(Laik_ActionSeq *as);
static void laik_shmem_cleanup(Laik_ActionSeq *as);
static void laik_shmem_exec(Laik_ActionSeq *as);
static void laik_shmem_updateGroup(Laik_Group *as);
static bool laik_shmem_log_action(Laik_Action *as);
static void laik_shmem_sync(Laik_KVStore *kvs);

// C guarantees that unset function pointers are NULL
static Laik_Backend laik_backend_shmem = {
    .name = "SHMEM",
    .finalize = laik_shmem_finalize,
    .prepare = laik_shmem_prepare,
    .cleanup = laik_shmem_cleanup,
    .exec = laik_shmem_exec,
    .updateGroup = laik_shmem_updateGroup,
    .log_action = laik_shmem_log_action,
    .sync = laik_shmem_sync};

static Laik_Instance *shmem_instance = 0;

int shmid = -1;
int rank = -1;
int size = -1;

struct shmseg
{
    int port;
    int size;
};

//----------------------------------------------------------------------------
// error helpers

static void laik_shmem_panic(int err)
{
    if (err != -3 && shmctl(shmid, IPC_RMID, 0) == -1)
    {
        perror("shmctl");
        laik_shmem_panic(-3);
    }
    laik_log(LAIK_LL_Panic, "SHMEM backend: error '%d'", err);
    exit(1);
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
    int size, old_size;
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

//----------------------------------------------------------------------------
// backend interface implementation: initialization

Laik_Instance *laik_init_shmem(int *argc, char ***argv)
{
    argc = argc;
    argv = argv;
    if (shmem_instance)
        return shmem_instance;

    shmid = shmget(SHM_KEY, sizeof(struct shmseg), IPC_EXCL | 0644 | IPC_CREAT);
    if (shmid == -1)
    {
        int sock = 0, client_fd = -1;
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
        while (time(NULL) - t_0 < 3 && client_fd < 0)
        {
            client_fd = connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
        }
        if (client_fd < 0)
        {
            printf("\nConnection Failed \n");
            laik_shmem_panic(0);
        }

        read(sock, &size, sizeof(int));
        read(sock, &rank, sizeof(int));

        close(client_fd); // closing the connected socket TODO: Maybe move to finalize

        laik_log(2, "Client%d initialization completed", rank);
    }
    else
    {
        size = shm_master_init(shmid);
        int new_socket[size];

        int server_fd;
        struct sockaddr_in address;
        int opt = 1;
        int addrlen = sizeof(address);

        // Creating socket file descriptor
        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
        {
            perror("socket failed");
            exit(EXIT_FAILURE); // TOFO change theese
            laik_shmem_panic(0);
        }

        // Forcefully attaching socket to the port 8080
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
        {
            perror("setsockopt");
            exit(EXIT_FAILURE);
            laik_shmem_panic(0);
        }
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(PORT);

        // Forcefully attaching socket to the port 8080
        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
        {
            perror("bind failed");
            exit(EXIT_FAILURE);
            laik_shmem_panic(0);
        }
        if (listen(server_fd, 3) < 0)
        {
            perror("listen");
            exit(EXIT_FAILURE);
            laik_shmem_panic(0);
        }

        for (int i = 0; i < size - 1; i++)
        {
            if ((new_socket[i] = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
            {
                perror("accept");
                exit(EXIT_FAILURE);
                laik_shmem_panic(0);
            }
        }

        // Assign processes their ranks and tell them the group size.
        rank = 0;
        for (int i = 1; i < size; i++)
        {
            send(new_socket[i - 1], &size, sizeof(int), 0);
            send(new_socket[i - 1], &i, sizeof(int), 0);
        }

        // closing the connected sockets
        for (int i = 0; i < size - 1; i++)
        {
            close(new_socket[i]);
        }

        // closing the listening socket TODO: Maybe move to finalize
        shutdown(server_fd, SHUT_RDWR);

        laik_log(2, "Master initialization completed");
    }

    Laik_Instance *inst;
    inst = laik_new_instance(&laik_backend_shmem, size, rank, 0, 0, "local", 0);

    // create and attach initial world group
    Laik_Group *world = laik_create_group(inst, size);
    world->size = size; // TODO rework world creation process
    world->myid = rank;
    world->locationid[0] = 0;
    inst->world = world;

    laik_log(2, "SHMEM backend initialized\n");

    shmem_instance = inst;
    return inst;
}

static void laik_shmem_finalize(Laik_Instance *inst)
{
    // TODO: Implement needed finalize routines.
    assert(inst == shmem_instance);
}

// calc statistics updates for MPI-specific actions
static void laik_shmem_aseq_calc_stats(Laik_ActionSeq *as)
{
    as = as; // TODO: implement
}

static void laik_shmem_prepare(Laik_ActionSeq *as)
{
    // TODO: Adjust method where necessary
    if (laik_log_begin(1))
    {
        laik_log_append("SHMEM backend prepare:\n");
        laik_log_ActionSeq(as, false);
        laik_log_flush(0);
    }

    // mark as prepared by SHMEM backend: for SHMEM-specific cleanup + action logging
    as->backend = &laik_backend_shmem;

    bool changed = laik_aseq_splitTransitionExecs(as);
    laik_log_ActionSeqIfChanged(changed, as, "After splitting transition execs");
    if (as->actionCount == 0)
    {
        laik_aseq_calc_stats(as);
        return;
    }

    changed = laik_aseq_flattenPacking(as);
    laik_log_ActionSeqIfChanged(changed, as, "After flattening actions");

    // TODO add something similar to mpi_reduce if needed

    changed = laik_aseq_combineActions(as);
    laik_log_ActionSeqIfChanged(changed, as, "After combining actions 1");

    changed = laik_aseq_allocBuffer(as);
    laik_log_ActionSeqIfChanged(changed, as, "After buffer allocation 1");

    changed = laik_aseq_splitReduce(as);
    laik_log_ActionSeqIfChanged(changed, as, "After splitting reduce actions");

    changed = laik_aseq_allocBuffer(as);
    laik_log_ActionSeqIfChanged(changed, as, "After buffer allocation 2");

    changed = laik_aseq_sort_rounds(as);
    laik_log_ActionSeqIfChanged(changed, as, "After sorting rounds");

    changed = laik_aseq_combineActions(as);
    laik_log_ActionSeqIfChanged(changed, as, "After combining actions 2");

    changed = laik_aseq_allocBuffer(as);
    laik_log_ActionSeqIfChanged(changed, as, "After buffer allocation 3");

    changed = laik_aseq_sort_2phases(as);
    // changed = laik_aseq_sort_rankdigits(as);
    laik_log_ActionSeqIfChanged(changed, as, "After sorting for deadlock avoidance");

    // TODO add something similar to mpi_async if needed
    laik_aseq_freeTempSpace(as);

    laik_aseq_calc_stats(as);
    laik_shmem_aseq_calc_stats(as);
}

static void laik_shmem_cleanup(Laik_ActionSeq *as)
{
    if (laik_log_begin(1))
    {
        laik_log_append("SHMEM backend cleanup:\n");
        laik_log_ActionSeq(as, false);
        laik_log_flush(0);
    }

    assert(as->backend == &laik_backend_shmem);

    // TODO: implement cleanup routines if necessary
}

static void laik_shmem_exec(Laik_ActionSeq *as)
{
    if (as->actionCount == 0)
    {
        laik_log(1, "SHMEM backend exec: nothing to do\n");
        return;
    }

    if (as->backend == 0)
    {
        // no preparation: do minimal transformations, sorting send/recv
        laik_log(1, "SHMEM backend exec: prepare before exec\n");
        laik_log_ActionSeqIfChanged(true, as, "Original sequence");
        bool changed = laik_aseq_splitTransitionExecs(as);
        laik_log_ActionSeqIfChanged(changed, as, "After splitting texecs");
        changed = laik_aseq_flattenPacking(as);
        laik_log_ActionSeqIfChanged(changed, as, "After flattening");
        changed = laik_aseq_allocBuffer(as);
        laik_log_ActionSeqIfChanged(changed, as, "After buffer alloc");
        changed = laik_aseq_sort_2phases(as);
        laik_log_ActionSeqIfChanged(changed, as, "After sorting");

        int not_handled = laik_aseq_calc_stats(as);
        assert(not_handled == 0); // there should be no SHMEM-specific actions
    }

    if (laik_log_begin(1))
    {
        laik_log_append("SHMEM backend exec:\n");
        laik_log_ActionSeq(as, false);
        laik_log_flush(0);
    }

    // TODO: use transition context given by each action
    /* TODO: uncomment when it will be used, commented in to supress warnings
    Laik_TransitionContext* tc = as->context[0];
    Laik_MappingList* fromList = tc->fromList;
    Laik_MappingList* toList = tc->toList;
    int elemsize = tc->data->elemsize;
    */

    // TODO: Implement needed variables

    Laik_Action *a = as->action;
    for (unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a))
    {
        // Laik_BackendAction* ba = (Laik_BackendAction*) a; TODO: Uncomment when it will be used.
        if (laik_log_begin(1))
        {
            laik_log_Action(a, as);
            laik_log_flush(0);
        }

        switch (a->type)
        {
        case LAIK_AT_BufReserve:
        case LAIK_AT_Nop:
            // no need to do anything
            break;

        case 0:
        {
            // TODO: Implement the handling of all the different Laik_Actions
            break;
        }
        }
    }
    assert(((char *)as->action) + as->bytesUsed == ((char *)a));
}

static void laik_shmem_updateGroup(Laik_Group *g)
{
    // calculate SHMEM communicator for group <g>
    // TODO: only supports shrinking of parent for now
    assert(g->parent);
    assert(g->parent->size >= g->size);

    laik_log(1, "SHMEM backend updateGroup: parent %d (size %d, myid %d) "
                "=> group %d (size %d, myid %d)",
             g->parent->gid, g->parent->size, g->parent->myid,
             g->gid, g->size, g->myid);

    // only interesting if this task is still part of parent
    if (g->parent->myid < 0)
        return;

    // TODO: implement updateGroup functionality
}

static bool laik_shmem_log_action(Laik_Action *a)
{
    // TODO: Insert needed action types.
    switch (a->type)
    {
    case 0:
    {
        laik_log_append("Log the event");
        break;
    }

    default:
        return false;
    }
    a = a;
    return true;
}

static void laik_shmem_sync(Laik_KVStore *kvs)
{
    // TODO: Add SHMEM related variables
    assert(kvs->inst == shmem_instance);
    Laik_Group *world = kvs->inst->world;
    int myid = world->myid;
    int count[2] = {0, 0};
    // int err = 0; TODO: Uncomment when it will be used.

    if (myid > 0)
    {
        return;
    }

    // master: receive changes from all others, sort, merge, send back

    // first sort own changes, as preparation for merging
    laik_kvs_changes_sort(&(kvs->changes));

    Laik_KVS_Changes recvd, changes;
    laik_kvs_changes_init(&changes); // temporary changes struct
    laik_kvs_changes_init(&recvd);

    Laik_KVS_Changes *src, *dst, *tmp;
    // after merging, result should be in dst;
    dst = &(kvs->changes);
    src = &changes;

    for (int i = 1; i < world->size; i++)
    {
        // TODO: Implement receiving of data
        laik_log(1, "SHMEM sync: getting %d changes (total %d chars) from T%d",
                 count[0] / 2, count[1], i);
        laik_kvs_changes_set_size(&recvd, 0, 0); // fresh reuse
        laik_kvs_changes_ensure_size(&recvd, count[0], count[1]);
        if (count[0] == 0)
        {
            assert(count[1] == 0);
            continue;
        }

        assert(count[1] > 0);
        // TODO: Implement receiving of data
        laik_kvs_changes_set_size(&recvd, count[0], count[1]);

        // for merging, both inputs need to be sorted
        laik_kvs_changes_sort(&recvd);

        // swap src/dst: now merging can overwrite dst
        tmp = src;
        src = dst;
        dst = tmp;

        laik_kvs_changes_merge(dst, src, &recvd);
    }

    // send merged changes to all others: may be 0 entries
    count[0] = dst->offUsed;
    count[1] = dst->dataUsed;
    assert(count[1] > count[0]); // more byte than offsets
    for (int i = 1; i < world->size; i++)
    {
        laik_log(1, "SHMEM sync: sending %d changes (total %d chars) to T%d",
                 count[0] / 2, count[1], i);
        // TODO: Implement sending back of data
    }

    // TODO: opt - remove own changes from received ones
    laik_kvs_changes_apply(dst, kvs);

    laik_kvs_changes_free(&recvd);
    laik_kvs_changes_free(&changes);
}

#endif // USE_SHMEM