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
static Laik_ActionSeq* laik_mpi_prepare(Laik_Data* d, Laik_Transition* t,
                                        Laik_MappingList *fromList, Laik_MappingList *toList);
static void laik_mpi_cleanup(Laik_ActionSeq*);
static void laik_mpi_exec(Laik_Data* d, Laik_Transition* t, Laik_ActionSeq* tp,
                          Laik_MappingList* from, Laik_MappingList* to);
static void laik_mpi_wait(Laik_ActionSeq* as, int mapNo);
static bool laik_mpi_probe(Laik_ActionSeq* as, int mapNo);
static void laik_mpi_updateGroup(Laik_Group*);

// C guarantees that unset function pointers are NULL
static Laik_Backend laik_backend_mpi = {
    .name        = "MPI Backend Driver (synchronous)",
    .finalize    = laik_mpi_finalize,
    .prepare     = laik_mpi_prepare,
    .cleanup     = laik_mpi_cleanup,
    .exec        = laik_mpi_exec,
    .wait        = laik_mpi_wait,
    .probe       = laik_mpi_probe,
    .updateGroup = laik_mpi_updateGroup
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

// intentially make MPI backend buggy by setting LAIK_MPI_BUG=1
// useful to ensure that a test is sentitive to backend bugs
static int mpi_bug = 0;

//----------------------------------------------------------------
// MPI backend behavior configurable by environment variables

// LAIK_MPI_REDUCE: make use of MPI_(All)Reduce? Default: Yes
// If not, we do own algorithm with send/recv.
static int mpi_reduce = 1;


//----------------------------------------------------------------
// buffer space for messages if packing/unpacking from/to not-1d layout
// is necessary
// TODO: if we go to asynchronous messages, this needs to be dynamic per data
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

    laik_log(1, "MPI backend initialized (location '%s', pid %d)\n",
             inst->mylocation, (int) getpid());

    // for intentionally buggy MPI backend behavior
    char* str = getenv("LAIK_MPI_BUG");
    if (str) mpi_bug = atoi(str);

    // do own reduce algorithm?
    str = getenv("LAIK_MPI_REDUCE");
    if (str) mpi_reduce = atoi(str);

    // wait for debugger to attach?
    char* rstr = getenv("LAIK_DEBUG_RANK");
    if (rstr) {
        int wrank = atoi(rstr);
        if ((wrank < 0) || (wrank == rank)) {
            // as long as "wait" is 1, wait in loop for debugger
            volatile int wait = 1;
            while(wait) { usleep(10000); }
        }
    }

    mpi_instance = inst;
    return inst;
}

static MPIData* mpiData(Laik_Instance* i)
{
    return (MPIData*) i->backend_data;
}

static MPIGroupData* mpiGroupData(Laik_Group* g)
{
    return (MPIGroupData*) g->backend_data;
}

static void laik_mpi_finalize()
{
    if (mpiData(mpi_instance)->didInit)
        MPI_Finalize();
}

// update backend specific data for group if needed
static void laik_mpi_updateGroup(Laik_Group* g)
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
    else if (d->type == laik_Float) mpiDataType = MPI_FLOAT;
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
    int byteCount = a->count * map->data->elemsize;
    int packed = (map->layout->pack)(map, a->slc, &idx, a->toBuf, byteCount);
    assert(packed == a->count);
    assert(laik_index_isEqual(a->dims, &idx, &(a->slc->to)));
}

static
void laik_mpi_exec_packAndSend(Laik_BackendAction* a, Laik_Mapping* map, int dims,
                               MPI_Datatype dataType, int tag, MPI_Comm comm)
{
    Laik_Index idx = a->slc->from;
    int packed;
    int count = 0;
    while(1) {
        packed = (map->layout->pack)(map, a->slc, &idx,
                                     packbuf, PACKBUFSIZE);
        assert(packed > 0);
        MPI_Send(packbuf, packed,
                 dataType, a->peer_rank, tag, comm);
        count += packed;
        if (laik_index_isEqual(dims, &idx, &(a->slc->to))) break;
    }
    assert(count == a->count);
}

static
void laik_mpi_exec_unpack(Laik_BackendAction* a, Laik_Mapping* map)
{
    Laik_Index idx = a->slc->from;
    int byteCount = a->count * map->data->elemsize;
    int unpacked = (map->layout->unpack)(map, a->slc, &idx,
                                         a->fromBuf, byteCount);
    assert(unpacked == a->count);
    assert(laik_index_isEqual(a->dims, &idx, &(a->slc->to)));
}

static
void laik_mpi_exec_recvAndUnpack(Laik_BackendAction* a, Laik_Mapping* map,
                                 int dims, int elemsize,
                                 MPI_Datatype dataType, int tag, MPI_Comm comm)
{
    MPI_Status st;
    Laik_Index idx = a->slc->from;
    int recvCount, unpacked;
    int count = 0;
    while(1) {
        MPI_Recv(packbuf, PACKBUFSIZE / elemsize,
                 dataType, a->peer_rank, tag, comm, &st);
        MPI_Get_count(&st, dataType, &recvCount);
        unpacked = (map->layout->unpack)(map, a->slc, &idx,
                                         packbuf, recvCount * elemsize);
        assert(recvCount == unpacked);
        count += unpacked;
        if (laik_index_isEqual(dims, &idx, &(a->slc->to))) break;
    }
    assert(count == a->count);
}

// #define LOG_EXEC_DOUBLE_VALUES

static
void laik_mpi_exec_reduce(Laik_TransitionContext* tc, Laik_BackendAction* a,
                          MPI_Datatype dataType, MPI_Comm comm)
{
    assert(mpi_reduce > 0);

    MPI_Op mpiRedOp = getMPIOp(a->redOp);
    int rootTask = a->peer_rank;

#ifdef LOG_EXEC_DOUBLE_VALUES
    if (a->fromBuf) {
        assert(dataType == MPI_DOUBLE);
        for(int i = 0; i < a->count; i++)
            laik_log(1, "    before at %d: %f", i, ((double*)a->fromBuf)[i]);
    }
#endif

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

#ifdef LOG_EXEC_DOUBLE_VALUES
    if (a->toBuf) {
        assert(dataType == MPI_DOUBLE);
        for(int i = 0; i < a->count; i++)
            laik_log(1, "    before at %d: %f", i, ((double*)a->toBuf)[i]);
    }
#endif

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
    assert(a->type == LAIK_AT_GroupReduce);
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
        if (inTask == myid) {

#ifdef LOG_EXEC_DOUBLE_VALUES
            assert(data->elemsize == 8);
            for(int i = 0; i < a->count; i++)
                laik_log(1, "    have at %d: %f", i,
                         ((double*)a->fromBuf)[i]);
#endif
#ifdef LOG_FLOAT_VALUES
            assert(d->elemsize == 4);
            for(uint64_t i = 0; i < elemCount; i++)
                laik_log(1, "    have at %d: %f", from + i,
                         (double) ((float*)fromBase)[i] );
#endif
            continue;
        }

        laik_log(1, "        exec MPI_Recv from T%d (buf off %d, count %d)",
                 inTask, off, a->count);

        bufOff[ii++] = off;
        MPI_Recv(packbuf + off, a->count, dataType, inTask, 1, comm, &st);

#ifdef LOG_EXEC_DOUBLE_VALUES
        if (laik_log_begin(1)) {
            laik_log_Checksum(packbuf + off, a->count, data->type);
            laik_log_flush(0);
        }

        assert(data->elemsize == 8);
        for(int i = 0; i < a->count; i++)
            laik_log(1, "    got at %d: %f", i,
                     ((double*)(packbuf + off))[i]);
#endif
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

#ifdef LOG_EXEC_DOUBLE_VALUES
    if (laik_log_begin(1)) {
        laik_log_append("reduction (count %d) result ", a->count);
        laik_log_Checksum(a->toBuf, a->count, data->type);
        laik_log_flush(0);
    }

    assert(data->elemsize == 8);
    for(int i = 0; i < a->count; i++)
        laik_log(1, "    sum at %d: %f", i, ((double*)a->toBuf)[i]);
#endif

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
void laik_mpi_exec_actions(Laik_ActionSeq* as, Laik_SwitchStat* ss)
{
    assert(as->actionCount > 0);

    laik_log(1, "MPI backend: exec %d actions\n", as->actionCount);

    // TODO: use transition context given by each action
    Laik_TransitionContext* tc = as->context[0];
    Laik_MappingList* fromList = tc->fromList;
    Laik_MappingList* toList = tc->toList;
    int dims = tc->data->space->dims;
    int elemsize = tc->data->elemsize;

    // common for all MPI calls: tag, comm, datatype
    int tag = 1;
    MPIGroupData* gd = mpiGroupData(tc->transition->group);
    assert(gd);
    MPI_Comm comm = gd->comm;
    MPI_Datatype dataType = getMPIDataType(tc->data);
    MPI_Status st;

    for(int i = 0; i < as->actionCount; i++) {
        Laik_BackendAction* a = &(as->action[i]);
        if (laik_log_begin(1)) {
            laik_log_Action((Laik_Action*) a, tc);
            laik_log_flush(0);
        }

        switch(a->type) {
        case LAIK_AT_BufReserve:
        case LAIK_AT_Nop:
            // no need to do anything
            break;

        case LAIK_AT_MapSend: {
            assert(a->fromMapNo < fromList->count);
            Laik_Mapping* fromMap = &(fromList->map[a->fromMapNo]);
            assert(fromMap->base != 0);
            MPI_Send(fromMap->base + a->offset, a->count,
                     dataType, a->peer_rank, tag, comm);
            break;
        }

        case LAIK_AT_RBufSend:
            assert(a->bufID < ASEQ_BUFFER_MAX);
            MPI_Send(as->buf[a->bufID] + a->offset, a->count,
                     dataType, a->peer_rank, tag, comm);
            break;

        case LAIK_AT_BufSend:
            MPI_Send(a->fromBuf, a->count,
                     dataType, a->peer_rank, tag, comm);
            break;

        case LAIK_AT_MapRecv: {
            assert(a->toMapNo < toList->count);
            Laik_Mapping* toMap = &(toList->map[a->toMapNo]);
            assert(toMap->base != 0);
            MPI_Recv(toMap->base + a->offset, a->count,
                     dataType, a->peer_rank, tag, comm, &st);
            break;
        }

        case LAIK_AT_RBufRecv:
            assert(a->bufID < ASEQ_BUFFER_MAX);
            MPI_Recv(as->buf[a->bufID] + a->offset, a->count,
                     dataType, a->peer_rank, tag, comm, &st);
            break;

        case LAIK_AT_BufRecv:
            MPI_Recv(a->toBuf, a->count,
                     dataType, a->peer_rank, tag, comm, &st);
            break;

        case LAIK_AT_CopyFromBuf:
            for(int i = 0; i < a->count; i++)
                memcpy(a->ce[i].ptr,
                       a->fromBuf + a->ce[i].offset,
                       a->ce[i].bytes);
            break;

        case LAIK_AT_CopyToBuf:
            for(int i = 0; i < a->count; i++)
                memcpy(a->toBuf + a->ce[i].offset,
                       a->ce[i].ptr,
                       a->ce[i].bytes);
            break;

        case LAIK_AT_PackToBuf:
            laik_mpi_exec_pack(a, a->map);
            break;

        case LAIK_AT_UnpackFromBuf:
            laik_mpi_exec_unpack(a, a->map);
            break;

        case LAIK_AT_MapPackAndSend: {
            assert(a->fromMapNo < fromList->count);
            Laik_Mapping* fromMap = &(fromList->map[a->fromMapNo]);
            assert(fromMap->base != 0);
            laik_mpi_exec_packAndSend(a, fromMap, dims, dataType, tag, comm);
            break;
        }

        case LAIK_AT_PackAndSend:
            laik_mpi_exec_packAndSend(a, a->map, dims, dataType, tag, comm);
            break;

        case LAIK_AT_MapRecvAndUnpack: {
            assert(a->toMapNo < toList->count);
            Laik_Mapping* toMap = &(toList->map[a->toMapNo]);
            assert(toMap->base);
            laik_mpi_exec_recvAndUnpack(a, toMap, dims, elemsize, dataType, tag, comm);
            break;
        }

        case LAIK_AT_RecvAndUnpack:
            laik_mpi_exec_recvAndUnpack(a, a->map, dims, elemsize, dataType, tag, comm);
            break;

        case LAIK_AT_Reduce:
            laik_mpi_exec_reduce(tc, a, dataType, comm);
            break;

        case LAIK_AT_GroupReduce:
            laik_mpi_exec_groupReduce(tc, a, dataType, comm);
            break;

        case LAIK_AT_RBufLocalReduce:
            assert(a->bufID < ASEQ_BUFFER_MAX);
            assert(a->dtype->reduce != 0);
            (a->dtype->reduce)(a->toBuf, a->toBuf, as->buf[a->bufID] + a->offset,
                               a->count, a->redOp);
            break;

        case LAIK_AT_RBufCopy:
            assert(a->bufID < ASEQ_BUFFER_MAX);
            memcpy(a->toBuf, as->buf[a->bufID] + a->offset, a->count * elemsize);
            break;

        case LAIK_AT_BufCopy:
            memcpy(a->toBuf, a->fromBuf, a->count * elemsize);
            break;

        default:
            laik_log(LAIK_LL_Panic, "mpi_exec: unknown action %d", a->type);
            assert(0);
        }
    }

    ss->sentBytes     += as->sendCount * tc->data->elemsize;
    ss->receivedBytes += as->recvCount * tc->data->elemsize;
    ss->reducedBytes  += as->reduceCount * tc->data->elemsize;
}



static
void laik_execOrRecord(bool record,
                       Laik_Data *data, Laik_Transition *t, Laik_ActionSeq* as,
                       Laik_MappingList* fromList, Laik_MappingList* toList)
{
    if (record) {
        assert(as && (as->actionCount == 0));

        assert((fromList != 0) && (toList != 0) &&
               "recording without mappings not supported at the moment");
    }

    Laik_Group* group = t->group;
    int myid  = group->myid;
    int dims = data->space->dims;
    Laik_SwitchStat* ss = data->stat;

    laik_log(1, "MPI backend: %s transition (data '%s', group %d:%d/%d)\n"
             "    actions: %d reductions, %d sends, %d recvs",
             record ? "record":"execute",
             data->name, group->gid, myid, group->size,
             t->redCount, t->sendCount, t->recvCount);

    if (myid < 0) {
        // this task is not part of the communicator to use
        return;
    }

    MPIGroupData* gd = mpiGroupData(group);
    assert(gd); // must have been updated by laik_mpi_updateGroup()
    MPI_Comm comm = gd->comm;

    // when recording, just create actions
    // TODO: pull out of backend
    if (record) {
        laik_aseq_addReds(as, 0, data, t);
        laik_aseq_addSends(as, 0, data, t);
        laik_aseq_addRecvs(as, 0, data, t);
        return;
    }

    if (t->redCount > 0) {
        assert(dims == 1);
        assert(fromList);

        for(int i=0; i < t->redCount; i++) {
            struct redTOp* op = &(t->red[i]);
            int64_t from = op->slc.from.i[0];
            int64_t to   = op->slc.to.i[0];

            Laik_Mapping* fromMap = 0;
            if (fromList && (fromList->count > 0) && (op->myInputMapNo >= 0)) {
                assert(op->myInputMapNo < fromList->count);
                fromMap = &(fromList->map[op->myInputMapNo]);
            }

            Laik_Mapping* toMap = 0;
            if (toList && (toList->count > 0) && (op->myOutputMapNo >= 0)) {
                assert(op->myOutputMapNo < toList->count);
                toMap = &(toList->map[op->myOutputMapNo]);
                assert(toMap->base != 0);
            }

            char* fromBase = fromMap ? fromMap->base : 0;
            char* toBase = toMap ? toMap->base : 0;
            uint64_t elemCount = to - from;

            // if current task is receiver, toBase should be allocated
            if (laik_trans_isInGroup(t, op->outputGroup, myid))
                assert(toBase != 0);
            else
                toBase = 0; // no interest in receiving anything

            if (fromBase) {
                assert(from >= fromMap->requiredSlice.from.i[0]);
                fromBase += (from - fromMap->requiredSlice.from.i[0]) * data->elemsize;
            }
            if (toBase) {
                assert(from >= toMap->requiredSlice.from.i[0]);
                toBase += (from - toMap->requiredSlice.from.i[0]) * data->elemsize;
            }

            MPI_Datatype mpiDataType = getMPIDataType(data);

            // all-groups never should be specified explicitly
            if (op->outputGroup >= 0)
                assert(t->subgroup[op->outputGroup].count < group->size);
            if (op->inputGroup >= 0)
                assert(t->subgroup[op->inputGroup].count < group->size);

            if (laik_log_begin(1)) {
                laik_log_append("    %s reduction: (%lld - %lld) slc/map %d/%d",
                                record ? "record":"execute",
                                (long long int) from, (long long int) to,
                                op->myInputSliceNo, op->myInputMapNo);
                laik_log_flush(0);
            }

            if (record) {
                laik_aseq_addGroupReduce(as, 0,
                                         op->inputGroup, op->outputGroup,
                                         fromBase, toBase, elemCount, op->redOp);
            }
            else {
                // fill out action parameters and transition context,
                // and execute the action directly
                Laik_BackendAction a;
                Laik_TransitionContext tc;

                laik_aseq_initTContext(&tc,
                                       data, t, fromList, toList);

                // can this be replaced with MPI_reduce?
                bool doReduce = (op->inputGroup == -1);
                int root = -1;
                if (op->outputGroup >= 0) {
                    if (t->subgroup[op->outputGroup].count == 1)
                        root = t->subgroup[op->outputGroup].task[0];
                    else
                        doReduce = false;
                }

                if (doReduce) {
                    laik_aseq_initReduce(&a, fromBase, toBase, to - from,
                                         root, op->redOp);
                    laik_mpi_exec_reduce(&tc, &a, mpiDataType, comm);
                }
                else {
                    laik_aseq_initGroupReduce(&a,
                                              op->inputGroup, op->outputGroup,
                                              fromBase, toBase, elemCount, op->redOp);
                    laik_mpi_exec_groupReduce(&tc, &a, mpiDataType, comm);
                }
            }

            if ((record == 0) && ss) {
                ss->reduceCount++;
                ss->reducedBytes += (to - from) * data->elemsize;
            }
        }
    }

    // use 2x <taskcount> phases to avoid deadlocks
    // - count phases X: 0..<taskcount - 1>
    //     - receive from <task X> if <task X> lower rank
    //     - send to <task X> if <task X> is higher rank
    // - count phases Y: 0..<taskcount - 1>
    //     - receive from <taskcount - Y> if it is higher rank
    //     - send to <taskcount - 1 - Y> if it is lower rank
    //
    // TODO: prepare communication schedule with sorted transitions actions!

    int tcount = group->size;
    for(int phase = 0; phase < 2*tcount; phase++) {
        int task = (phase < tcount) ? phase : (2*tcount-phase-1);
        bool sendToHigher   = (phase < tcount);
        bool recvFromLower  = (phase < tcount);
        bool sendToLower    = (phase >= tcount);
        bool recvFromHigher = (phase >= tcount);

        // receive
        for(int i=0; i < t->recvCount; i++) {
            struct recvTOp* op = &(t->recv[i]);
            if (task != op->fromTask) continue;
            if (recvFromLower  && (myid < task)) continue;
            if (recvFromHigher && (myid > task)) continue;

            if (laik_log_begin(1)) {
                laik_log_append("    %s recv ", record ? "record":"execute");
                laik_log_Slice(dims, &(op->slc));
                laik_log_flush(" from T%d", op->fromTask);
            }

            assert(myid != op->fromTask);

            assert(op->mapNo < toList->count);
            Laik_Mapping* toMap = &(toList->map[op->mapNo]);
            assert(toMap != 0);
            assert(toMap->base != 0);

            MPI_Status s;
            uint64_t count = 0;

            MPI_Datatype mpiDataType = getMPIDataType(data);

            // TODO:
            // - tag 1 may conflict with application
            // - check status

            if (dims == 1) {
                // we directly support 1d data layouts

                // from global to receiver-local indexes
                int64_t from = op->slc.from.i[0] - toMap->requiredSlice.from.i[0];
                int64_t to   = op->slc.to.i[0] - toMap->requiredSlice.from.i[0];
                assert(from >= 0);
                assert(to > from);
                count = to - from;

                laik_log(1, "  direct recv to local [%lld;%lld[, slc/map %d/%d, "
                         "elemsize %d, baseptr %p\n",
                         (long long int) from, (long long int) to,
                         op->sliceNo, op->mapNo,
                         data->elemsize, (void*) toMap->base);

                if (mpi_bug > 0) {
                    // intentional bug: ignore small amounts of data received
                    if (count < 1000) {
                        char dummy[8000];
                        MPI_Recv(dummy, count,
                                 mpiDataType, op->fromTask, 1, comm, &s);
                        continue;
                    }
                }

                if (record)
                    laik_aseq_addBufRecv(as, 0,
                                            toMap->base + from * data->elemsize,
                                            count, op->fromTask);
                else {
                    // TODO: tag 1 may conflict with application
                    MPI_Recv(toMap->base + from * data->elemsize, count,
                             mpiDataType, op->fromTask, 1, comm, &s);
                }
            }
            else {
                // use temporary receive buffer and layout-specific unpack

                // the used layout must support unpacking
                assert(toMap->layout->unpack);

                if (record)
                    laik_aseq_addRecvAndUnpack(as, 0, toMap,
                                                  &(op->slc), op->fromTask);
                else {
                    Laik_BackendAction a;
                    laik_aseq_initRecvAndUnpack(&a, toMap,
                                                   dims, &(op->slc), op->fromTask);
                    laik_mpi_exec_recvAndUnpack(&a, a.map, dims, data->elemsize,
                                                mpiDataType, 1, comm);
                    count = a.count;
                }
            }

            if ((record == 0) && ss) {
                ss->recvCount++;
                ss->receivedBytes += count * data->elemsize;
            }


        }

        // send
        for(int i=0; i < t->sendCount; i++) {
            struct sendTOp* op = &(t->send[i]);
            if (task != op->toTask) continue;
            if (sendToLower  && (myid < task)) continue;
            if (sendToHigher && (myid > task)) continue;

            if (laik_log_begin(1)) {
                laik_log_append("    %s send ", record ? "record":"execute");
                laik_log_Slice(dims, &(op->slc));
                laik_log_flush(" to T%d", op->toTask);
            }

            assert(myid != op->toTask);

            assert(op->mapNo < fromList->count);
            Laik_Mapping* fromMap = &(fromList->map[op->mapNo]);
            // data to send must exist in local memory
            assert(fromMap);
            if (!fromMap->base) {
                laik_log_begin(LAIK_LL_Panic);
                laik_log_append("About to send data ('%s', slice ", data->name);
                laik_log_Slice(dims, &(op->slc));
                laik_log_flush(") to preserve it for the next phase as"
                                " requested by you, but it never was written"
                                " to in the previous phase. Fix your code!");
                assert(0);
            }

            uint64_t count = 0;
            MPI_Datatype mpiDataType = getMPIDataType(data);

            if (dims == 1) {
                // we directly support 1d data layouts

                // from global to sender-local indexes
                int64_t from = op->slc.from.i[0] - fromMap->requiredSlice.from.i[0];
                int64_t to   = op->slc.to.i[0] - fromMap->requiredSlice.from.i[0];
                assert(from >= 0);
                assert(to > from);
                count = to - from;

                laik_log(1, "  direct send: from local [%lld;%lld[, slice/map %d/%d, "
                            "elemsize %d, baseptr %p\n",
                         (long long int) from, (long long int) to,
                         op->sliceNo, op->mapNo,
                         data->elemsize, (void*) fromMap->base);

                if (record)
                    laik_aseq_addBufSend(as, 0,
                                            fromMap->base + from * data->elemsize,
                                            count, op->toTask);
                else {
                    // TODO: tag 1 may conflict with application
                    MPI_Send(fromMap->base + from * data->elemsize, count,
                             mpiDataType, op->toTask, 1, comm);
                }
            }
            else {
                // use temporary receive buffer and layout-specific unpack

                // the used layout must support packing
                assert(fromMap->layout->pack);

                if (record)
                    laik_aseq_addPackAndSend(as, 0, fromMap,
                                                &(op->slc), op->toTask);
                else {
                    Laik_BackendAction a;
                    laik_aseq_initPackAndSend(&a, fromMap,
                                                 dims, &(op->slc), op->toTask);
                    laik_mpi_exec_packAndSend(&a, a.map, dims, mpiDataType, 1, comm);
                    count = a.count;
                }
            }

            if ((record == 0) && ss) {
                ss->sendCount++;
                ss->sentBytes += count * data->elemsize;
            }
        }
    
    }
}

// replace group reductions be reduce actions if possible
static void detectReduce(Laik_ActionSeq* as, Laik_ActionSeq* as2)
{
    Laik_TransitionContext* tc = as->context[0];
    Laik_Transition* t = tc->transition;

    for(int i = 0; i < as->actionCount; i++) {
        Laik_BackendAction* ba = &(as->action[i]);

        switch(ba->type) {
        // TODO: LAIK_AT_MapGroupReduce
        case LAIK_AT_GroupReduce:
            if (ba->inputGroup == -1) {
                if (ba->outputGroup == -1) {
                    laik_aseq_addReduce(as2, ba->round, ba->fromBuf, ba->toBuf,
                                        ba->count, -1, ba->redOp);
                    continue;
                }
                else if (laik_trans_groupCount(t, ba->outputGroup) == 1) {
                    int root = laik_trans_taskInGroup(t, ba->outputGroup, 0);
                    laik_aseq_addReduce(as2, ba->round, ba->fromBuf, ba->toBuf,
                                        ba->count, root, ba->redOp);
                    continue;
                }
            }
            // fall-through;
        default:
            laik_actions_add(ba, as2, -1);
            break;
        }
    }
}

static
Laik_ActionSeq* laik_mpi_prepare(Laik_Data* d, Laik_Transition* t,
                                 Laik_MappingList* fromList,
                                 Laik_MappingList* toList)
{
    laik_log(1, "MPI backend: prepare sequence for transition on data '%s'\n",
             d->name);

    Laik_ActionSeq *as, *as2;

    as = laik_aseq_new(d->space->inst);
    laik_aseq_addTContext(as, d, t, fromList, toList);
    laik_execOrRecord(true, d, t, as, fromList, toList);

    if (laik_log_begin(1)) {
        laik_log_ActionSeq(as);
        laik_log_flush(0);
    }

    as2 = laik_actions_setupTransform(as);
    laik_aseq_flattenPacking(as, as2);
    laik_aseq_free(as);
    as = as2;

    if (laik_log_begin(1)) {
        laik_log_append("After flattening actions:\n");
        laik_log_ActionSeq(as);
        laik_log_flush(0);
    }

    if (mpi_reduce) {
        // detect group reduce actions which can be replaced by all-reduce
        // can be prohibited by setting LAIK_MPI_REDUCE=0
        as2 = laik_actions_setupTransform(as);
        detectReduce(as, as2);
        laik_aseq_free(as);
        as = as2;

        if (laik_log_begin(1)) {
            laik_log_append("After all-reduce detection:\n");
            laik_log_ActionSeq(as);
            laik_log_flush(0);
        }
    }

#if 0
    as2 = laik_actions_setupTransform(as);
    laik_aseq_combineActions(as, as2);
    laik_aseq_free(as);
    as = as2;

    if (laik_log_begin(1)) {
        laik_log_append("After combining actions:\n");
        laik_log_ActionSeq(as);
        laik_log_flush(0);
    }

    laik_aseq_allocBuffer(as);

    if (laik_log_begin(1)) {
        laik_log_append("After buffer allocation:\n");
        laik_log_ActionSeq(as);
        laik_log_flush(0);
    }

#endif

    as2 = laik_actions_setupTransform(as);
    laik_aseq_splitReduce(as, as2);
    laik_aseq_free(as);
    as = as2;

    if (laik_log_begin(1)) {
        laik_log_append("After splitting reduce actions:\n");
        laik_log_ActionSeq(as);
        laik_log_flush(0);
    }

    as2 = laik_actions_setupTransform(as);
    //laik_aseq_sort_2phases(as, as2);
    laik_aseq_sort_rankdigits(as, as2);
    laik_aseq_free(as);
    as = as2;

    if (laik_log_begin(1)) {
        laik_log_append("After sorting actions:\n");
        laik_log_ActionSeq(as);
        laik_log_flush(0);
    }

    laik_aseq_allocBuffer(as);

    if (laik_log_begin(1)) {
        laik_log_append("After buffer allocation 1:\n");
        laik_log_ActionSeq(as);
        laik_log_flush(0);
    }

    as2 = laik_actions_setupTransform(as);
    laik_aseq_combineActions(as, as2);
    laik_aseq_free(as);
    as = as2;

    if (laik_log_begin(1)) {
        laik_log_append("After combining actions:\n");
        laik_log_ActionSeq(as);
        laik_log_flush(0);
    }

    laik_aseq_allocBuffer(as);

    if (laik_log_begin(1)) {
        laik_log_append("After buffer allocation 2:\n");
        laik_log_ActionSeq(as);
        laik_log_flush(0);
    }

    return as;
}

static void laik_mpi_cleanup(Laik_ActionSeq* as)
{
    laik_log(1, "MPI backend: cleanup action sequence (%d actions)\n",
             as->actionCount);
    laik_aseq_free(as);
}

static void laik_mpi_wait(Laik_ActionSeq* as, int mapNo)
{
    // required due to interface signature
    (void) as;
    (void) mapNo;

    // nothing to wait for: this backend driver currently is synchronous
}

static bool laik_mpi_probe(Laik_ActionSeq* p, int mapNo)
{
    // required due to interface signature
    (void) p;
    (void) mapNo;

    // all communication finished: this backend driver currently is synchronous
    return true;
}

static
void laik_mpi_exec(Laik_Data *d, Laik_Transition *t, Laik_ActionSeq* as,
                   Laik_MappingList* fromList, Laik_MappingList* toList)
{
    if (!as) {
        laik_execOrRecord(false, d, t, as, fromList, toList);
        return;
    }

    Laik_TransitionContext* tc = as->context[0];
    assert(d == tc->data);
    assert(t == tc->transition);

    if (as->actionCount > 0)
        laik_mpi_exec_actions(as, d->stat);
}


#endif // USE_MPI
