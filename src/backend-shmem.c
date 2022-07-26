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

#include <shmem.h>
#include <mpi.h>

// forward decls, types/structs , global variables

static void laik_shmem_finalize(Laik_Instance *);
static void laik_shmem_prepare(Laik_ActionSeq *);
static void laik_shmem_cleanup(Laik_ActionSeq *);
static void laik_shmem_exec(Laik_ActionSeq *as);
static void laik_shmem_updateGroup(Laik_Group *);
static bool laik_shmem_log_action(Laik_Action *a);
static void laik_shmem_sync(Laik_KVStore *kvs);

// C guarantees that unset function pointers are NULL
static Laik_Backend laik_backend_shmem = {
    .name = "SHMEM (two-sided)",
    .finalize = laik_shmem_finalize,
    .prepare = laik_shmem_prepare,
    .cleanup = laik_shmem_cleanup,
    .exec = laik_shmem_exec,
    .updateGroup = laik_shmem_updateGroup,
    .log_action = laik_shmem_log_action,
    .sync = laik_shmem_sync};

static Laik_Instance *shmem_instance = 0;

typedef struct
{
    MPI_Comm comm;
    bool didInit;
} MPIData;

typedef struct
{
    MPI_Comm comm;
} MPIGroupData;

//----------------------------------------------------------------
// buffer space for messages if packing/unpacking from/to not-1d layout
// is necessary
#define PACKBUFSIZE (10 * 1024 * 1024)
//#define PACKBUFSIZE (10*800)
static char packbuf[PACKBUFSIZE];

//----------------------------------------------------------------------------
// MPI-specific actions + transformation

#define LAIK_AT_MpiReq (LAIK_AT_Backend + 0)
#define LAIK_AT_MpiIrecv (LAIK_AT_Backend + 1)
#define LAIK_AT_MpiIsend (LAIK_AT_Backend + 2)
#define LAIK_AT_MpiWait (LAIK_AT_Backend + 3)

// action structs must be packed
#pragma pack(push, 1)

// ReqBuf action: provide base address for MPI_Request array
// referenced in following IRecv/Wait actions via req_it operands
typedef struct
{
    Laik_Action h;
    unsigned int count;
    MPI_Request *req;
} Laik_A_MpiReq;

// IRecv action
typedef struct
{
    Laik_Action h;
    unsigned int count;
    int from_rank;
    int req_id;
    char *buf;
} Laik_A_MpiIrecv;

// ISend action
typedef struct
{
    Laik_Action h;
    unsigned int count;
    int to_rank;
    int req_id;
    char *buf;
} Laik_A_MpiIsend;

#pragma pack(pop)

// Wait action
typedef struct
{
    Laik_Action h;
    int req_id;
} Laik_A_MpiWait;

static bool laik_shmem_log_action(Laik_Action *a)
{
    switch (a->type)
    {
    default:
        return false;
    }
    return true;
}

//----------------------------------------------------------------------------
// error helpers

static void laik_shmem_panic(int err)
{
    laik_log(LAIK_LL_Panic, "SHMEM backend: error '%d'", err);

    char str[SHMEM_MAX_ERROR_STRING];
    int len;

    assert(err != SHMEM_SUCCESS);
    if (shmem_error_string(err, str) == SHMEM_SUCCESS)
        laik_log(LAIK_LL_Panic, "SHMEM backend: SHMEM error '%s'", str);
    else if (MPI_Error_string(err, str, &len) == MPI_SUCCESS)
        laik_log(LAIK_LL_Panic, "SHMEM backend: MPI error '%s'", str);
    else
        laik_panic("SHMEM backend: Unknown error!");
    exit(1);
}

//----------------------------------------------------------------------------
// backend interface implementation: initialization

Laik_Instance *laik_init_shmem(int *argc, char ***argv)
{
    if (shmem_instance)
        return shmem_instance;

    int err;

    err = shmem_init();
    if (err != SHMEM_SUCCESS)
        laik_shmem_panic(err);

    MPIData *d = malloc(sizeof(MPIData));
    if (!d)
    {
        laik_panic("Out of memory allocating MPIData object");
        exit(1); // not actually needed, laik_panic never returns
    }
    d->didInit = false;

    MPIGroupData *gd = malloc(sizeof(MPIGroupData));
    if (!gd)
    {
        laik_panic("Out of memory allocating MPIGroupData object");
        exit(1); // not actually needed, laik_panic never returns
    }

    // eventually initialize MPI first before accessing MPI_COMM_WORLD
    if (argc)
    {
        err = MPI_Init(argc, argv);
        if (err != MPI_SUCCESS)
            laik_shmem_panic(err);
        d->didInit = true;
    }

    // create own communicator duplicating WORLD to
    // - not have to worry about conflicting use of MPI_COMM_WORLD by application
    // - install error handler which passes errors through - we want them
    MPI_Comm ownworld;
    err = MPI_Comm_dup(MPI_COMM_WORLD, &ownworld);
    if (err != MPI_SUCCESS)
        laik_shmem_panic(err);
    err = MPI_Comm_set_errhandler(ownworld, MPI_ERRORS_RETURN);
    if (err != MPI_SUCCESS)
        laik_shmem_panic(err);

    // now finish initilization of <gd>/<d>, as MPI_Init is run
    gd->comm = ownworld;
    d->comm = ownworld;

    int size, rank;
    err = shmem_comm_size(&size);
    if (err != SHMEM_SUCCESS)
        laik_shmem_panic(err);
    err = shmem_comm_rank(&rank);
    if (err != SHMEM_SUCCESS)
        laik_shmem_panic(err);

    // TODO Cut out once MPI Calls are replaced
    err = MPI_Comm_rank(d->comm, &rank);
    if (err != MPI_SUCCESS)
        laik_shmem_panic(err);

    // Get the name of the processor
    char processor_name[MPI_MAX_PROCESSOR_NAME + 15];
    int name_len;
    err = MPI_Get_processor_name(processor_name, &name_len);
    if (err != MPI_SUCCESS)
        laik_shmem_panic(err);
    snprintf(processor_name + name_len, 15, ":%d", getpid());

    Laik_Instance *inst;
    inst = laik_new_instance(&laik_backend_shmem, size, rank, 0, 0,
                             processor_name, d);

    // initial world group
    Laik_Group *world = laik_create_group(inst, size);
    world->size = size;
    world->myid = rank; // same as location ID of this process
    world->backend_data = gd;
    // initial location IDs are the MPI ranks
    for (int i = 0; i < size; i++)
        world->locationid[i] = i;
    // attach world to instance
    inst->world = world;

    sprintf(inst->guid, "%d", rank);

    laik_log(2, "MPI backend initialized (at '%s', rank %d/%d)\n",
             inst->mylocation, rank, size);

    shmem_instance = inst;
    return inst;
}

static MPIData *mpiData(Laik_Instance *i)
{
    return (MPIData *)i->backend_data;
}

static MPIGroupData *mpiGroupData(Laik_Group *g)
{
    return (MPIGroupData *)g->backend_data;
}

static void laik_shmem_finalize(Laik_Instance *inst)
{
    assert(inst == shmem_instance);

    if (mpiData(shmem_instance)->didInit)
    {
        int err = MPI_Finalize(); // TODO remove
        if (err != MPI_SUCCESS)
            laik_shmem_panic(err);

        err = shmem_finalize();
        if (err != SHMEM_SUCCESS)
            laik_shmem_panic(err);
    }
}

// update backend specific data for group if needed
static void laik_shmem_updateGroup(Laik_Group *g)
{
    // calculate MPI communicator for group <g>
    // TODO: only supports shrinking of parent for now
    assert(g->parent);
    assert(g->parent->size >= g->size);

    laik_log(1, "MPI backend updateGroup: parent %d (size %d, myid %d) "
                "=> group %d (size %d, myid %d)",
             g->parent->gid, g->parent->size, g->parent->myid,
             g->gid, g->size, g->myid);

    // only interesting if this task is still part of parent
    if (g->parent->myid < 0)
        return;

    MPIGroupData *gdParent = (MPIGroupData *)g->parent->backend_data;
    assert(gdParent);

    MPIGroupData *gd = (MPIGroupData *)g->backend_data;
    assert(gd == 0); // must not be updated yet
    gd = malloc(sizeof(MPIGroupData));
    if (!gd)
    {
        laik_panic("Out of memory allocating MPIGroupData object");
        exit(1); // not actually needed, laik_panic never returns
    }
    g->backend_data = gd;

    laik_log(1, "MPI Comm_split: old myid %d => new myid %d",
             g->parent->myid, g->fromParent[g->parent->myid]);

    int err = MPI_Comm_split(gdParent->comm, g->myid < 0 ? MPI_UNDEFINED : 0,
                             g->myid, &(gd->comm));
    if (err != MPI_SUCCESS)
        laik_shmem_panic(err);
}

static MPI_Datatype getMPIDataType(Laik_Data *d)
{
    MPI_Datatype mpiDataType;
    if (d->type == laik_Double)
        mpiDataType = MPI_DOUBLE;
    else if (d->type == laik_Float)
        mpiDataType = MPI_FLOAT;
    else if (d->type == laik_Int64)
        mpiDataType = MPI_INT64_T;
    else if (d->type == laik_Int32)
        mpiDataType = MPI_INT32_T;
    else if (d->type == laik_Char)
        mpiDataType = MPI_INT8_T;
    else if (d->type == laik_UInt64)
        mpiDataType = MPI_UINT64_T;
    else if (d->type == laik_UInt32)
        mpiDataType = MPI_UINT32_T;
    else if (d->type == laik_UChar)
        mpiDataType = MPI_UINT8_T;
    else
        assert(0);

    return mpiDataType;
}

/*static int getSHMEMDataType(Laik_Data *d)
{
    int dataType;
    if (d->type == laik_Double)
        dataType = sizeof(double);
    else if (d->type == laik_Float)
        dataType = sizeof(float);
    else if (d->type == laik_Int64)
        dataType = sizeof(int64_t);
    else if (d->type == laik_Int32)
        dataType = sizeof(int32_t);
    else if (d->type == laik_Char)
        dataType = sizeof(int8_t);
    else if (d->type == laik_UInt64)
        dataType = sizeof(uint64_t);
    else if (d->type == laik_UInt32)
        dataType = sizeof(uint32_t);
    else if (d->type == laik_UChar)
        dataType = sizeof(uint8_t);
    else
        assert(0);

    return dataType;
}*/

static void laik_shmem_exec_packAndSend(Laik_Mapping *map, Laik_Range *range,
                                        int to_rank, uint64_t slc_size,
                                        MPI_Datatype dataType, int tag, MPI_Comm comm)
{
    Laik_Index idx = range->from;
    int dims = range->space->dims;
    unsigned int packed;
    uint64_t count = 0;
    while (1)
    {
        packed = (map->layout->pack)(map, range, &idx,
                                     packbuf, PACKBUFSIZE);
        assert(packed > 0);
        int err = MPI_Send(packbuf, (int)packed,
                           dataType, to_rank, tag, comm);
        if (err != MPI_SUCCESS)
            laik_shmem_panic(err);

        count += packed;
        if (laik_index_isEqual(dims, &idx, &(range->to)))
            break;
    }
    assert(count == slc_size);
}

static void laik_shmem_exec_recvAndUnpack(Laik_Mapping *map, Laik_Range *range,
                                          int from_rank, uint64_t slc_size,
                                          int elemsize,
                                          MPI_Datatype dataType, int tag, MPI_Comm comm)
{
    MPI_Status st;
    Laik_Index idx = range->from;
    int dims = range->space->dims;
    int recvCount, unpacked;
    uint64_t count = 0;
    while (1)
    {
        int err = MPI_Recv(packbuf, PACKBUFSIZE / elemsize,
                           dataType, from_rank, tag, comm, &st);
        if (err != MPI_SUCCESS)
            laik_shmem_panic(err);
        err = MPI_Get_count(&st, dataType, &recvCount);
        if (err != MPI_SUCCESS)
            laik_shmem_panic(err);

        unpacked = (map->layout->unpack)(map, range, &idx,
                                         packbuf, recvCount * elemsize);
        assert(recvCount == unpacked);
        count += unpacked;
        if (laik_index_isEqual(dims, &idx, &(range->to)))
            break;
    }
    assert(count == slc_size);
}

// a naive, manual reduction using send/recv:
// one process is chosen to do the reduction: the smallest rank from processes
// which are interested in the result. All other processes with input
// send their data to him, he does the reduction, and sends to all processes
// interested in the result
static void laik_shmem_exec_groupReduce(Laik_TransitionContext *tc,
                                        Laik_BackendAction *a,
                                        MPI_Datatype dataType, MPI_Comm comm)
{
    assert(a->h.type == LAIK_AT_GroupReduce);
    Laik_Transition *t = tc->transition;
    Laik_Data *data = tc->data;

    // do the manual reduction on smallest rank of output group
    int reduceTask = laik_trans_taskInGroup(t, a->outputGroup, 0);
    laik_log(1, "      exec reduce at T%d", reduceTask);

    int myid = t->group->myid;
    MPI_Status st;
    int count, err;

    if (myid != reduceTask)
    {
        // not the reduce task: eventually send input and recv result

        if (laik_trans_isInGroup(t, a->inputGroup, myid))
        {
            laik_log(1, "        exec MPI_Send to T%d", reduceTask);
            err = MPI_Send(a->fromBuf, (int)a->count, dataType,
                           reduceTask, 1, comm);
            if (err != MPI_SUCCESS)
                laik_shmem_panic(err);
        }
        if (laik_trans_isInGroup(t, a->outputGroup, myid))
        {
            laik_log(1, "        exec MPI_Recv from T%d", reduceTask);
            err = MPI_Recv(a->toBuf, (int)a->count, dataType,
                           reduceTask, 1, comm, &st);
            if (err != MPI_SUCCESS)
                laik_shmem_panic(err);
            // check that we received the expected number of elements
            err = MPI_Get_count(&st, dataType, &count);
            if (err != MPI_SUCCESS)
                laik_shmem_panic(err);
            assert((int)a->count == count);
        }
        return;
    }

    // we are the reduce task
    int inCount = laik_trans_groupCount(t, a->inputGroup);
    uint64_t byteCount = a->count * data->elemsize;
    bool inputFromMe = laik_trans_isInGroup(t, a->inputGroup, myid);

    // for direct execution: use global <packbuf> (size PACKBUFSIZE)
    // check that bufsize is enough. TODO: dynamically increase?
    int bufSize = (inCount - (inputFromMe ? 1 : 0)) * byteCount;
    assert(bufSize < PACKBUFSIZE);

    // collect values from tasks in input group
    int bufOff[32], off = 0;
    assert(inCount <= 32);

    // always put this task in front: we use toBuf to calculate
    // our results, but there may be input from us, which would
    // be overwritten if not starting with our input
    int ii = 0;
    if (inputFromMe)
    {
        ii++; // slot 0 reserved for this task (use a->fromBuf)
        bufOff[0] = 0;
    }
    for (int i = 0; i < inCount; i++)
    {
        int inTask = laik_trans_taskInGroup(t, a->inputGroup, i);
        if (inTask == myid)
            continue;

        laik_log(1, "        exec MPI_Recv from T%d (buf off %d, count %d)",
                 inTask, off, a->count);

        bufOff[ii++] = off;
        err = MPI_Recv(packbuf + off, (int)a->count, dataType,
                       inTask, 1, comm, &st);
        if (err != MPI_SUCCESS)
            laik_shmem_panic(err);
        // check that we received the expected number of elements
        err = MPI_Get_count(&st, dataType, &count);
        if (err != MPI_SUCCESS)
            laik_shmem_panic(err);
        assert((int)a->count == count);
        off += byteCount;
    }
    assert(ii == inCount);
    assert(off == bufSize);

    // do the reduction, put result back to my input buffer
    if (data->type->reduce)
    {
        // reduce with 0/1 inputs by setting input pointer to 0
        char *buf0 = inputFromMe ? a->fromBuf : (packbuf + bufOff[0]);
        (data->type->reduce)(a->toBuf,
                             (inCount < 1) ? 0 : buf0,
                             (inCount < 2) ? 0 : (packbuf + bufOff[1]),
                             a->count, a->redOp);
        for (int t = 2; t < inCount; t++)
            (data->type->reduce)(a->toBuf, a->toBuf, packbuf + bufOff[t],
                                 a->count, a->redOp);
    }
    else
    {
        laik_log(LAIK_LL_Panic,
                 "Need reduce function for type '%s'. Not set!",
                 data->type->name);
        assert(0);
    }

    // send result to tasks in output group
    int outCount = laik_trans_groupCount(t, a->outputGroup);
    for (int i = 0; i < outCount; i++)
    {
        int outTask = laik_trans_taskInGroup(t, a->outputGroup, i);
        if (outTask == myid)
        {
            // that's myself: nothing to do
            continue;
        }

        laik_log(1, "        exec MPI_Send result to T%d", outTask);
        err = MPI_Send(a->toBuf, (int)a->count, dataType, outTask, 1, comm);
        if (err != MPI_SUCCESS)
            laik_shmem_panic(err);
    }
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
    Laik_TransitionContext *tc = as->context[0];
    Laik_MappingList *fromList = tc->fromList;
    Laik_MappingList *toList = tc->toList;
    int elemsize = tc->data->elemsize;

    // common for all MPI calls: tag, comm, datatype
    int tag = 1;
    MPIGroupData *gd = mpiGroupData(tc->transition->group);
    assert(gd);
    MPI_Comm comm = gd->comm;
    MPI_Datatype dataType = getMPIDataType(tc->data);
    MPI_Status st;
    int err, count;

    // int dType = getSHMEMDataType(tc->data); // TODO merge with dataType, this way it has a double meaning

    Laik_Action *a = as->action;
    for (unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a))
    {
        Laik_BackendAction *ba = (Laik_BackendAction *)a;
        if (laik_log_begin(3)) // TODO change level back to 1
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

        case LAIK_AT_MapSend:
        {
            assert(ba->fromMapNo < fromList->count);
            Laik_Mapping *fromMap = &(fromList->map[ba->fromMapNo]);
            assert(fromMap->base != 0);
            err = MPI_Send(fromMap->base + ba->offset, ba->count,
                           dataType, ba->rank, tag, comm);
            if (err != MPI_SUCCESS)
                laik_shmem_panic(err);
            break;
        }

        case LAIK_AT_RBufSend:
        {
            Laik_A_RBufSend *aa = (Laik_A_RBufSend *)a;
            assert(aa->bufID < ASEQ_BUFFER_MAX);
            err = MPI_Send(as->buf[aa->bufID] + aa->offset, aa->count,
                           dataType, aa->to_rank, tag, comm);
            if (err != MPI_SUCCESS)
                laik_shmem_panic(err);
            break;
        }

        case LAIK_AT_BufSend:
        {
            Laik_A_BufSend *aa = (Laik_A_BufSend *)a;
            err = MPI_Send(aa->buf, aa->count,
                           dataType, aa->to_rank, tag, comm);
            if (err != MPI_SUCCESS)
                laik_shmem_panic(err);
            break;
        }

        case LAIK_AT_MapRecv:
        {
            assert(ba->toMapNo < toList->count);
            Laik_Mapping *toMap = &(toList->map[ba->toMapNo]);
            assert(toMap->base != 0);
            err = MPI_Recv(toMap->base + ba->offset, ba->count,
                           dataType, ba->rank, tag, comm, &st);
            if (err != MPI_SUCCESS)
                laik_shmem_panic(err);

            // check that we received the expected number of elements
            err = MPI_Get_count(&st, dataType, &count);
            if (err != MPI_SUCCESS)
                laik_shmem_panic(err);
            assert((int)ba->count == count);
            break;
        }

        case LAIK_AT_RBufRecv:
        {
            Laik_A_RBufRecv *aa = (Laik_A_RBufRecv *)a;
            assert(aa->bufID < ASEQ_BUFFER_MAX);
            err = MPI_Recv(as->buf[aa->bufID] + aa->offset, aa->count,
                           dataType, aa->from_rank, tag, comm, &st);
            if (err != MPI_SUCCESS)
                laik_shmem_panic(err);

            // check that we received the expected number of elements
            err = MPI_Get_count(&st, dataType, &count);
            if (err != MPI_SUCCESS)
                laik_shmem_panic(err);
            assert((int)ba->count == count);
            break;
        }

        case LAIK_AT_BufRecv:
        {
            Laik_A_BufRecv *aa = (Laik_A_BufRecv *)a;
            err = MPI_Recv(aa->buf, aa->count,
                           dataType, aa->from_rank, tag, comm, &st);
            if (err != MPI_SUCCESS)
                laik_shmem_panic(err);

            // check that we received the expected number of elements
            err = MPI_Get_count(&st, dataType, &count);
            if (err != MPI_SUCCESS)
                laik_shmem_panic(err);
            assert((int)ba->count == count);
            break;
        }

        case LAIK_AT_CopyFromBuf:
            for (unsigned int i = 0; i < ba->count; i++)
                memcpy(ba->ce[i].ptr,
                       ba->fromBuf + ba->ce[i].offset,
                       ba->ce[i].bytes);
            break;

        case LAIK_AT_CopyToBuf:
            for (unsigned int i = 0; i < ba->count; i++)
                memcpy(ba->toBuf + ba->ce[i].offset,
                       ba->ce[i].ptr,
                       ba->ce[i].bytes);
            break;

        case LAIK_AT_PackToBuf:
            laik_exec_pack(ba, ba->map);
            break;

        case LAIK_AT_MapPackToBuf:
        {
            assert(ba->fromMapNo < fromList->count);
            Laik_Mapping *fromMap = &(fromList->map[ba->fromMapNo]);
            assert(fromMap->base != 0);
            laik_exec_pack(ba, fromMap);
            break;
        }

        case LAIK_AT_UnpackFromBuf:
            laik_exec_unpack(ba, ba->map);
            break;

        case LAIK_AT_MapUnpackFromBuf:
        {
            assert(ba->toMapNo < toList->count);
            Laik_Mapping *toMap = &(toList->map[ba->toMapNo]);
            assert(toMap->base);
            laik_exec_unpack(ba, toMap);
            break;
        }

        case LAIK_AT_MapPackAndSend:
        {
            Laik_A_MapPackAndSend *aa = (Laik_A_MapPackAndSend *)a;
            assert(aa->fromMapNo < fromList->count);
            Laik_Mapping *fromMap = &(fromList->map[aa->fromMapNo]);
            assert(fromMap->base != 0);
            laik_shmem_exec_packAndSend(fromMap, aa->range, aa->to_rank, aa->count,
                                        dataType, tag, comm);
            break;
        }

        case LAIK_AT_PackAndSend:
            laik_shmem_exec_packAndSend(ba->map, ba->range, ba->rank,
                                        (uint64_t)ba->count,
                                        dataType, tag, comm);
            break;

        case LAIK_AT_MapRecvAndUnpack:
        {
            Laik_A_MapRecvAndUnpack *aa = (Laik_A_MapRecvAndUnpack *)a;
            assert(aa->toMapNo < toList->count);
            Laik_Mapping *toMap = &(toList->map[aa->toMapNo]);
            assert(toMap->base);
            laik_shmem_exec_recvAndUnpack(toMap, aa->range, aa->from_rank, aa->count,
                                          elemsize, dataType, tag, comm);
            break;
        }

        case LAIK_AT_RecvAndUnpack:
            laik_shmem_exec_recvAndUnpack(ba->map, ba->range, ba->rank,
                                          (uint64_t)ba->count,
                                          elemsize, dataType, tag, comm);
            break;

        case LAIK_AT_Reduce:
            laik_log(42, "LAIK_AT_Reduce wanted but not implemented.");
            laik_shmem_panic(42);
            break;

        case LAIK_AT_GroupReduce:
            laik_shmem_exec_groupReduce(tc, ba, dataType, comm);
            break;

        case LAIK_AT_RBufLocalReduce:
            assert(ba->bufID < ASEQ_BUFFER_MAX);
            assert(ba->dtype->reduce != 0);
            (ba->dtype->reduce)(ba->toBuf, ba->toBuf, as->buf[ba->bufID] + ba->offset,
                                ba->count, ba->redOp);
            break;

        case LAIK_AT_RBufCopy:
            assert(ba->bufID < ASEQ_BUFFER_MAX);
            memcpy(ba->toBuf, as->buf[ba->bufID] + ba->offset, ba->count * elemsize);
            break;

        case LAIK_AT_BufCopy:
            memcpy(ba->toBuf, ba->fromBuf, ba->count * elemsize);
            break;

        case LAIK_AT_BufInit:
            assert(ba->dtype->init != 0);
            (ba->dtype->init)(ba->toBuf, ba->count, ba->redOp);
            break;

        default:
            laik_log(LAIK_LL_Panic, "mpi_exec: no idea how to exec action %d (%s)",
                     a->type, laik_at_str(a->type));
            assert(0);
        }
    }
    assert(((char *)as->action) + as->bytesUsed == ((char *)a));
}

// calc statistics updates for MPI-specific actions
static void laik_shmem_aseq_calc_stats(Laik_ActionSeq *as)
{
    unsigned int count;
    Laik_TransitionContext *tc = as->context[0];
    int current_tid = 0;
    Laik_Action *a = as->action;
    for (unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a))
    {
        assert(a->tid == current_tid); // TODO: only assumes actions from one transition
        switch (a->type)
        {
        case LAIK_AT_MpiIsend:
            count = ((Laik_A_MpiIsend *)a)->count;
            as->msgAsyncSendCount++;
            as->elemSendCount += count;
            as->byteSendCount += count * tc->data->elemsize;
            break;
        case LAIK_AT_MpiIrecv:
            count = ((Laik_A_MpiIrecv *)a)->count;
            as->msgAsyncRecvCount++;
            as->elemRecvCount += count;
            as->byteRecvCount += count * tc->data->elemsize;
            break;
        default:
            break;
        }
    }
}

static void laik_shmem_prepare(Laik_ActionSeq *as)
{
    if (laik_log_begin(1))
    {
        laik_log_append("MPI backend prepare:\n");
        laik_log_ActionSeq(as, false);
        laik_log_flush(0);
    }

    // mark as prepared by MPI backend: for MPI-specific cleanup + action logging
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

    laik_aseq_freeTempSpace(as);

    laik_aseq_calc_stats(as);
    laik_shmem_aseq_calc_stats(as);
}

static void laik_shmem_cleanup(Laik_ActionSeq *as)
{
    if (laik_log_begin(1))
    {
        laik_log_append("MPI backend cleanup:\n");
        laik_log_ActionSeq(as, false);
        laik_log_flush(0);
    }

    assert(as->backend == &laik_backend_shmem);

    if ((as->actionCount > 0) && (as->action->type == LAIK_AT_MpiReq))
    {
        Laik_A_MpiReq *aa = (Laik_A_MpiReq *)as->action;
        free(aa->req);
        laik_log(1, "  freed MPI_Request array with %d entries", aa->count);
    }
}

//----------------------------------------------------------------------------
// KV store

static void laik_shmem_sync(Laik_KVStore *kvs)
{
    assert(kvs->inst == shmem_instance);
    MPI_Comm comm = mpiData(shmem_instance)->comm;
    Laik_Group *world = kvs->inst->world;
    int myid = world->myid;
    MPI_Status status;
    int count[2] = {0, 0};
    int err;

    if (myid > 0)
    {
        // send to master, receive from master
        count[0] = (int)kvs->changes.offUsed;
        assert((count[0] == 0) || ((count[0] & 1) == 1)); // 0 or odd number of offsets
        count[1] = (int)kvs->changes.dataUsed;
        laik_log(1, "MPI sync: sending %d changes (total %d chars) to T0",
                 count[0] / 2, count[1]);
        err = MPI_Send(count, 2, MPI_INTEGER, 0, 0, comm);
        if (err != MPI_SUCCESS)
            laik_shmem_panic(err);
        if (count[0] > 0)
        {
            assert(count[1] > 0);
            err = MPI_Send(kvs->changes.off, count[0], MPI_INTEGER, 0, 0, comm);
            if (err != MPI_SUCCESS)
                laik_shmem_panic(err);
            err = MPI_Send(kvs->changes.data, count[1], MPI_CHAR, 0, 0, comm);
            if (err != MPI_SUCCESS)
                laik_shmem_panic(err);
        }
        else
            assert(count[1] == 0);

        err = MPI_Recv(count, 2, MPI_INTEGER, 0, 0, comm, &status);
        if (err != MPI_SUCCESS)
            laik_shmem_panic(err);
        laik_log(1, "MPI sync: getting %d changes (total %d chars) from T0",
                 count[0] / 2, count[1]);
        if (count[0] > 0)
        {
            assert(count[1] > 0);
            laik_kvs_changes_ensure_size(&(kvs->changes), count[0], count[1]);
            err = MPI_Recv(kvs->changes.off, count[0], MPI_INTEGER, 0, 0, comm, &status);
            if (err != MPI_SUCCESS)
                laik_shmem_panic(err);
            err = MPI_Recv(kvs->changes.data, count[1], MPI_CHAR, 0, 0, comm, &status);
            if (err != MPI_SUCCESS)
                laik_shmem_panic(err);
            laik_kvs_changes_set_size(&(kvs->changes), count[0], count[1]);
            // TODO: opt - remove own changes from received ones
            laik_kvs_changes_apply(&(kvs->changes), kvs);
        }
        else
            assert(count[1] == 0);

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
        err = MPI_Recv(count, 2, MPI_INTEGER, i, 0, comm, &status);
        if (err != MPI_SUCCESS)
            laik_shmem_panic(err);
        laik_log(1, "MPI sync: getting %d changes (total %d chars) from T%d",
                 count[0] / 2, count[1], i);
        laik_kvs_changes_set_size(&recvd, 0, 0); // fresh reuse
        laik_kvs_changes_ensure_size(&recvd, count[0], count[1]);
        if (count[0] == 0)
        {
            assert(count[1] == 0);
            continue;
        }

        assert(count[1] > 0);
        err = MPI_Recv(recvd.off, count[0], MPI_INTEGER, i, 0, comm, &status);
        if (err != MPI_SUCCESS)
            laik_shmem_panic(err);
        err = MPI_Recv(recvd.data, count[1], MPI_CHAR, i, 0, comm, &status);
        if (err != MPI_SUCCESS)
            laik_shmem_panic(err);
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
        laik_log(1, "MPI sync: sending %d changes (total %d chars) to T%d",
                 count[0] / 2, count[1], i);
        err = MPI_Send(count, 2, MPI_INTEGER, i, 0, comm);
        if (err != MPI_SUCCESS)
            laik_shmem_panic(err);
        if (count[0] == 0)
            continue;

        err = MPI_Send(dst->off, count[0], MPI_INTEGER, i, 0, comm);
        if (err != MPI_SUCCESS)
            laik_shmem_panic(err);
        err = MPI_Send(dst->data, count[1], MPI_CHAR, i, 0, comm);
        if (err != MPI_SUCCESS)
            laik_shmem_panic(err);
    }

    // TODO: opt - remove own changes from received ones
    laik_kvs_changes_apply(dst, kvs);

    laik_kvs_changes_free(&recvd);
    laik_kvs_changes_free(&changes);
}

#endif // USE_SHMEM