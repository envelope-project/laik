/*
 * This file is part of the LAIK library.
 * Copyright (c) 2022 Robert Hubinger <robert.hubinger@tum.de>
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
#include <mpi.h>
#include <shmem.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

// forward decls, types/structs , global variables

static void laik_shmem_finalize(Laik_Instance *);
static void laik_shmem_prepare(Laik_ActionSeq *);
static void laik_shmem_cleanup(Laik_ActionSeq *);
static void laik_shmem_exec(Laik_ActionSeq *as);
static void laik_shmem_updateGroup(Laik_Group *);
static void laik_shmem_sync(Laik_KVStore *kvs);

// C guarantees that unset function pointers are NULL
static Laik_Backend laik_backend_shmem = {
    .name = "shmem (two-sided)",
    .finalize = laik_shmem_finalize,
    .prepare = laik_shmem_prepare,
    .cleanup = laik_shmem_cleanup,
    .exec = laik_shmem_exec,
    .updateGroup = laik_shmem_updateGroup,
    .sync = laik_shmem_sync
};

static Laik_Instance *shmem_instance = 0;

#define LAIK_AT_ShmemMapSend (LAIK_AT_Backend + 50)
#define LAIK_AT_ShmemRBufSend (LAIK_AT_Backend + 51)
#define LAIK_AT_ShmemBufSend (LAIK_AT_Backend + 52)
#define LAIK_AT_ShmemMapRecv (LAIK_AT_Backend + 53)
#define LAIK_AT_ShmemRBufRecv (LAIK_AT_Backend + 54)
#define LAIK_AT_ShmemBufRecv (LAIK_AT_Backend + 55)

#define LAIK_AT_ShmemMapPackAndSend (LAIK_AT_Backend + 56)
#define LAIK_AT_ShmemPackAndSend (LAIK_AT_Backend + 57)
#define LAIK_AT_ShmemMapRecvAndUnpack (LAIK_AT_Backend + 58)
#define LAIK_AT_ShmemRecvAndUnpack (LAIK_AT_Backend + 59)

typedef struct
{
    int comm;
    bool didInit;
} SHMEMData;

typedef struct
{
    int comm;
} SHMEMGroupData;

//----------------------------------------------------------------
// buffer space for messages if packing/unpacking from/to not-1d layout
// is necessary
#define PACKBUFSIZE (10 * 1024 * 1024)
//#define PACKBUFSIZE (10*800)
static char packbuf[PACKBUFSIZE];

//----------------------------------------------------------------------------
// error helpers

static void laik_shmem_panic(int err)
{
    char str[SHMEM_MAX_ERROR_STRING];

    assert(err != SHMEM_SUCCESS);
    if (shmem_error_string(err, str) != SHMEM_SUCCESS)
        laik_panic("SHMEM backend: Unknown SHMEM error!");
    else
        laik_log(LAIK_LL_Panic, "SHMEM backend: SHMEM error '%s'", str);
    exit(1);
}

//----------------------------------------------------------------------------
// backend interface implementation: initialization

Laik_Instance *laik_init_shmem(int *argc, char ***argv)
{
    (void) argc;
    (void) argv;
    
    if (shmem_instance)
        return shmem_instance;

    int err;

    SHMEMData *d = malloc(sizeof(SHMEMData));
    if (!d)
    {
        laik_panic("Out of memory allocating SHMEMData object");
        exit(1); // not actually needed, laik_panic never returns
    }
    d->didInit = false;

    SHMEMGroupData *gd = malloc(sizeof(SHMEMGroupData));
    if (!gd)
    {
        laik_panic("Out of memory allocating SHMEMGroupData object");
        exit(1); // not actually needed, laik_panic never returns
    }

    // eventually initialize SHMEM
    if (argc)
    {
        err = shmem_init();
        if(err != SHMEM_SUCCESS)
            laik_shmem_panic(err);
        d->didInit = true;
    }

    // now finish initilization of <gd>/<d>
    shmem_get_identifier(&gd->comm);
    shmem_get_identifier(&d->comm);

    int size, rank;
    err = shmem_comm_size(&size);
    if (err != SHMEM_SUCCESS)
        laik_shmem_panic(err);
    err = shmem_comm_rank(&rank);
    if (err != SHMEM_SUCCESS)
        laik_shmem_panic(err);

    char *processor_name = "InterimNameProcessor";
    Laik_Instance *inst;
    inst = laik_new_instance(&laik_backend_shmem, size, rank, 0, 0, processor_name, d);

    // initial world group
    Laik_Group *world = laik_create_group(inst, size);
    world->size = size;
    world->myid = rank; // same as location ID of this process
    world->backend_data = gd;
    // initial location IDs are the SHMEM ranks
    for (int i = 0; i < size; i++)
        world->locationid[i] = i;
    // attach world to instance
    inst->world = world;

    sprintf(inst->guid, "%d", rank);

    laik_log(2, "SHMEM backend initialized (at '%s', rank %d/%d)\n",
             inst->mylocation, rank, size);

    shmem_instance = inst;
    return inst;
}

static SHMEMData *shmemData(Laik_Instance *i)
{
    return (SHMEMData *)i->backend_data;
}

static void laik_shmem_finalize(Laik_Instance *inst)
{
    assert(inst == shmem_instance);

    if (shmemData(shmem_instance)->didInit)
    {
        int err = shmem_finalize();
        if (err != SHMEM_SUCCESS)
            laik_shmem_panic(err);
    }
}

// update backend specific data for group if needed
static void laik_shmem_updateGroup(Laik_Group *g)
{
    (void) g;
    return;
}

static int getSHMEMDataType(Laik_Data *d)
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
}

static void laik_shmem_exec_packAndSend(Laik_Mapping *map, Laik_Range *range, int to_rank, uint64_t slc_size, int dataType)
{
    Laik_Index idx = range->from;
    int dims = range->space->dims;
    unsigned int packed;
    uint64_t count = 0;
    while (1)
    {
        packed = (map->layout->pack)(map, range, &idx, packbuf, PACKBUFSIZE);
        assert(packed > 0);
        int err = shmem_send(packbuf, (int)packed, dataType, to_rank);
        if (err != SHMEM_SUCCESS)
            laik_shmem_panic(err);

        count += packed;
        if (laik_index_isEqual(dims, &idx, &(range->to)))
            break;
    }
    assert(count == slc_size);
}

static void laik_shmem_exec_recvAndUnpack(Laik_Mapping *map, Laik_Range *range, int from_rank, uint64_t slc_size, int elemsize, int dataType)
{
    Laik_Index idx = range->from;
    int dims = range->space->dims;
    int recvCount, unpacked;
    uint64_t count = 0;
    while (1)
    {
        int err = shmem_recv(packbuf, PACKBUFSIZE / elemsize, dataType, from_rank, &recvCount);
        if (err != SHMEM_SUCCESS)
            laik_shmem_panic(err);

        unpacked = (map->layout->unpack)(map, range, &idx, packbuf, recvCount * elemsize);
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
static void laik_shmem_exec_groupReduce(Laik_TransitionContext *tc, Laik_BackendAction *a, int dataType)
{
    assert(a->h.type == LAIK_AT_GroupReduce);
    Laik_Transition *t = tc->transition;
    Laik_Data *data = tc->data;

    // do the manual reduction on smallest rank of output group
    int reduceTask = laik_trans_taskInGroup(t, a->outputGroup, 0);
    laik_log(1, "      exec reduce at T%d", reduceTask);

    int myid = t->group->myid;
    int count, err;

    if (myid != reduceTask)
    {
        // not the reduce task: eventually send input and recv result

        if (laik_trans_isInGroup(t, a->inputGroup, myid))
        {
            laik_log(1, "        exec SHMEM_Send to T%d", reduceTask);
            err = shmem_send(a->fromBuf, (int)a->count, dataType, reduceTask);
            if (err != SHMEM_SUCCESS)
                laik_shmem_panic(err);
        }
        if (laik_trans_isInGroup(t, a->outputGroup, myid))
        {
            laik_log(1, "        exec SHMEM_Recv from T%d", reduceTask);
            err = shmem_recv(a->toBuf, (int)a->count, dataType, reduceTask, &count);
            if (err != SHMEM_SUCCESS)
                laik_shmem_panic(err);
            // check that we received the expected number of elements
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

        laik_log(1, "        exec SHMEM_Recv from T%d (buf off %d, count %d)",
                 inTask, off, a->count);

        bufOff[ii++] = off;
        err = shmem_recv(packbuf + off, (int)a->count, dataType, inTask, &count);
        if (err != SHMEM_SUCCESS)
            laik_shmem_panic(err);
        // check that we received the expected number of elements
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

        laik_log(1, "        exec SHMEM_Send result to T%d", outTask);
        err = shmem_send(a->toBuf, (int)a->count, dataType, outTask);
        if (err != SHMEM_SUCCESS)
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
        assert(not_handled == 0);
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

    int err, count;
    int dType = getSHMEMDataType(tc->data);

    Laik_Action *a = as->action;
    for (unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a))
    {
        Laik_BackendAction *ba = (Laik_BackendAction *)a;
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

        case LAIK_AT_MapSend:
        {
            assert(ba->fromMapNo < fromList->count);
            Laik_Mapping *fromMap = &(fromList->map[ba->fromMapNo]);
            assert(fromMap->base != 0);
            err = shmem_send(fromMap->base + ba->offset, ba->count, dType, ba->rank);
            if (err != SHMEM_SUCCESS)
                laik_shmem_panic(err);
            break;
        }

        case LAIK_AT_RBufSend:
        {
            Laik_A_RBufSend *aa = (Laik_A_RBufSend *)a;
            assert(aa->bufID < ASEQ_BUFFER_MAX);
            err = shmem_send(as->buf[aa->bufID] + aa->offset, aa->count, dType, aa->to_rank);
            if (err != SHMEM_SUCCESS)
                laik_shmem_panic(err);
            break;
        }

        case LAIK_AT_BufSend: {
            Laik_A_BufSend* aa = (Laik_A_BufSend*) a;
            err = shmem_send(aa->buf, aa->count, dType, aa->to_rank);
            if (err != SHMEM_SUCCESS) 
                laik_shmem_panic(err);
            break;
        }

        case LAIK_AT_MapRecv:
        {
            assert(ba->toMapNo < toList->count);
            Laik_Mapping *toMap = &(toList->map[ba->toMapNo]);
            assert(toMap->base != 0);
            err = shmem_recv(toMap->base + ba->offset, ba->count, dType, ba->rank, &count);
            if (err != SHMEM_SUCCESS)
                laik_shmem_panic(err);
            assert((int)ba->count == count);
            break;
        }

        case LAIK_AT_RBufRecv:
        {
            Laik_A_RBufRecv *aa = (Laik_A_RBufRecv *)a;
            assert(aa->bufID < ASEQ_BUFFER_MAX);
            err = shmem_recv(as->buf[aa->bufID] + aa->offset, aa->count, dType, aa->from_rank, &count);
            if (err != SHMEM_SUCCESS)
                laik_shmem_panic(err);
            assert((int)ba->count == count);
            break;
        }

        case LAIK_AT_BufRecv: {
            Laik_A_BufRecv* aa = (Laik_A_BufRecv*) a;
            err = shmem_recv(aa->buf, aa->count, dType, aa->from_rank, &count);
            if (err != SHMEM_SUCCESS) 
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
            laik_shmem_exec_packAndSend(fromMap, aa->range, aa->to_rank, aa->count, dType);
            break;
        }

        case LAIK_AT_PackAndSend:
            laik_shmem_exec_packAndSend(ba->map, ba->range, ba->rank, (uint64_t)ba->count, dType);
            break;

        case LAIK_AT_MapRecvAndUnpack:
        {
            Laik_A_MapRecvAndUnpack *aa = (Laik_A_MapRecvAndUnpack *)a;
            assert(aa->toMapNo < toList->count);
            Laik_Mapping *toMap = &(toList->map[aa->toMapNo]);
            assert(toMap->base);
            laik_shmem_exec_recvAndUnpack(toMap, aa->range, aa->from_rank, aa->count, elemsize, dType);
            break;
        }

        case LAIK_AT_RecvAndUnpack:
            laik_shmem_exec_recvAndUnpack(ba->map, ba->range, ba->rank, (uint64_t)ba->count, elemsize, dType);
            break;

        case LAIK_AT_GroupReduce:
            laik_shmem_exec_groupReduce(tc, ba, dType);
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
            laik_log(LAIK_LL_Panic, "shmem_exec: no idea how to exec action %d (%s)",
                     a->type, laik_at_str(a->type));
            assert(0);
        }
    }
    assert(((char *)as->action) + as->bytesUsed == ((char *)a));
}

static void laik_shmem_prepare(Laik_ActionSeq *as)
{
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
}

//----------------------------------------------------------------------------
// KV store

static void laik_shmem_sync(Laik_KVStore *kvs)
{
    assert(kvs->inst == shmem_instance);
    Laik_Group *world = kvs->inst->world;
    int myid = world->myid;
    int count[2] = {0, 0};
    int received, err;

    if (myid > 0)
    {
        // send to master, receive from master
        count[0] = (int)kvs->changes.offUsed;
        assert((count[0] == 0) || ((count[0] & 1) == 1)); // 0 or odd number of offsets
        count[1] = (int)kvs->changes.dataUsed;
        laik_log(1, "SHMEM sync: sending %d changes (total %d chars) to T0",
                 count[0] / 2, count[1]);
        err = shmem_send(count, 2, sizeof(int), 0);
        if (err != SHMEM_SUCCESS)
            laik_shmem_panic(err);
        if (count[0] > 0)
        {
            assert(count[1] > 0);
            err = shmem_send(kvs->changes.off, count[0], sizeof(int), 0);
            if (err != SHMEM_SUCCESS)
                laik_shmem_panic(err);
            err = shmem_send(kvs->changes.data, count[1], sizeof(char), 0);
            if (err != SHMEM_SUCCESS)
                laik_shmem_panic(err);
        }
        else
            assert(count[1] == 0);

        err = shmem_recv(count, 2, sizeof(int) , 0, &received);
        if (err != SHMEM_SUCCESS)
            laik_shmem_panic(err);
        laik_log(1, "SHMEM sync: getting %d changes (total %d chars) from T0",
                 count[0] / 2, count[1]);
        if (count[0] > 0)
        {
            assert(count[1] > 0);
            laik_kvs_changes_ensure_size(&(kvs->changes), count[0], count[1]);
            err = shmem_recv(kvs->changes.off, count[0], sizeof(int), 0, &received);
            if (err != SHMEM_SUCCESS)
                laik_shmem_panic(err);
            err = shmem_recv(kvs->changes.data, count[1], sizeof(char), 0, &received);
            if (err != SHMEM_SUCCESS)
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
        err = shmem_recv(count, 2, sizeof(int), i, &received);
        if (err != SHMEM_SUCCESS)
            laik_shmem_panic(err);
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
        err = shmem_recv(recvd.off, count[0], sizeof(int), i, &received);
        if (err != SHMEM_SUCCESS)
            laik_shmem_panic(err);
        err = shmem_recv(recvd.data, count[1], sizeof(char), i, &received);
        if (err != SHMEM_SUCCESS)
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
        laik_log(1, "SHMEM sync: sending %d changes (total %d chars) to T%d",
                 count[0] / 2, count[1], i);
        err = shmem_send(count, 2, sizeof(int), i);
        if (err != SHMEM_SUCCESS)
            laik_shmem_panic(err);
        if (count[0] == 0)
            continue;

        err = shmem_send(dst->off, count[0], sizeof(int), i);
        if (err != SHMEM_SUCCESS)
            laik_shmem_panic(err);
        err = shmem_send(dst->data, count[1], sizeof(char), i);
        if (err != SHMEM_SUCCESS)
            laik_shmem_panic(err);
    }

    // TODO: opt - remove own changes from received ones
    laik_kvs_changes_apply(dst, kvs);

    laik_kvs_changes_free(&recvd);
    laik_kvs_changes_free(&changes);
}


// Secondary backend functionality
int laik_shmem_secondary_init(int primaryRank, int primarySize, int (*send)(int *, int, int),
                         int (*recv)(int *, int, int))
{
    return shmem_secondary_init(primaryRank, primarySize, send, recv);
}

int laik_shmem_secondary_finalize()
{
    return shmem_finalize();
}

bool laik_aseq_replaceWithShmemCalls(Laik_ActionSeq *as)
{
    int size, rank;
    shmem_comm_size(&size);
    shmem_comm_rank(&rank);
    int *colours = malloc(size * sizeof(int));
    shmem_get_colours(colours);
    int *secondaryRanks = malloc(size * sizeof(int));
    shmem_get_secondaryRanks(secondaryRanks);

    bool ret = false;

    Laik_Action *a = as->action;
    for (unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a))
    {
        switch (a->type)
        {
        case LAIK_AT_MapSend:
        {
            Laik_BackendAction *ba = (Laik_BackendAction *)a;
            if(colours[rank] == colours[ba->rank]){
                ba->rank = secondaryRanks[ba->rank];
                a->type = LAIK_AT_ShmemMapSend;
                ret = true;
            }
            break;
        }
        case LAIK_AT_RBufSend:
        {
            Laik_A_RBufSend *aa = (Laik_A_RBufSend *)a;
            if(colours[rank] == colours[aa->to_rank]){
                aa->to_rank = secondaryRanks[aa->to_rank];
                a->type = LAIK_AT_ShmemRBufSend;
                ret = true;
            }
            break;
        }
        case LAIK_AT_BufSend:
        {
            Laik_A_BufSend *aa = (Laik_A_BufSend *)a;
            if(colours[rank] == colours[aa->to_rank]){
                aa->to_rank = secondaryRanks[aa->to_rank];
                a->type = LAIK_AT_ShmemBufSend;
                ret = true;
            }
            break;
        }
        case LAIK_AT_MapRecv:
        {
            Laik_BackendAction *ba = (Laik_BackendAction *)a;
            if(colours[rank] == colours[ba->rank]){
                ba->rank = secondaryRanks[ba->rank];
                a->type = LAIK_AT_ShmemMapRecv;
                ret = true;
            }
            break;
        }
        case LAIK_AT_RBufRecv:
        {
            Laik_A_RBufRecv *aa = (Laik_A_RBufRecv *)a;
            if(colours[rank] == colours[aa->from_rank]){
                aa->from_rank = secondaryRanks[aa->from_rank];
                a->type = LAIK_AT_ShmemRBufRecv;
                ret = true;
            }
            break;
        }
        case LAIK_AT_BufRecv:
        {
            Laik_A_BufRecv *aa = (Laik_A_BufRecv *)a;
            if(colours[rank] == colours[aa->from_rank]){
                aa->from_rank = secondaryRanks[aa->from_rank];
                a->type = LAIK_AT_ShmemBufRecv;
                ret = true;
            }
            break;
        }
        case LAIK_AT_MapPackAndSend:
        {
            Laik_A_MapPackAndSend *aa = (Laik_A_MapPackAndSend *)a;
            if(colours[rank] == colours[aa->to_rank]){
                aa->to_rank = secondaryRanks[aa->to_rank];
                a->type = LAIK_AT_ShmemMapPackAndSend;
                ret = true;
            }
            break;
        }
        case LAIK_AT_PackAndSend:
        {
            Laik_BackendAction *ba = (Laik_BackendAction *)a;
            if(colours[rank] == colours[ba->rank]){
                ba->rank = secondaryRanks[ba->rank];
                a->type = LAIK_AT_ShmemPackAndSend;
                ret = true;
            }
            break;
        }
        case LAIK_AT_MapRecvAndUnpack:
        {
            Laik_A_MapRecvAndUnpack *aa = (Laik_A_MapRecvAndUnpack *)a;
            if(colours[rank] == colours[aa->from_rank]){
                aa->from_rank = secondaryRanks[aa->from_rank];
                a->type = LAIK_AT_ShmemMapRecvAndUnpack;
                ret = true;
            }
            break;
        }
        case LAIK_AT_RecvAndUnpack:
        {
            Laik_BackendAction *ba = (Laik_BackendAction *)a;
            if(colours[rank] == colours[ba->rank]){
                ba->rank = secondaryRanks[ba->rank];
                a->type = LAIK_AT_ShmemRecvAndUnpack;
                ret = true;
            }
            break;
        }
        default:
            break;
        }
    }
    
    free(colours);
    free(secondaryRanks);
    return ret;
}

bool laik_shmem_secondary_exec(Laik_ActionSeq *as, Laik_Action *a)
{
    // TODO: use transition context given by each action
    Laik_TransitionContext *tc = as->context[0];
    Laik_MappingList *fromList = tc->fromList;
    Laik_MappingList *toList = tc->toList;
    int elemsize = tc->data->elemsize;

    int err, count;
    int dType = getSHMEMDataType(tc->data);

    Laik_BackendAction *ba = (Laik_BackendAction *)a;

    int rank;
    shmem_comm_rank(&rank);

    switch (a->type)
    {
    case LAIK_AT_ShmemMapSend:
    {
        assert(ba->fromMapNo < fromList->count);
        Laik_Mapping *fromMap = &(fromList->map[ba->fromMapNo]);
        assert(fromMap->base != 0);
        err = shmem_send(fromMap->base + ba->offset, ba->count, dType, ba->rank);
        if (err != SHMEM_SUCCESS)
            laik_shmem_panic(err);
        break;
    }
    case LAIK_AT_ShmemRBufSend:
    {
        Laik_A_RBufSend *aa = (Laik_A_RBufSend *)a;
        assert(aa->bufID < ASEQ_BUFFER_MAX);
        err = shmem_send(as->buf[aa->bufID] + aa->offset, aa->count, dType, aa->to_rank);
        if (err != SHMEM_SUCCESS)
            laik_shmem_panic(err);
        break;
    }
    case LAIK_AT_ShmemBufSend:
    {
        Laik_A_BufSend* aa = (Laik_A_BufSend*) a;
        err = shmem_send(aa->buf, aa->count, dType, aa->to_rank);
        if (err != SHMEM_SUCCESS) 
            laik_shmem_panic(err);
        break;
    }
    case LAIK_AT_ShmemMapRecv:
    {
        assert(ba->toMapNo < toList->count);
        Laik_Mapping *toMap = &(toList->map[ba->toMapNo]);
        assert(toMap->base != 0);
        err = shmem_recv(toMap->base + ba->offset, ba->count, dType, ba->rank, &count);
        if (err != SHMEM_SUCCESS)
            laik_shmem_panic(err);
        assert((int)ba->count == count);
        break;
    }
    case LAIK_AT_ShmemRBufRecv:
    {
        Laik_A_RBufRecv *aa = (Laik_A_RBufRecv *)a;
        assert(aa->bufID < ASEQ_BUFFER_MAX);
        err = shmem_recv(as->buf[aa->bufID] + aa->offset, aa->count, dType, aa->from_rank, &count);
        if (err != SHMEM_SUCCESS)
            laik_shmem_panic(err);
        assert((int)ba->count == count);
        break;
    }
    case LAIK_AT_ShmemBufRecv:
    {
        Laik_A_BufRecv* aa = (Laik_A_BufRecv*) a;
        err = shmem_recv(aa->buf, aa->count, dType, aa->from_rank, &count);
        if (err != SHMEM_SUCCESS) 
            laik_shmem_panic(err);
        assert((int)ba->count == count);
        break;
    }
    case LAIK_AT_ShmemMapPackAndSend:
    {
        Laik_A_MapPackAndSend *aa = (Laik_A_MapPackAndSend *)a;
        assert(aa->fromMapNo < fromList->count);
        Laik_Mapping *fromMap = &(fromList->map[aa->fromMapNo]);
        assert(fromMap->base != 0);
        laik_shmem_exec_packAndSend(fromMap, aa->range, aa->to_rank, aa->count, dType);
        break;
    }
    case LAIK_AT_ShmemPackAndSend:
    {
        laik_shmem_exec_packAndSend(ba->map, ba->range, ba->rank, (uint64_t)ba->count, dType);
        break;
    }
    case LAIK_AT_ShmemMapRecvAndUnpack:
    {
        Laik_A_MapRecvAndUnpack *aa = (Laik_A_MapRecvAndUnpack *)a;
        assert(aa->toMapNo < toList->count);
        Laik_Mapping *toMap = &(toList->map[aa->toMapNo]);
        assert(toMap->base);
        laik_shmem_exec_recvAndUnpack(toMap, aa->range, aa->from_rank, aa->count, elemsize, dType);
        break;
    }
    case LAIK_AT_ShmemRecvAndUnpack:
    {
        laik_shmem_exec_recvAndUnpack(ba->map, ba->range, ba->rank, (uint64_t)ba->count, elemsize, dType);
        break;
    }
    default:
        laik_log(LAIK_LL_Panic, "shmem_secondary_exec: no idea how to exec action %d (%s)",
                     a->type, laik_at_str(a->type));
        return false;
    }
    return true;
}

bool laik_shmem_log_action(Laik_Action *a){
    switch (a->type)
    {
    case LAIK_AT_ShmemMapSend:
    {
        laik_log_append("ShmemMapSend");
        break;
    }
    case LAIK_AT_ShmemRBufSend:
    {
        Laik_A_RBufSend* aa = (Laik_A_RBufSend*) a;
        laik_log_append(": from buf %d, off %lld, count %d ==> T%d (shmem)",
                        aa->bufID, (long long int) aa->offset,
                        aa->count,
                        aa->to_rank);
        break;
    }
    case LAIK_AT_ShmemBufSend:
    {
        Laik_A_BufSend* aa = (Laik_A_BufSend*) a;
        laik_log_append(": from %p, count %d ==> T%d (shmem)", 
                        aa->buf, 
                        aa->count, 
                        aa->to_rank);
        break;
    }
    case LAIK_AT_ShmemMapRecv:
    {
        laik_log_append("ShmemMapRecv");
        break;
    }
    case LAIK_AT_ShmemRBufRecv:
    {
        Laik_A_RBufRecv* aa = (Laik_A_RBufRecv*) a;
        laik_log_append(": T%d ==> to buf %d, off %lld, count %d (shmem)",
                        aa->from_rank,
                        aa->bufID, (long long int) aa->offset,
                        aa->count);
        break;
    }
    case LAIK_AT_ShmemBufRecv:
    {
        Laik_A_BufRecv* aa = (Laik_A_BufRecv*) a;
        laik_log_append(": T%d ==> to %p, count %d (shmem)",
                        aa->from_rank,
                        aa->buf,
                        aa->count);
        break;
    }
    case LAIK_AT_ShmemMapPackAndSend:
    {
        Laik_A_MapPackAndSend* aa = (Laik_A_MapPackAndSend*) a;
        laik_log_append(": ");
        laik_log_Range(aa->range);
        laik_log_append(" mapNo %d, count %llu ==> T%d (shmem)",
                        aa->fromMapNo, (unsigned long long) aa->count, aa->to_rank);
        break;
    }
    case LAIK_AT_ShmemPackAndSend:
    {
        laik_log_append("ShmemPackAndSend");
        break;
    }
    case LAIK_AT_ShmemMapRecvAndUnpack:
    {
        Laik_A_MapRecvAndUnpack* aa = (Laik_A_MapRecvAndUnpack*) a;
        laik_log_append(": T%d ==> ", aa->from_rank);
        laik_log_Range(aa->range);
        laik_log_append(" mapNo %d, count %llu (shmem)",
                        aa->toMapNo, (unsigned long long) aa->count);
        break;
    }
    case LAIK_AT_ShmemRecvAndUnpack:
    {
        laik_log_append("ShmemPackAndSend");
        break;
    }
    default:
        return false;
    }
    return true;
}
#endif // USE_SHMEM