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

/* The TCP backend mainly uses its own minimal MPI implementation.
 * Similar to the LAIK MPI backend, this file just dispatches to
 * our own mini-MPI implementation instead of an official MPI.
 * Thus, this file is almost identical to "backend-mpi.c", and future
 * improvements/changes to the MPI backend may also be useful here.
 *
 * Differences:
 * - backend API functions are prefixed with laik_tcp_*
 * - no usage of async MPI (no opt-pass, no specific actions)
 *
 * For more details, run
 *   git diff --no-index src/backend-mpi.c src/backends/tcp/backend-tcp.c
 *
 */

#include "laik-internal.h"
#include "laik-backend-tcp.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

// own mini MPI version of the TCP backend
#include "mpi.h"

// forward decls, types/structs , global variables

static void laik_tcp_finalize(Laik_Instance*);
static void laik_tcp_prepare(Laik_ActionSeq*);
static void laik_tcp_cleanup(Laik_ActionSeq*);
static void laik_tcp_exec(Laik_ActionSeq* as);
static void laik_tcp_updateGroup(Laik_Group*);
static void laik_tcp_sync(Laik_KVStore* kvs);
static void laik_tcp_eliminate_nodes(Laik_Group *oldGroup, Laik_Group *newGroup, int *nodeStatuses);

// C guarantees that unset function pointers are NULL
static Laik_Backend laik_backend_tcp = {
    .name        = "TCP Backend",
    .finalize    = laik_tcp_finalize,
    .prepare     = laik_tcp_prepare,
    .cleanup     = laik_tcp_cleanup,
    .exec        = laik_tcp_exec,
    .updateGroup = laik_tcp_updateGroup,
    .eliminateNodes = laik_tcp_eliminate_nodes,
    .sync        = laik_tcp_sync
};

static Laik_Instance* tcp_instance = 0;

typedef struct {
    MPI_Comm comm;
    bool didInit;
} TCPData;

typedef struct {
    MPI_Comm comm;
} TCPGroupData;

//----------------------------------------------------------------
// MPI backend behavior configurable by environment variables

// LAIK_TCP_REDUCE: make use of MPI_(All)Reduce? Default: Yes
// If not, we do own algorithm with send/recv.
static int tcp_reduce = 1;

//----------------------------------------------------------------
// buffer space for messages if packing/unpacking from/to not-1d layout
// is necessary
#define PACKBUFSIZE (10*1024*1024)
//#define PACKBUFSIZE (10*800)
static char packbuf[PACKBUFSIZE];



//----------------------------------------------------------------------------
// error helpers

static
void laik_tcp_panic(int err)
{
    char str[MPI_MAX_ERROR_STRING];
    int len;

    assert(err != MPI_SUCCESS);

    if(laik_error_handler_get(tcp_instance) != NULL) {

        laik_log(LAIK_LL_Debug, "Error handler found, attempting to handle error.\n");
        if(MPI_Error_string(err, str, &len) != MPI_SUCCESS) {
            strncpy(str, "Unknown MPI Error!", sizeof(str));
        }
        laik_tcp_set_errors(err, NULL);
        laik_error_handler_get(tcp_instance)(tcp_instance, str);
        laik_tcp_clear_errors();
//        fprintf(stderr, "[LAIK TCP Backend] Error handler exited, attempting to continue\n");
        return;
    }

    if (MPI_Error_string(err, str, &len) != MPI_SUCCESS)
        laik_panic("TCP backend: Unknown mini-MPI error!");
    else
        laik_log(LAIK_LL_Panic, "TCP backend: mini-MPI error '%s'", str);
    exit(1);
}


//----------------------------------------------------------------------------
// backend interface implementation: initialization

Laik_Instance* laik_init_tcp(int* argc, char*** argv)
{
    if (tcp_instance) return tcp_instance;

    int err;

    TCPData* d = malloc(sizeof(TCPData));
    if (!d) {
        laik_panic("Out of memory allocating TCPData object");
        exit(1); // not actually needed, laik_panic never returns
    }
    d->didInit = false;

    TCPGroupData* gd = malloc(sizeof(TCPGroupData));
    if (!gd) {
        laik_panic("Out of memory allocating TCPGroupData object");
        exit(1); // not actually needed, laik_panic never returns
    }

    // eventually initialize MPI first before accessing MPI_COMM_WORLD
    if (argc) {
        err = MPI_Init(argc, argv);
        if (err != MPI_SUCCESS) laik_tcp_panic(err);
        d->didInit = true;
    }

    // create own communicator duplicating WORLD to
    // - not have to worry about conflicting use of MPI_COMM_WORLD by application
    // - install error handler which passes errors through - we want them
    MPI_Comm ownworld;
    err = MPI_Comm_dup(MPI_COMM_WORLD, &ownworld);
    if (err != MPI_SUCCESS) laik_tcp_panic(err);
    // TCP backend always returns errors
    //err = MPI_Comm_set_errhandler(ownworld, MPI_ERRORS_RETURN);
    //if (err != MPI_SUCCESS) laik_tcp_panic(err);

    // now finish initilization of <gd>/<d>, as MPI_Init is run
    gd->comm = ownworld;
    d->comm = ownworld;

    int size, rank;
    err = MPI_Comm_size(d->comm, &size);
    if (err != MPI_SUCCESS) laik_tcp_panic(err);
    err = MPI_Comm_rank(d->comm, &rank);
    if (err != MPI_SUCCESS) laik_tcp_panic(err);

    // Get the name of the processor
    char processor_name[MPI_MAX_PROCESSOR_NAME];
    int name_len;
    err = MPI_Get_processor_name(processor_name, &name_len);
    if (err != MPI_SUCCESS) laik_tcp_panic(err);

    Laik_Instance* inst;
    inst = laik_new_instance(&laik_backend_tcp, size, rank,
                             processor_name, d, gd);

    sprintf(inst->guid, "%d", rank);

    laik_log(2, "TCP backend initialized (at '%s', rank %d/%d)\n",
             inst->mylocation, rank, size);

    // do own reduce algorithm?
    char* str = getenv("LAIK_TCP_REDUCE");
    if (str) tcp_reduce = atoi(str);

    tcp_instance = inst;
    return inst;
}

static
TCPData* tcpData(Laik_Instance* i)
{
    return (TCPData*) i->backend_data;
}

static
TCPGroupData* tcpGroupData(Laik_Group* g)
{
    return (TCPGroupData*) g->backend_data;
}

static
void laik_tcp_finalize(Laik_Instance* inst)
{
    assert(inst == tcp_instance);

    if (tcpData(tcp_instance)->didInit) {
        laik_log(1, "TCP backend: calling our MPI_Finalize");
        int err = MPI_Finalize();
        if (err != MPI_SUCCESS) laik_tcp_panic(err);
    }
}

static TCPGroupData *allocateBackendData(Laik_Group *g) {
    TCPGroupData *gd = (TCPGroupData *) g->backend_data;
    assert(gd == 0); // must not be updated yet
    gd = malloc(sizeof(TCPGroupData));
    if (!gd) {
        laik_panic("Out of memory allocating TCPGroupData object");
        exit(1); // not actually needed, laik_panic never returns
    }
    g->backend_data = gd;
    return gd;
}

// update backend specific data for group if needed
static void laik_tcp_updateGroup(Laik_Group* g)
{
    // calculate MPI communicator for group <g>
    // TODO: only supports shrinking of parent for now
    assert(g->parent);
    assert(g->parent->size >= g->size);

    laik_log(1, "TCP backend updateGroup: parent %d (size %d, myid %d) "
             "=> group %d (size %d, myid %d)",
             g->parent->gid, g->parent->size, g->parent->myid,
             g->gid, g->size, g->myid);

    // only interesting if this task is still part of parent
    if (g->parent->myid < 0) return;

    TCPGroupData* gdParent = (TCPGroupData*) g->parent->backend_data;
    assert(gdParent);

    TCPGroupData *gd = allocateBackendData(g);

    laik_log(1, "Comm_split: old myid %d => new myid %d",
             g->parent->myid, g->fromParent[g->parent->myid]);

    int err = MPI_Comm_split(gdParent->comm, g->myid < 0 ? MPI_UNDEFINED : 0,
                             g->myid, &(gd->comm));
    if (err != MPI_SUCCESS) laik_tcp_panic(err);
}

static void laik_tcp_eliminate_nodes(Laik_Group *oldGroup, Laik_Group *newGroup, int *nodeStatuses) {
    laik_log(1, "TCP backend eliminate nodes");

    TCPGroupData *gd = allocateBackendData(newGroup);

    int err = MPI_Comm_eliminate(((TCPGroupData*)oldGroup->backend_data)->comm, oldGroup->size,
                             nodeStatuses, LAIK_FT_NODE_OK, &gd->comm);
    if (err != MPI_SUCCESS) laik_tcp_panic(err);

    // Reset the TCP MiniMPI Comm World to the smaller group. To date, this is only used for MPI_Finalize.
    LAIK_TCP_MINIMPI_COMM_WORLD = gd->comm;
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
void laik_mpi_exec_packAndSend(Laik_Mapping* map, Laik_Slice* slc,
                               int to_rank, uint64_t slc_size,
                               MPI_Datatype dataType, int tag, MPI_Comm comm)
{
    Laik_Index idx = slc->from;
    int dims = slc->space->dims;
    unsigned int packed;
    uint64_t count = 0;
    while(1) {
        packed = (map->layout->pack)(map, slc, &idx,
                                     packbuf, PACKBUFSIZE);
        assert(packed > 0);
        int err = MPI_Send(packbuf, (int) packed,
                           dataType, to_rank, tag, comm);
        if (err != MPI_SUCCESS) laik_tcp_panic(err);

        count += packed;
        if (laik_index_isEqual(dims, &idx, &(slc->to))) break;
    }
    assert(count == slc_size);
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
        int err = MPI_Recv(packbuf, PACKBUFSIZE / elemsize,
                           dataType, from_rank, tag, comm, &st);
        if (err != MPI_SUCCESS) laik_tcp_panic(err);
        err = MPI_Get_count(&st, dataType, &recvCount);
        if (err != MPI_SUCCESS) laik_tcp_panic(err);

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
    assert(tcp_reduce > 0);

    MPI_Op mpiRedOp = getMPIOp(a->redOp);
    int rootTask = a->rank;
    int err;

    if (rootTask == -1) {
        if (a->fromBuf == a->toBuf) {
            laik_log(1, "      exec MPI_Allreduce in-place, count %d", a->count);
            err = MPI_Allreduce(MPI_IN_PLACE, a->toBuf, (int) a->count,
                                    dataType, mpiRedOp, comm);
        }
        else {
            laik_log(1, "      exec MPI_Allreduce, count %d", a->count);
            err = MPI_Allreduce(a->fromBuf, a->toBuf, (int) a->count,
                                dataType, mpiRedOp, comm);
        }
    }
    else {
        if ((a->fromBuf == a->toBuf) && (tc->transition->group->myid == rootTask)) {
            laik_log(1, "      exec MPI_Reduce in-place, count %d, root %d",
                     a->count, rootTask);
            err = MPI_Reduce(MPI_IN_PLACE, a->toBuf, (int) a->count,
                             dataType, mpiRedOp, rootTask, comm);
        }
        else {
            laik_log(1, "      exec MPI_Reduce, count %d, root %d", a->count, rootTask);
            err = MPI_Reduce(a->fromBuf, a->toBuf, (int) a->count,
                             dataType, mpiRedOp, rootTask, comm);
        }
    }
    if (err != MPI_SUCCESS) laik_tcp_panic(err);
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
    int count, err;

    if (myid != reduceTask) {
        // not the reduce task: eventually send input and recv result

        if (laik_trans_isInGroup(t, a->inputGroup, myid)) {
            laik_log(1, "        exec MPI_Send to T%d", reduceTask);
            err = MPI_Send(a->fromBuf, (int) a->count, dataType,
                           reduceTask, 1, comm);
            if (err != MPI_SUCCESS) laik_tcp_panic(err);
        }
        if (laik_trans_isInGroup(t, a->outputGroup, myid)) {
            laik_log(1, "        exec MPI_Recv from T%d", reduceTask);
            err = MPI_Recv(a->toBuf, (int) a->count, dataType,
                           reduceTask, 1, comm, &st);
            if (err != MPI_SUCCESS) laik_tcp_panic(err);
            // check that we received the expected number of elements
            err = MPI_Get_count(&st, dataType, &count);
            if (err != MPI_SUCCESS) laik_tcp_panic(err);
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
        err = MPI_Recv(packbuf + off, (int) a->count, dataType,
                       inTask, 1, comm, &st);
        if (err != MPI_SUCCESS) laik_tcp_panic(err);
        // check that we received the expected number of elements
        err = MPI_Get_count(&st, dataType, &count);
        if (err != MPI_SUCCESS) laik_tcp_panic(err);
        assert((int)a->count == count);
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
        err = MPI_Send(a->toBuf, (int) a->count, dataType, outTask, 1, comm);
        if (err != MPI_SUCCESS) laik_tcp_panic(err);
    }
}

static
void laik_tcp_exec(Laik_ActionSeq* as)
{
    if (as->actionCount == 0) {
        laik_log(1, "TCP backend exec: nothing to do\n");
        return;
    }

    if (as->backend == 0) {
        // no preparation: do minimal transformations, sorting send/recv
        laik_log(1, "TCP backend exec: prepare before exec\n");
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
        assert(not_handled == 0); // there should be no MPI-specific actions
    }

    if (laik_log_begin(1)) {
        laik_log_append("TCP backend exec:\n");
        laik_log_ActionSeq(as, false);
        laik_log_flush(0);
    }

    // TODO: use transition context given by each action
    Laik_TransitionContext* tc = as->context[0];
    Laik_MappingList* fromList = tc->fromList;
    Laik_MappingList* toList = tc->toList;
    int elemsize = tc->data->elemsize;

    // common for all MPI calls: tag, comm, datatype
    int tag = 1;
    TCPGroupData* gd = tcpGroupData(tc->transition->group);
    assert(gd);
    MPI_Comm comm = gd->comm;
    MPI_Datatype dataType = getMPIDataType(tc->data);
    MPI_Status st;
    int err, count;

    Laik_Action* a = as->action;
    for(unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a)) {
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
            err = MPI_Send(fromMap->base + ba->offset, ba->count,
                           dataType, ba->rank, tag, comm);
            if (err != MPI_SUCCESS) laik_tcp_panic(err);
            break;
        }

        case LAIK_AT_RBufSend: {
            Laik_A_RBufSend* aa = (Laik_A_RBufSend*) a;
            assert(aa->bufID < ASEQ_BUFFER_MAX);
            err = MPI_Send(as->buf[aa->bufID] + aa->offset, aa->count,
                           dataType, aa->to_rank, tag, comm);
            if (err != MPI_SUCCESS) laik_tcp_panic(err);
            break;
        }

        case LAIK_AT_BufSend: {
            Laik_A_BufSend* aa = (Laik_A_BufSend*) a;
            err = MPI_Send(aa->buf, aa->count,
                           dataType, aa->to_rank, tag, comm);
            if (err != MPI_SUCCESS) laik_tcp_panic(err);
            break;
        }

        case LAIK_AT_MapRecv: {
            assert(ba->toMapNo < toList->count);
            Laik_Mapping* toMap = &(toList->map[ba->toMapNo]);
            assert(toMap->base != 0);
            err = MPI_Recv(toMap->base + ba->offset, ba->count,
                           dataType, ba->rank, tag, comm, &st);
            if (err != MPI_SUCCESS) laik_tcp_panic(err);

            // check that we received the expected number of elements
            err = MPI_Get_count(&st, dataType, &count);
            if (err != MPI_SUCCESS) laik_tcp_panic(err);
            assert((int)ba->count == count);
            break;
        }

        case LAIK_AT_RBufRecv: {
            Laik_A_RBufRecv* aa = (Laik_A_RBufRecv*) a;
            assert(aa->bufID < ASEQ_BUFFER_MAX);
            err = MPI_Recv(as->buf[aa->bufID] + aa->offset, aa->count,
                           dataType, aa->from_rank, tag, comm, &st);
            if (err != MPI_SUCCESS) laik_tcp_panic(err);

            // check that we received the expected number of elements
            err = MPI_Get_count(&st, dataType, &count);
            if (err != MPI_SUCCESS) laik_tcp_panic(err);
            assert((int)ba->count == count);
            break;
        }

        case LAIK_AT_BufRecv: {
            Laik_A_BufRecv* aa = (Laik_A_BufRecv*) a;
            err = MPI_Recv(aa->buf, aa->count,
                           dataType, aa->from_rank, tag, comm, &st);
            if (err != MPI_SUCCESS) laik_tcp_panic(err);

            // check that we received the expected number of elements
            err = MPI_Get_count(&st, dataType, &count);
            if (err != MPI_SUCCESS) laik_tcp_panic(err);
//            assert((int)ba->count == count);
            break;
        }

        case LAIK_AT_CopyFromBuf:
            for(unsigned int i = 0; i < ba->count; i++)
                memcpy(ba->ce[i].ptr,
                       ba->fromBuf + ba->ce[i].offset,
                       ba->ce[i].bytes);
            break;

        case LAIK_AT_CopyToBuf:
            for(unsigned int i = 0; i < ba->count; i++)
                memcpy(ba->toBuf + ba->ce[i].offset,
                       ba->ce[i].ptr,
                       ba->ce[i].bytes);
            break;

        case LAIK_AT_PackToBuf:
            laik_exec_pack(ba, ba->map);
            break;

        case LAIK_AT_MapPackToBuf: {
            assert(ba->fromMapNo < fromList->count);
            Laik_Mapping* fromMap = &(fromList->map[ba->fromMapNo]);
            assert(fromMap->base != 0);
            laik_exec_pack(ba, fromMap);
            break;
        }

        case LAIK_AT_UnpackFromBuf:
            laik_exec_unpack(ba, ba->map);
            break;

        case LAIK_AT_MapUnpackFromBuf: {
            assert(ba->toMapNo < toList->count);
            Laik_Mapping* toMap = &(toList->map[ba->toMapNo]);
            assert(toMap->base);
            laik_exec_unpack(ba, toMap);
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
    assert( ((char*)as->action) + as->bytesUsed == ((char*)a) );
}


static
void laik_tcp_prepare(Laik_ActionSeq* as)
{
    if (laik_log_begin(1)) {
        laik_log_append("TCP backend prepare:\n");
        laik_log_ActionSeq(as, false);
        laik_log_flush(0);
    }

    // mark as prepared by TCP backend
    as->backend = &laik_backend_tcp;

    bool changed = laik_aseq_splitTransitionExecs(as);
    laik_log_ActionSeqIfChanged(changed, as, "After splitting transition execs");
    if (as->actionCount == 0) {
        laik_aseq_calc_stats(as);
        return;
    }

    changed = laik_aseq_flattenPacking(as);
    laik_log_ActionSeqIfChanged(changed, as, "After flattening actions");

    if (tcp_reduce) {
        // detect group reduce actions which can be replaced by all-reduce
        // can be prohibited by setting LAIK_TCP_REDUCE=0
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
    laik_aseq_calc_stats(as);
}

static void laik_tcp_cleanup(Laik_ActionSeq* as)
{
    if (laik_log_begin(1)) {
        laik_log_append("TCP backend cleanup:\n");
        laik_log_ActionSeq(as, false);
        laik_log_flush(0);
    }

    assert(as->backend == &laik_backend_tcp);
}

//----------------------------------------------------------------------------
// KV store


static void laik_tcp_sync(Laik_KVStore* kvs)
{
    assert(kvs->inst == tcp_instance);
    MPI_Comm comm = tcpData(tcp_instance)->comm;
    Laik_Group* world = kvs->inst->world;
    int myid = world->myid;
    MPI_Status status;
    int count[2] = {0,0};
    int err;

    if (myid > 0) {
        // send to master, receive from master
        count[0] = (int) kvs->changes.offUsed;
        assert((count[0] == 0) || ((count[0] & 1) == 1)); // 0 or odd number of offsets
        count[1] = (int) kvs->changes.dataUsed;
        laik_log(1, "MPI sync: sending %d changes (total %d chars) to T0",
                 count[0] / 2, count[1]);
        err = MPI_Send(count, 2, MPI_INTEGER, 0, 0, comm);
        if (err != MPI_SUCCESS) laik_tcp_panic(err);
        if (count[0] > 0) {
            assert(count[1] > 0);
            err = MPI_Send(kvs->changes.off, count[0], MPI_INTEGER, 0, 0, comm);
            if (err != MPI_SUCCESS) laik_tcp_panic(err);
            err = MPI_Send(kvs->changes.data, count[1], MPI_CHAR, 0, 0, comm);
            if (err != MPI_SUCCESS) laik_tcp_panic(err);
        }
        else assert(count[1] == 0);

        err = MPI_Recv(count, 2, MPI_INTEGER, 0, 0, comm, &status);
        if (err != MPI_SUCCESS) laik_tcp_panic(err);
        laik_log(1, "MPI sync: getting %d changes (total %d chars) from T0",
                 count[0] / 2, count[1]);
        if (count[0] > 0) {
            assert(count[1] > 0);
            laik_kvs_changes_ensure_size(&(kvs->changes), count[0], count[1]);
            err = MPI_Recv(kvs->changes.off, count[0], MPI_INTEGER, 0, 0, comm, &status);
            if (err != MPI_SUCCESS) laik_tcp_panic(err);
            err = MPI_Recv(kvs->changes.data, count[1], MPI_CHAR, 0, 0, comm, &status);
            if (err != MPI_SUCCESS) laik_tcp_panic(err);
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

    for(int i = 1; i < world->size; i++) {
        err = MPI_Recv(count, 2, MPI_INTEGER, i, 0, comm, &status);
        if (err != MPI_SUCCESS) laik_tcp_panic(err);
        laik_log(1, "MPI sync: getting %d changes (total %d chars) from T%d",
                 count[0] / 2, count[1], i);
        laik_kvs_changes_set_size(&recvd, 0, 0); // fresh reuse
        laik_kvs_changes_ensure_size(&recvd, count[0], count[1]);
        if (count[0] == 0) {
            assert(count[1] == 0);
            continue;
        }

        assert(count[1] > 0);
        err = MPI_Recv(recvd.off, count[0], MPI_INTEGER, i, 0, comm, &status);
        if (err != MPI_SUCCESS) laik_tcp_panic(err);
        err = MPI_Recv(recvd.data, count[1], MPI_CHAR, i, 0, comm, &status);
        if (err != MPI_SUCCESS) laik_tcp_panic(err);
        laik_kvs_changes_set_size(&recvd, count[0], count[1]);

        // for merging, both inputs need to be sorted
        laik_kvs_changes_sort(&recvd);

        // swap src/dst: now merging can overwrite dst
        tmp = src; src = dst; dst = tmp;

        laik_kvs_changes_merge(dst, src, &recvd);
    }

    // send merged changes to all others: may be 0 entries
    count[0] = dst->offUsed;
    count[1] = dst->dataUsed;
    assert(count[1] > count[0]); // more byte than offsets
    for(int i = 1; i < world->size; i++) {
        laik_log(1, "MPI sync: sending %d changes (total %d chars) to T%d",
                 count[0] / 2, count[1], i);
        err = MPI_Send(count, 2, MPI_INTEGER, i, 0, comm);
        if (err != MPI_SUCCESS) laik_tcp_panic(err);
        if (count[0] == 0) continue;

        err = MPI_Send(dst->off, count[0], MPI_INTEGER, i, 0, comm);
        if (err != MPI_SUCCESS) laik_tcp_panic(err);
        err = MPI_Send(dst->data, count[1], MPI_CHAR, i, 0, comm);
        if (err != MPI_SUCCESS) laik_tcp_panic(err);
    }

    // TODO: opt - remove own changes from received ones
    laik_kvs_changes_apply(dst, kvs);

    laik_kvs_changes_free(&recvd);
    laik_kvs_changes_free(&changes);
}
