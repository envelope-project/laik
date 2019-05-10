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

#define USE_MPI 1
#ifdef USE_MPI

#include "laik-internal.h"
#include "laik-backend-mpi.h"

#include <assert.h>
#include <stdlib.h>
#include <mpi.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

// forward decls, types/structs , global variables

static void laik_mpi_finalize();
static void laik_mpi_prepare(Laik_ActionSeq*);
static void laik_mpi_cleanup(Laik_ActionSeq*);
static void laik_mpi_exec(Laik_ActionSeq* as);
static void laik_mpi_updateGroup(Laik_Group*);


// C guarantees that unset function pointers are NULL
static Laik_Backend laik_backend_mpi = {
    .name        = "MPI Backend Driver (synchronous)",
    .finalize    = laik_mpi_finalize,
    .prepare     = laik_mpi_prepare,
    .cleanup     = laik_mpi_cleanup,
    .exec        = laik_mpi_exec,
    .updateGroup = laik_mpi_updateGroup,
    .get_rank_by_nodes = laik_mpi_get_rank_by_node
};

static Laik_Instance* mpi_instance = 0;

typedef struct _MPIData MPIData;
struct _MPIData {
    MPI_Comm comm;
    bool didInit;
};

typedef struct _MPIGroupData MPIGroupData;
struct _MPIGroupData {
    MPI_Comm comm;
};

//----------------------------------------------------------------
// MPI backend behavior configurable by environment variables

// LAIK_MPI_REDUCE: make use of MPI_(All)Reduce? Default: Yes
// If not, we do own algorithm with send/recv.
static int mpi_reduce = 1;


//----------------------------------------------------------------
// buffer space for messages if packing/unpacking from/to not-1d layout
// is necessary
#define PACKBUFSIZE (10*1024*1024)
//#define PACKBUFSIZE (10*800)
static char packbuf[PACKBUFSIZE];

Laik_Instance* laik_init_mpi(int* argc, char*** argv)
{
    if (mpi_instance) return mpi_instance;

    MPIData* d = malloc(sizeof(MPIData));
    if (!d) {
        laik_panic("Out of memory allocating MPIData object");
        exit(1); // not actually needed, laik_panic never returns
    }
    d->didInit = false;

    MPIGroupData* gd = malloc(sizeof(MPIGroupData));
    if (!gd) {
        laik_panic("Out of memory allocating MPIGroupData object");
        exit(1); // not actually needed, laik_panic never returns
    }

    // eventually initialize MPI first before accessing MPI_COMM_WORLD
    if (argc) {
        MPI_Init(argc, argv);
        d->didInit = true;
    }

    // now finish initilization of <gd>/<d>, as MPI_Init is run
    gd->comm = MPI_COMM_WORLD;
    d->comm = MPI_COMM_WORLD;


    int size, rank;
    MPI_Comm_size(d->comm, &size);
    MPI_Comm_rank(d->comm, &rank);

    // Get the name of the processor
    char processor_name[MPI_MAX_PROCESSOR_NAME];
    int name_len;
    MPI_Get_processor_name(processor_name, &name_len);

    Laik_Instance* inst;
    inst = laik_new_instance(&laik_backend_mpi, size, rank,
                             processor_name, d, gd);

    sprintf(inst->guid, "%d", rank);

    laik_log(2, "MPI backend initialized (at %s:%d, rank %d/%d)\n",
             inst->mylocation, (int) getpid(),
             rank, size);

    // do own reduce algorithm?
    char* str = getenv("LAIK_MPI_REDUCE");
    if (str) mpi_reduce = atoi(str);

    mpi_instance = inst;
    return inst;
}

static
MPIData* mpiData(Laik_Instance* i)
{
    return (MPIData*) i->backend_data;
}

static
MPIGroupData* mpiGroupData(Laik_Group* g)
{
    return (MPIGroupData*) g->backend_data;
}

static
void laik_mpi_finalize()
{
    if (mpiData(mpi_instance)->didInit)
        MPI_Finalize();
}

// update backend specific data for group if needed
static
void laik_mpi_updateGroup(Laik_Group* g)
{
    // calculate MPI communicator for group <g>
    // TODO: only supports shrinking of parent for now
    assert(g->parent);
    assert(g->parent->size > g->size);

    laik_log(1, "MPI backend updateGroup: parent %d (size %d, myid %d) "
             "=> group %d (size %d, myid %d)",
             g->parent->gid, g->parent->size, g->parent->myid,
             g->gid, g->size, g->myid);

    // only interesting if this task is still part of parent
    if (g->parent->myid < 0) return;

    MPIGroupData* gdParent = (MPIGroupData*) g->parent->backend_data;
    assert(gdParent);

    MPIGroupData* gd = (MPIGroupData*) g->backend_data;
    assert(gd == 0); // must not be updated yet
    gd = malloc(sizeof(MPIGroupData));
    if (!gd) {
        laik_panic("Out of memory allocating MPIGroupData object");
        exit(1); // not actually needed, laik_panic never returns
    }
    g->backend_data = gd;

    laik_log(1, "MPI Comm_split: old myid %d => new myid %d",
             g->parent->myid, g->fromParent[g->parent->myid]);

    MPI_Comm_split(gdParent->comm, g->myid < 0 ? MPI_UNDEFINED : 0, g->myid,
        &(gd->comm));
}

static
MPI_Datatype getMPIDataType(Laik_Data* d)
{
    MPI_Datatype mpiDataType;
    if      (d->type == laik_Double) mpiDataType = MPI_DOUBLE;
    else if (d->type == laik_Float)  mpiDataType = MPI_FLOAT;
    else if (d->type == laik_Int64)  mpiDataType = MPI_INT64_T;
    else if (d->type == laik_Int32)  mpiDataType = MPI_INT32_T;
    else if (d->type == laik_Char)   mpiDataType = MPI_INT8_T;
    else if (d->type == laik_UInt64) mpiDataType = MPI_UINT64_T;
    else if (d->type == laik_UInt32) mpiDataType = MPI_UINT32_T;
    else if (d->type == laik_UChar)  mpiDataType = MPI_UINT8_T;
    else assert(0);

    return mpiDataType;
}

static
MPI_Op getMPIOp(Laik_ReductionOperation redOp)
{
    MPI_Op mpiRedOp;
    switch(redOp) {
    case LAIK_RO_Sum:  mpiRedOp = MPI_SUM; break;
    case LAIK_RO_Prod: mpiRedOp = MPI_PROD; break;
    case LAIK_RO_Min:  mpiRedOp = MPI_MIN; break;
    case LAIK_RO_Max:  mpiRedOp = MPI_MAX; break;
    case LAIK_RO_And:  mpiRedOp = MPI_LAND; break;
    case LAIK_RO_Or:   mpiRedOp = MPI_LOR; break;
    default: assert(0);
    }
    return mpiRedOp;
}

static
void laik_mpi_exec_pack(Laik_BackendAction* a, Laik_Mapping* map)
{
    Laik_Index idx = a->slc->from;
    int dims = a->slc->space->dims;
    int byteCount = a->count * map->data->elemsize;
    int packed = (map->layout->pack)(map, a->slc, &idx, a->toBuf, byteCount);
    assert(packed == a->count);
    assert(laik_index_isEqual(dims, &idx, &(a->slc->to)));
}

static
void laik_mpi_exec_packAndSend(Laik_Mapping* map, Laik_Slice* slc,
                               int to_rank, uint64_t slc_size,
                               MPI_Datatype dataType, int tag, MPI_Comm comm)
{
    Laik_Index idx = slc->from;
    int dims = slc->space->dims;
    int packed;
    uint64_t count = 0;
    while(1) {
        packed = (map->layout->pack)(map, slc, &idx,
                                     packbuf, PACKBUFSIZE);
        assert(packed > 0);
        MPI_Send(packbuf, packed,
                 dataType, to_rank, tag, comm);
        count += packed;
        if (laik_index_isEqual(dims, &idx, &(slc->to))) break;
    }
    assert(count == slc_size);
}

static
void laik_mpi_exec_unpack(Laik_BackendAction* a, Laik_Mapping* map)
{
    Laik_Index idx = a->slc->from;
    int dims = a->slc->space->dims;
    int byteCount = a->count * map->data->elemsize;
    int unpacked = (map->layout->unpack)(map, a->slc, &idx,
                                         a->fromBuf, byteCount);
    assert(unpacked == a->count);
    assert(laik_index_isEqual(dims, &idx, &(a->slc->to)));
}

static
void laik_mpi_exec_recvAndUnpack(Laik_Mapping* map, Laik_Slice* slc,
                                 int from_rank, uint64_t slc_size,
                                 int elemsize,
                                 MPI_Datatype dataType, int tag, MPI_Comm comm)
{
    MPI_Status st;
    Laik_Index idx = slc->from;
    int dims = slc->space->dims;
    int recvCount, unpacked;
    uint64_t count = 0;
    while(1) {
        MPI_Recv(packbuf, PACKBUFSIZE / elemsize,
                 dataType, from_rank, tag, comm, &st);
        MPI_Get_count(&st, dataType, &recvCount);
        unpacked = (map->layout->unpack)(map, slc, &idx,
                                         packbuf, recvCount * elemsize);
        assert(recvCount == unpacked);
        count += unpacked;
        if (laik_index_isEqual(dims, &idx, &(slc->to))) break;
    }
    assert(count == slc_size);
}

static
void laik_mpi_exec_reduce(Laik_TransitionContext* tc, Laik_BackendAction* a,
                          MPI_Datatype dataType, MPI_Comm comm)
{
    assert(mpi_reduce > 0);

    MPI_Op mpiRedOp = getMPIOp(a->redOp);
    int rootTask = a->rank;

    if (rootTask == -1) {
        if (a->fromBuf == a->toBuf) {
            laik_log(1, "      exec MPI_Allreduce in-place, count %d", a->count);
            MPI_Allreduce(MPI_IN_PLACE, a->toBuf, a->count,
                          dataType, mpiRedOp, comm);
        }
        else {
            laik_log(1, "      exec MPI_Allreduce, count %d", a->count);
            MPI_Allreduce(a->fromBuf, a->toBuf, a->count,
                          dataType, mpiRedOp, comm);
        }
    }
    else {
        if ((a->fromBuf == a->toBuf) && (tc->transition->group->myid == rootTask)) {
            laik_log(1, "      exec MPI_Reduce in-place, count %d, root %d",
                     a->count, rootTask);
            MPI_Reduce(MPI_IN_PLACE, a->toBuf, a->count,
                       dataType, mpiRedOp, rootTask, comm);
        }
        else {
            laik_log(1, "      exec MPI_Reduce, count %d, root %d", a->count, rootTask);
            MPI_Reduce(a->fromBuf, a->toBuf, a->count,
                       dataType, mpiRedOp, rootTask, comm);
        }
    }
}

// a naive, manual reduction using send/recv:
// one process is chosen to do the reduction: the smallest rank from processes
// which are interested in the result. All other processes with input
// send their data to him, he does the reduction, and sends to all processes
// interested in the result
static
void laik_mpi_exec_groupReduce(Laik_TransitionContext* tc,
                               Laik_BackendAction* a,
                               MPI_Datatype dataType, MPI_Comm comm)
{
    assert(a->h.type == LAIK_AT_GroupReduce);
    Laik_Transition* t = tc->transition;
    Laik_Data* data = tc->data;

    // do the manual reduction on smallest rank of output group
    int reduceTask = laik_trans_taskInGroup(t, a->outputGroup, 0);
    laik_log(1, "      exec reduce at T%d", reduceTask);

    int myid = t->group->myid;
    MPI_Status st;

    if (myid != reduceTask) {
        // not the reduce task: eventually send input and recv result

        if (laik_trans_isInGroup(t, a->inputGroup, myid)) {
            laik_log(1, "        exec MPI_Send to T%d", reduceTask);
            MPI_Send(a->fromBuf, a->count, dataType, reduceTask, 1, comm);
        }
        if (laik_trans_isInGroup(t, a->outputGroup, myid)) {
            laik_log(1, "        exec MPI_Recv from T%d", reduceTask);
            MPI_Recv(a->toBuf, a->count, dataType, reduceTask, 1, comm, &st);
        }
        return;
    }

    // we are the reduce task
    int inCount = laik_trans_groupCount(t, a->inputGroup);
    uint64_t byteCount = a->count * data->elemsize;
    bool inputFromMe = laik_trans_isInGroup(t, a->inputGroup, myid);

    // for direct execution: use global <packbuf> (size PACKBUFSIZE)
    // check that bufsize is enough. TODO: dynamically increase?
    int bufSize = (inCount - (inputFromMe ? 1:0)) * byteCount;
    assert(bufSize < PACKBUFSIZE);

    // collect values from tasks in input group
    int bufOff[32], off = 0;
    assert(inCount <= 32);

    // always put this task in front: we use toBuf to calculate
    // our results, but there may be input from us, which would
    // be overwritten if not starting with our input
    int ii = 0;
    if (inputFromMe) {
        ii++; // slot 0 reserved for this task (use a->fromBuf)
        bufOff[0] = 0;
    }
    for(int i = 0; i< inCount; i++) {
        int inTask = laik_trans_taskInGroup(t, a->inputGroup, i);
        if (inTask == myid) continue;

        laik_log(1, "        exec MPI_Recv from T%d (buf off %d, count %d)",
                 inTask, off, a->count);

        bufOff[ii++] = off;
        MPI_Recv(packbuf + off, a->count, dataType, inTask, 1, comm, &st);
        off += byteCount;
    }
    assert(ii == inCount);
    assert(off == bufSize);

    // do the reduction, put result back to my input buffer
    if (data->type->reduce) {
        // reduce with 0/1 inputs by setting input pointer to 0
        char* buf0 = inputFromMe ? a->fromBuf : (packbuf + bufOff[0]);
        (data->type->reduce)(a->toBuf,
                             (inCount < 1) ? 0 : buf0,
                             (inCount < 2) ? 0 : (packbuf + bufOff[1]),
                a->count, a->redOp);
        for(int t = 2; t < inCount; t++)
            (data->type->reduce)(a->toBuf, a->toBuf, packbuf + bufOff[t],
                                 a->count, a->redOp);
    }
    else {
        laik_log(LAIK_LL_Panic,
                 "Need reduce function for type '%s'. Not set!",
                 data->type->name);
        assert(0);
    }

    // send result to tasks in output group
    int outCount = laik_trans_groupCount(t, a->outputGroup);
    for(int i = 0; i< outCount; i++) {
        int outTask = laik_trans_taskInGroup(t, a->outputGroup, i);
        if (outTask == myid) {
            // that's myself: nothing to do
            continue;
        }

        laik_log(1, "        exec MPI_Send result to T%d", outTask);
        MPI_Send(a->toBuf, a->count, dataType, outTask, 1, comm);
    }
}

static
void laik_mpi_exec(Laik_ActionSeq* as)
{
    if (as->actionCount == 0) {
        laik_log(1, "MPI backend exec: nothing to do\n");
        return;
    }

    if (as->backend == 0) {
        // no preparation: do minimal transformations, sorting send/recv
        laik_log(1, "MPI backend exec: prepare before exec\n");
        laik_log_ActionSeqIfChanged(true, as, "Original sequence");
        bool changed = laik_aseq_splitTransitionExecs(as);
        laik_log_ActionSeqIfChanged(changed, as, "After splitting texecs");
        changed = laik_aseq_flattenPacking(as);
        laik_log_ActionSeqIfChanged(changed, as, "After flattening");
        changed = laik_aseq_allocBuffer(as);
        laik_log_ActionSeqIfChanged(changed, as, "After buffer alloc");
        changed = laik_aseq_sort_2phases(as);
        laik_log_ActionSeqIfChanged(changed, as, "After sorting");
    }

    if (laik_log_begin(1)) {
        laik_log_append("MPI backend exec:\n");
        laik_log_ActionSeq(as, false);
        laik_log_flush(0);
    }

    // TODO: use transition context given by each action
    Laik_TransitionContext* tc = as->context[0];
    Laik_SwitchStat* ss = tc->data->stat;
    Laik_MappingList* fromList = tc->fromList;
    Laik_MappingList* toList = tc->toList;
    int elemsize = tc->data->elemsize;

    // common for all MPI calls: tag, comm, datatype
    int tag = 1;
    MPIGroupData* gd = mpiGroupData(tc->transition->group);
    assert(gd);
    MPI_Comm comm = gd->comm;
    MPI_Datatype dataType = getMPIDataType(tc->data);
    MPI_Status st;

    Laik_Action* a = as->action;
    for(int i = 0; i < as->actionCount; i++, a = nextAction(a)) {
        Laik_BackendAction* ba = (Laik_BackendAction*) a;
        if (laik_log_begin(1)) {
            laik_log_Action(a, as);
            laik_log_flush(0);
        }

        switch(a->type) {
        case LAIK_AT_BufReserve:
        case LAIK_AT_Nop:
            // no need to do anything
            break;

        case LAIK_AT_MapSend: {
            assert(ba->fromMapNo < fromList->count);
            Laik_Mapping* fromMap = &(fromList->map[ba->fromMapNo]);
            assert(fromMap->base != 0);
            MPI_Send(fromMap->base + ba->offset, ba->count,
                     dataType, ba->rank, tag, comm);
            break;
        }

        case LAIK_AT_RBufSend: {
            Laik_A_RBufSend* aa = (Laik_A_RBufSend*) a;
            assert(aa->bufID < ASEQ_BUFFER_MAX);
            MPI_Send(as->buf[aa->bufID] + aa->offset, aa->count,
                     dataType, aa->to_rank, tag, comm);
            break;
        }

        case LAIK_AT_BufSend: {
            Laik_A_BufSend* aa = (Laik_A_BufSend*) a;
            MPI_Send(aa->buf, aa->count,
                     dataType, aa->to_rank, tag, comm);
            break;
        }

        case LAIK_AT_MapRecv: {
            assert(ba->toMapNo < toList->count);
            Laik_Mapping* toMap = &(toList->map[ba->toMapNo]);
            assert(toMap->base != 0);
            MPI_Recv(toMap->base + ba->offset, ba->count,
                     dataType, ba->rank, tag, comm, &st);
            break;
        }

        case LAIK_AT_RBufRecv: {
            Laik_A_RBufRecv* aa = (Laik_A_RBufRecv*) a;
            assert(aa->bufID < ASEQ_BUFFER_MAX);
            MPI_Recv(as->buf[aa->bufID] + aa->offset, aa->count,
                     dataType, aa->from_rank, tag, comm, &st);
            break;
        }

        case LAIK_AT_BufRecv: {
            Laik_A_BufRecv* aa = (Laik_A_BufRecv*) a;
            MPI_Recv(aa->buf, aa->count,
                     dataType, aa->from_rank, tag, comm, &st);
            break;
        }

        case LAIK_AT_CopyFromBuf:
            for(int i = 0; i < ba->count; i++)
                memcpy(ba->ce[i].ptr,
                       ba->fromBuf + ba->ce[i].offset,
                       ba->ce[i].bytes);
            break;

        case LAIK_AT_CopyToBuf:
            for(int i = 0; i < ba->count; i++)
                memcpy(ba->toBuf + ba->ce[i].offset,
                       ba->ce[i].ptr,
                       ba->ce[i].bytes);
            break;

        case LAIK_AT_PackToBuf:
            laik_mpi_exec_pack(ba, ba->map);
            break;

        case LAIK_AT_MapPackToBuf: {
            assert(ba->fromMapNo < fromList->count);
            Laik_Mapping* fromMap = &(fromList->map[ba->fromMapNo]);
            assert(fromMap->base != 0);
            laik_mpi_exec_pack(ba, fromMap);
            break;
        }

        case LAIK_AT_UnpackFromBuf:
            laik_mpi_exec_unpack(ba, ba->map);
            break;

        case LAIK_AT_MapUnpackFromBuf: {
            assert(ba->toMapNo < toList->count);
            Laik_Mapping* toMap = &(toList->map[ba->toMapNo]);
            assert(toMap->base);
            laik_mpi_exec_unpack(ba, toMap);
            break;
        }


        case LAIK_AT_MapPackAndSend: {
            Laik_A_MapPackAndSend* aa = (Laik_A_MapPackAndSend*) a;
            assert(aa->fromMapNo < fromList->count);
            Laik_Mapping* fromMap = &(fromList->map[aa->fromMapNo]);
            assert(fromMap->base != 0);
            laik_mpi_exec_packAndSend(fromMap, aa->slc, aa->to_rank, aa->count,
                                      dataType, tag, comm);
            break;
        }

        case LAIK_AT_PackAndSend:
            laik_mpi_exec_packAndSend(ba->map, ba->slc, ba->rank,
                                      (uint64_t) ba->count,
                                      dataType, tag, comm);
            break;

        case LAIK_AT_MapRecvAndUnpack: {
            Laik_A_MapRecvAndUnpack* aa = (Laik_A_MapRecvAndUnpack*) a;
            assert(aa->toMapNo < toList->count);
            Laik_Mapping* toMap = &(toList->map[aa->toMapNo]);
            assert(toMap->base);
            laik_mpi_exec_recvAndUnpack(toMap, aa->slc, aa->from_rank, aa->count,
                                        elemsize, dataType, tag, comm);
            break;
        }

        case LAIK_AT_RecvAndUnpack:
            laik_mpi_exec_recvAndUnpack(ba->map, ba->slc, ba->rank,
                                        (uint64_t) ba->count,
                                        elemsize, dataType, tag, comm);
            break;

        case LAIK_AT_Reduce:
            laik_mpi_exec_reduce(tc, ba, dataType, comm);
            break;

        case LAIK_AT_GroupReduce:
            laik_mpi_exec_groupReduce(tc, ba, dataType, comm);
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

        default:
            laik_log(LAIK_LL_Panic, "mpi_exec: no idea how to exec action %d (%s)",
                     a->type, laik_at_str(a->type));
            assert(0);
        }
    }
    assert( ((char*)as->action) + as->bytesUsed == ((char*)a) );

    ss->sentBytes     += as->sendCount * tc->data->elemsize;
    ss->receivedBytes += as->recvCount * tc->data->elemsize;
    ss->reducedBytes  += as->reduceCount * tc->data->elemsize;
}

static
void laik_mpi_prepare(Laik_ActionSeq* as)
{
    if (laik_log_begin(1)) {
        laik_log_append("MPI backend prepare:\n");
        laik_log_ActionSeq(as, false);
        laik_log_flush(0);
    }

    bool changed = laik_aseq_splitTransitionExecs(as);
    laik_log_ActionSeqIfChanged(changed, as, "After splitting transition execs");
    if (as->actionCount == 0) return;

    changed = laik_aseq_flattenPacking(as);
    laik_log_ActionSeqIfChanged(changed, as, "After flattening actions");

    if (mpi_reduce) {
        // detect group reduce actions which can be replaced by all-reduce
        // can be prohibited by setting LAIK_MPI_REDUCE=0
        changed = laik_aseq_replaceWithAllReduce(as);
        laik_log_ActionSeqIfChanged(changed, as, "After all-reduce detection");
    }

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
    //changed = laik_aseq_sort_rankdigits(as);
    laik_log_ActionSeqIfChanged(changed, as, "After sorting for deadlock avoidance");

    laik_aseq_freeTempSpace(as);

    // request for cleanup function to be called
    // (not really needed at the moment)
    as->backend = &laik_backend_mpi;
}

static void laik_mpi_cleanup(Laik_ActionSeq* as)
{
    if (laik_log_begin(1)) {
        laik_log_append("MPI backend cleanup:\n");
        laik_log_ActionSeq(as, false);
        laik_log_flush(0);
    }

    assert(as->backend == &laik_backend_mpi);
}


#define MAX_RANKS 65536
#define MAX_NODE_NAME_LENGTH 512
int laik_mpi_get_rank_by_node(Laik_Instance* inst, char** nodes, size_t sznodes, int** list, size_t* szlist){
    
    //all gather
    //map. 

    char* my_loc = laik_mylocation(inst);
    int my_rank = atoi(inst->guid);
    
#pragma pack(1)
    struct rank_loc_pair{
        char location[MAX_NODE_NAME_LENGTH];
        int rank;
    };
#pragma pack() 

    struct rank_loc_pair myid;
    struct rank_loc_pair* all_ids;

    strncpy(myid.location, my_loc, sizeof(myid.location));
    myid.rank = my_rank;

    //all gather    
    all_ids = (char*) malloc ((inst->size) * sizeof(struct rank_loc_pair));
    MPI_Allgather(&myid, sizeof(struct rank_loc_pair), MPI_BYTE, 
                  all_ids, sizeof(struct rank_loc_pair), MPI_BYTE, MPI_COMM_WORLD);
     
    int ranks_buf[65536];
    int count = 0;
    //test the failed ranks
    //FIXME: High complexity
    laik_log(LAIK_LL_Debug, "#all ids %d\n", inst->size);

    for(size_t i=0; i<sznodes; i++){
        for(int j=0; j<inst->size; j++){
        laik_log(LAIK_LL_Debug, "#all ids %d\n", inst->size);
            laik_log(LAIK_LL_Debug, "Rank/loc %d/%s\n", all_ids[j].rank, all_ids[j].location);
            if(strncmp(all_ids[j].location, nodes[i], MAX_NODE_NAME_LENGTH-1) == 0){
                ranks_buf[count] = all_ids[j].rank;
                count++;
            }
        }
    }

    *list = (int*) malloc(count*sizeof(int));
    memcpy(*list, ranks_buf, count*sizeof(int));
    *szlist = count;

    return count;
}

#endif // USE_MPI
