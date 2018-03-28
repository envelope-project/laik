/*
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
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

// print out values received/sent
//#define LOG_DOUBLE_VALUES 1

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

    laik_log(LAIK_LL_Debug,
             "MPI backend initialized (location '%s', pid %d)\n",
             inst->mylocation, (int)getpid());

    // for intentionally buggy MPI backend behavior
    char* str = getenv("LAIK_MPI_BUG");
    if (str) mpi_bug = atoi(str);

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


    laik_log(LAIK_LL_Debug,
             "MPI backend updateGroup: parent %d (size %d, myid %d) " "=> group %d (size %d, myid %d)",
             g->parent->gid, g->parent->size, g->parent->myid, g->gid,
             g->size, g->myid);

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

    laik_log(LAIK_LL_Debug, "MPI Comm_split: old myid %d => new myid %d",
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
    case LAIK_RO_Sum: mpiRedOp = MPI_SUM; break;
    default: assert(0);
    }
    return mpiRedOp;
}




static
void laik_mpi_exec_packAndSend(Laik_BackendAction* a, int dims,
                               MPI_Datatype dataType, int tag, MPI_Comm comm)
{
    Laik_Index idx = a->slc->from;
    int packed;
    int count = 0;
    while(1) {
        packed = (a->map->layout->pack)(a->map, a->slc, &idx,
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
void laik_mpi_exec_recvAndUnpack(Laik_BackendAction* a, int dims, int elemsize,
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
        unpacked = (a->map->layout->unpack)(a->map, a->slc, &idx,
                                           packbuf,
                                           recvCount * elemsize);
        assert(recvCount == unpacked);
        count += unpacked;
        if (laik_index_isEqual(dims, &idx, &(a->slc->to))) break;
    }
    assert(count == a->count);
}

static
void laik_mpi_exec_reduce(Laik_BackendAction* a,
                          MPI_Datatype dataType, MPI_Comm comm)
{
    MPI_Op mpiRedOp = getMPIOp(a->redOp);
    int rootTask = a->peer_rank;

    if (rootTask == -1) {
        if (a->fromBuf == a->toBuf)
            MPI_Allreduce(MPI_IN_PLACE, a->toBuf, a->count,
                          dataType, mpiRedOp, comm);
        else
            MPI_Allreduce(a->fromBuf, a->toBuf, a->count,
                          dataType, mpiRedOp, comm);
    }
    else {
        if (a->fromBuf == a->toBuf)
            MPI_Reduce(MPI_IN_PLACE, a->toBuf, a->count,
                       dataType, mpiRedOp, rootTask, comm);
        else
            MPI_Reduce(a->fromBuf, a->toBuf, a->count,
                       dataType, mpiRedOp, rootTask, comm);
    }
}

static
void laik_mpi_exec_groupReduce(Laik_TransitionContext* tc,
                               Laik_BackendAction* a,
                               MPI_Datatype dataType, MPI_Comm comm)
{
    Laik_Transition* t = tc->transition;
    Laik_Data* data = tc->data;

    // do the manual reduction on smallest rank of output group
    int reduceTask = t->subgroup[a->outputGroup].task[0];
    laik_log(LAIK_LL_Debug, "reduce at T%d", reduceTask);

    uint64_t byteCount = a->count * data->elemsize;
    int myid = t->group->myid;
    MPI_Status st;

    if (reduceTask == myid) {
        TaskGroup* tg;

        // collect values from tasks in input group
        tg = &(t->subgroup[a->inputGroup]);
        // check that bufsize is enough
        assert(tg->count * byteCount < PACKBUFSIZE);

        char* ptr[32], *p;
        assert(tg->count <= 32);
        p = packbuf;
        int myIdx = -1;
        for(int i = 0; i< tg->count; i++) {
            if (tg->task[i] == myid) {
                ptr[i] = a->fromBuf;
                myIdx = i;

#ifdef LOG_DOUBLE_VALUES
                assert(d->elemsize == 8);
                for(uint64_t i = 0; i < elemCount; i++)
                    laik_log(LAIK_LL_Debug, "    have at %d: %f", from + i,
                             ((double *)fromBase)[i]);
#endif
#ifdef LOG_FLOAT_VALUES
                assert(d->elemsize == 4);
                for(uint64_t i = 0; i < elemCount; i++)
                    laik_log(LAIK_LL_Debug, "    have at %d: %f", from + i,
                             (double)((float *)fromBase)[i]);
#endif
                continue;
            }

            laik_log(LAIK_LL_Debug, "  MPI_Recv from T%d (buf off %lld)",
                     tg->task[i], (long long int)(p - packbuf));

            ptr[i] = p;
            MPI_Recv(p, a->count, dataType, tg->task[i], 1, comm, &st);
#ifdef LOG_DOUBLE_VALUES
            assert(d->elemsize == 8);
            for(uint64_t i = 0; i < elemCount; i++)
                laik_log(LAIK_LL_Debug, "    got at %d: %f", from + i,
                         ((double *)p)[i]);
#endif
            p += byteCount;
        }

        // toBase may be same as fromBase (= our values).
        // e.g. when we are 3rd task (ptr[3] == fromBase), we
        // would overwrite our values. Swap ptr[0] with ptr[3].
        if (myIdx >= 0) {
            assert(ptr[myIdx] == a->fromBuf);
            ptr[myIdx] = ptr[0];
            ptr[0] = a->fromBuf;
        }

        // do the reduction, put result back to my input buffer
        if (data->type->reduce) {
            assert(tg->count > 1);


            (data->type->reduce)(a->toBuf, ptr[0], ptr[1],
                                 a->count, a->redOp);
            for(int t = 2; t < tg->count; t++)
                (data->type->reduce)(a->toBuf, a->toBuf, ptr[t],
                                     a->count, a->redOp);
        }
        else {
            laik_log(LAIK_LL_Panic,
                     "Need reduce function for type '%s'. Not set!",
                     data->type->name);
            assert(0);
        }

#ifdef LOG_DOUBLE_VALUES
        assert(d->elemsize == 8);
        for(uint64_t i = 0; i < elemCount; i++)
            laik_log(LAIK_LL_Debug, "    sum at %d: %f", from + i,
                     ((double *)toBase)[i]);
#endif

        // send result to tasks in output group
        tg = &(t->subgroup[a->outputGroup]);
        for(int i = 0; i< tg->count; i++) {
            if (tg->task[i] == myid) {
                // that's myself: nothing to do
                continue;
            }

            laik_log(LAIK_LL_Debug, "  MPI_Send result to T%d", tg->task[i]);

            MPI_Send(a->toBuf, a->count, dataType,
                     tg->task[i], 1, comm);
        }
    }
    else {
        if (laik_isInGroup(t, a->inputGroup, myid)) {
            laik_log(LAIK_LL_Debug, "  MPI_Send to T%d", reduceTask);

#ifdef LOG_DOUBLE_VALUES
            assert(d->elemsize == 8);
            for(uint64_t i = 0; i < elemCount; i++)
                laik_log(LAIK_LL_Debug, "    at %d: %f", from + i,
                         ((double *)fromBase)[i]);
#endif

            MPI_Send(a->fromBuf, a->count, dataType,
                     reduceTask, 1, comm);
        }
        if (laik_isInGroup(t, a->outputGroup, myid)) {
            laik_log(LAIK_LL_Debug, "  MPI_Recv from T%d", reduceTask);

            MPI_Recv(a->toBuf, a->count, dataType,
                     reduceTask, 1, comm, &st);
#ifdef LOG_DOUBLE_VALUES
            assert(d->elemsize == 8);
            for(uint64_t i = 0; i < elemCount; i++)
                laik_log(LAIK_LL_Debug, "    at %d: %f", from + i,
                         ((double *)toBase)[i]);
#endif
        }
    }
}

static
void laik_mpi_exec_actions(Laik_ActionSeq* as, Laik_SwitchStat* ss)
{
    assert(as->actionCount > 0);

    laik_log(LAIK_LL_Debug, "MPI backend: exec %d actions\n", as->actionCount);

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
        if (laik_log_begin(LAIK_LL_Debug)) {
            laik_log_Action((Laik_Action*) a, tc);
            laik_log_flush(0);
        }

        switch(a->type) {
        case LAIK_AT_Send:
            MPI_Send(fromList->map[a->mapNo].base + a->offset, a->count,
                     dataType, a->peer_rank, tag, comm);
            break;

        case LAIK_AT_SendBuf:
            MPI_Send(a->fromBuf, a->count,
                     dataType, a->peer_rank, tag, comm);
            break;

        case LAIK_AT_Recv:
            MPI_Recv(toList->map[a->mapNo].base + a->offset, a->count,
                     dataType, a->peer_rank, tag, comm, &st);
            break;

        case LAIK_AT_RecvBuf:
            MPI_Recv(a->toBuf, a->count,
                     dataType, a->peer_rank, tag, comm, &st);
            break;

        case LAIK_AT_CopyFromBuf:
            for(int i = 0; i < a->count; i++)
                memcpy(a->ce[i].ptr,
                       as->buf + a->ce[i].offset,
                       a->ce[i].bytes);
            break;

        case LAIK_AT_CopyToBuf:
            for(int i = 0; i < a->count; i++)
                memcpy(as->buf + a->ce[i].offset,
                       a->ce[i].ptr,
                       a->ce[i].bytes);
            break;

        case LAIK_AT_PackAndSend:
            laik_mpi_exec_packAndSend(a, dims, dataType, tag, comm);
            break;

        case LAIK_AT_RecvAndUnpack:
            laik_mpi_exec_recvAndUnpack(a, dims, elemsize, dataType, tag, comm);
            break;

        case LAIK_AT_Reduce:
            laik_mpi_exec_reduce(a, dataType, comm);
            break;

        case LAIK_AT_GroupReduce:
            laik_mpi_exec_groupReduce(tc, a, dataType, comm);
            break;

        default: assert(0);
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

    laik_log(LAIK_LL_Debug,
             "MPI backend: %s transition (data '%s', group %d:%d/%d)\n" "    actions: %d reductions, %d sends, %d recvs",
             record ? "record" : "execute", data->name, group->gid, myid,
             group->size, t->redCount, t->sendCount, t->recvCount);

    if (myid < 0) {
        // this task is not part of the communicator to use
        return;
    }

    MPIGroupData* gd = mpiGroupData(group);
    assert(gd); // must have been updated by laik_mpi_updateGroup()
    MPI_Comm comm = gd->comm;
    MPI_Status status;

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

                if (toMap->base == 0) {
                    laik_allocateMap(toMap, ss);
                    assert(toMap->base != 0);
                }
            }

            char* fromBase = fromMap ? fromMap->base : 0;
            char* toBase = toMap ? toMap->base : 0;
            uint64_t elemCount = to - from;
            uint64_t byteCount = elemCount * data->elemsize;

            // if current task is receiver, toBase should be allocated
            if (laik_isInGroup(t, op->outputGroup, myid))
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

            // if neither input nor output are all-groups: manual reduction
            if ((op->inputGroup >= 0) && (op->outputGroup >= 0)) {

                if (laik_log_begin(LAIK_LL_Debug)) {
                    laik_log_append("    %s manual reduction: (%lld - %lld) slc/map %d/%d",
                                    record ? "record":"execute",
                                    (long long int) from, (long long int) to,
                                    op->myInputSliceNo, op->myInputMapNo);
                    laik_log_flush(0);
                }

                if (record)
                    laik_actions_addGroupReduce(as,
                                                     op->inputGroup, op->outputGroup,
                                                     fromBase, toBase, elemCount, op->redOp);
                else {
                    // do the manual reduction on smallest rank of output group
                    int reduceTask = t->subgroup[op->outputGroup].task[0];
                    laik_log(LAIK_LL_Debug, "reduce at T%d", reduceTask);


                    if (reduceTask == myid) {
                        TaskGroup* tg;

                        // collect values from tasks in input group
                        tg = &(t->subgroup[op->inputGroup]);
                        // check that bufsize is enough
                        assert(tg->count * byteCount < PACKBUFSIZE);

                        char* ptr[32], *p;
                        assert(tg->count <= 32);
                        p = packbuf;
                        int myIdx = -1;
                        for(int i = 0; i< tg->count; i++) {
                            if (tg->task[i] == myid) {
                                ptr[i] = fromBase;
                                myIdx = i;

#ifdef LOG_DOUBLE_VALUES
                                assert(d->elemsize == 8);
                                for(uint64_t i = 0; i < elemCount; i++)
                                    laik_log(LAIK_LL_Debug,
                                             "    have at %d: %f", from + i,
                                             ((double *)fromBase)[i]);
#endif
#ifdef LOG_FLOAT_VALUES
                                assert(d->elemsize == 4);
                                for(uint64_t i = 0; i < elemCount; i++)
                                    laik_log(LAIK_LL_Debug,
                                             "    have at %d: %f", from + i,
                                             (double)((float *)fromBase)[i]);
#endif
                                continue;
                            }

                            laik_log(LAIK_LL_Debug,
                                     "  MPI_Recv from T%d (buf off %lld)",
                                     tg->task[i],
                                     (long long int)(p - packbuf));

                            ptr[i] = p;
                            MPI_Recv(p, elemCount, mpiDataType,
                                     tg->task[i], 1, comm, &status);
#ifdef LOG_DOUBLE_VALUES
                            assert(d->elemsize == 8);
                            for(uint64_t i = 0; i < elemCount; i++)
                                laik_log(LAIK_LL_Debug, "    got at %d: %f",
                                         from + i, ((double *)p)[i]);
#endif
                            p += byteCount;
                        }

                        // toBase may be same as fromBase (= our values).
                        // e.g. when we are 3rd task (ptr[3] == fromBase), we
                        // would overwrite our values. Swap ptr[0] with ptr[3].
                        if (myIdx >= 0) {
                            assert(ptr[myIdx] == fromBase);
                            ptr[myIdx] = ptr[0];
                            ptr[0] = fromBase;
                        }

                        // do the reduction, put result back to my input buffer
                        if (data->type->reduce) {
                            assert(tg->count > 1);


                            (data->type->reduce)(toBase, ptr[0], ptr[1],
                                    elemCount, op->redOp);
                            for(int t = 2; t < tg->count; t++)
                                (data->type->reduce)(toBase, toBase, ptr[t],
                                                  elemCount, op->redOp);
                        }
                        else {
                            laik_log(LAIK_LL_Panic,
                                     "Need reduce function for type '%s'. Not set!",
                                     data->type->name);
                            assert(0);
                        }

#ifdef LOG_DOUBLE_VALUES
                        assert(d->elemsize == 8);
                        for(uint64_t i = 0; i < elemCount; i++)
                            laik_log(LAIK_LL_Debug, "    sum at %d: %f",
                                     from + i, ((double *)toBase)[i]);
#endif

                        // send result to tasks in output group
                        tg = &(t->subgroup[op->outputGroup]);
                        for(int i = 0; i< tg->count; i++) {
                            if (tg->task[i] == myid) {
                                // that's myself: nothing to do
                                continue;
                            }

                            laik_log(LAIK_LL_Debug,
                                     "  MPI_Send result to T%d", tg->task[i]);

                            MPI_Send(toBase, elemCount, mpiDataType,
                                     tg->task[i], 1, comm);
                        }
                    }
                    else {
                        if (laik_isInGroup(t, op->inputGroup, myid)) {
                            laik_log(LAIK_LL_Debug, "  MPI_Send to T%d",
                                     reduceTask);

#ifdef LOG_DOUBLE_VALUES
                            assert(d->elemsize == 8);
                            for(uint64_t i = 0; i < elemCount; i++)
                                laik_log(LAIK_LL_Debug, "    at %d: %f",
                                         from + i, ((double *)fromBase)[i]);
#endif

                            MPI_Send(fromBase, elemCount, mpiDataType,
                                     reduceTask, 1, comm);
                        }
                        if (laik_isInGroup(t, op->outputGroup, myid)) {
                            laik_log(LAIK_LL_Debug, "  MPI_Recv from T%d",
                                     reduceTask);

                            MPI_Recv(toBase, elemCount, mpiDataType,
                                     reduceTask, 1, comm, &status);
#ifdef LOG_DOUBLE_VALUES
                            assert(d->elemsize == 8);
                            for(uint64_t i = 0; i < elemCount; i++)
                                laik_log(LAIK_LL_Debug, "    at %d: %f",
                                         from + i, ((double *)toBase)[i]);
#endif
                        }
                    }
                }
            }
            else {
                // not handled yet: either input or output is all-group
                assert((op->inputGroup == -1) || (op->outputGroup == -1));

                MPI_Op mpiRedOp = getMPIOp(op->redOp);

                int rootTask;
                if (op->outputGroup == -1) rootTask = -1;
                else {
                    // TODO: support more then 1 receiver
                    assert(t->subgroup[op->outputGroup].count == 1);
                    rootTask = t->subgroup[op->outputGroup].task[0];
                }

                if (laik_log_begin(LAIK_LL_Debug)) {
                    laik_log_append("    %s reduce (root ",
                                    record ? "record":"execute");
                    if (rootTask == -1)
                        laik_log_append("ALL");
                    else
                        laik_log_append("%d", rootTask);
                    if (fromBase == toBase)
                        laik_log_append(", IN_PLACE");
                    laik_log_flush("): (%ld - %ld) in %d/%d out %d/%d (slc/map), "
                                   "elemsize %d, baseptr from/to %p/%p\n",
                                   from, to,
                                   op->myInputSliceNo, op->myInputMapNo,
                                   op->myOutputSliceNo, op->myOutputMapNo,
                                   data->elemsize, fromBase, toBase);
                }

#ifdef LOG_DOUBLE_VALUES
                if (fromBase) {
                    assert(d->elemsize == 8);
                    for(uint64_t i = 0; i < elemCount; i++)
                        laik_log(LAIK_LL_Debug, "    before at %d: %f",
                                 from + i, ((double *)fromBase)[i]);
                }
#endif

                if (record)
                    laik_actions_addReduce(as, fromBase, toBase, to - from,
                                           rootTask, op->redOp);
                else {
                    if (rootTask == -1) {
                        if (fromBase == toBase)
                            MPI_Allreduce(MPI_IN_PLACE, toBase, to - from,
                                          mpiDataType, mpiRedOp, comm);
                        else
                            MPI_Allreduce(fromBase, toBase, to - from,
                                          mpiDataType, mpiRedOp, comm);
                    }
                    else {
                        if (fromBase == toBase)
                            MPI_Reduce(MPI_IN_PLACE, toBase, to - from,
                                       mpiDataType, mpiRedOp, rootTask, comm);
                        else
                            MPI_Reduce(fromBase, toBase, to - from,
                                       mpiDataType, mpiRedOp, rootTask, comm);
                    }

#ifdef LOG_DOUBLE_VALUES
                    if (toBase) {
                        assert(d->elemsize == 8);
                        for(uint64_t i = 0; i < elemCount; i++)
                            laik_log(LAIK_LL_Debug, "    after at %d: %f",
                                     from + i, ((double *)toBase)[i]);
                    }
#endif
                }
            }

            if ((record == 0) && ss) {
                ss->reduceCount++;
                ss->reducedBytes += (to - from) * data->elemsize;
            }
        }
    }

    // use 2x <task count> phases to avoid deadlocks
    // - count phases X: 0..<count-1>
    //     - receive from <task X> if <task X> lower rank
    //     - send to <task X> if <task X> is higher rank
    // - count phases Y: 0..<count-1>
    //     - receive from <task count-Y> if it is higher rank
    //     - send to <task count-1-Y> if it is lower rank
    //
    // TODO: prepare communication schedule with sorted transitions actions!

    int count = group->size;
    for(int phase = 0; phase < 2*count; phase++) {
        int task = (phase < count) ? phase : (2*count-phase-1);
        bool sendToHigher   = (phase < count);
        bool recvFromLower  = (phase < count);
        bool sendToLower    = (phase >= count);
        bool recvFromHigher = (phase >= count);

        // receive
        for(int i=0; i < t->recvCount; i++) {
            struct recvTOp* op = &(t->recv[i]);
            if (task != op->fromTask) continue;
            if (recvFromLower  && (myid < task)) continue;
            if (recvFromHigher && (myid > task)) continue;

            if (laik_log_begin(LAIK_LL_Debug)) {
                laik_log_append("    %s recv ", record ? "record":"execute");
                laik_log_Slice(dims, &(op->slc));
                laik_log_flush(" from T%d", op->fromTask);
            }

            assert(myid != op->fromTask);

            assert(op->mapNo < toList->count);
            Laik_Mapping* toMap = &(toList->map[op->mapNo]);
            assert(toMap != 0);
            if (toMap->base == 0) {
                // space not yet allocated
                laik_allocateMap(toMap, ss);
                assert(toMap->base != 0);
            }

            MPI_Status s;
            uint64_t count;

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

                laik_log(LAIK_LL_Debug,
                         "  direct recv to local [%lld;%lld[, slc/map %d/%d, " "elemsize %d, baseptr %p\n",
                         (long long int)from, (long long int)to, op->sliceNo,
                         op->mapNo, data->elemsize, (void *)toMap->base);

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
                    laik_actions_addRecvBuf(as, toMap->base + from * data->elemsize,
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
                    laik_actions_addRecvAndUnpack(as, toMap,
                                                  &(op->slc), op->fromTask);
                else {
                    Laik_Index idx = op->slc.from;
                    uint64_t size = laik_slice_size(dims, &(op->slc));
                    assert(size > 0);
                    int recvCount, unpacked;
                    count = 0;
                    while(1) {
                        MPI_Recv(packbuf, PACKBUFSIZE / data->elemsize,
                                 mpiDataType, op->fromTask, 1, comm, &s);
                        MPI_Get_count(&s, mpiDataType, &recvCount);
                        unpacked = (toMap->layout->unpack)(toMap, &(op->slc), &idx,
                                                           packbuf,
                                                           recvCount * data->elemsize);
                        assert(recvCount == unpacked);
                        count += unpacked;
                        if (laik_index_isEqual(dims, &idx, &(op->slc.to))) break;
                    }
                    assert(count == size);
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

            if (laik_log_begin(LAIK_LL_Debug)) {
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

            uint64_t count;
            MPI_Datatype mpiDataType = getMPIDataType(data);

            if (dims == 1) {
                // we directly support 1d data layouts

                // from global to sender-local indexes
                int64_t from = op->slc.from.i[0] - fromMap->requiredSlice.from.i[0];
                int64_t to   = op->slc.to.i[0] - fromMap->requiredSlice.from.i[0];
                assert(from >= 0);
                assert(to > from);
                count = to - from;

                laik_log(LAIK_LL_Debug,
                         "  direct send: from local [%lld;%lld[, slice/map %d/%d, " "elemsize %d, baseptr %p\n",
                         (long long int)from, (long long int)to, op->sliceNo,
                         op->mapNo, data->elemsize, (void *)fromMap->base);

                if (record)
                    laik_actions_addSendBuf(as, fromMap->base + from * data->elemsize,
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
                    laik_actions_addPackAndSend(as, fromMap,
                                                &(op->slc), op->toTask);
                else {
                    Laik_Index idx = op->slc.from;
                    uint64_t size = laik_slice_size(dims, &(op->slc));
                    assert(size > 0);
                    int packed;
                    count = 0;
                    while(1) {
                        packed = (fromMap->layout->pack)(fromMap, &(op->slc), &idx,
                                                         packbuf, PACKBUFSIZE);
                        assert(packed > 0);
                        MPI_Send(packbuf, packed,
                                 mpiDataType, op->toTask, 1, comm);
                        count += packed;
                        if (laik_index_isEqual(dims, &idx, &(op->slc.to))) break;
                    }
                    assert(count == size);
                }
            }

            if ((record == 0) && ss) {
                ss->sendCount++;
                ss->sentBytes += count * data->elemsize;
            }
        }
    
    }
}


static
Laik_ActionSeq* laik_mpi_prepare(Laik_Data* d, Laik_Transition* t,
                                 Laik_MappingList* fromList,
                                 Laik_MappingList* toList)
{
    Laik_ActionSeq* as = laik_actions_new(d->space->inst);
    laik_actions_addTContext(as, d, t, fromList, toList);
    laik_execOrRecord(true, d, t, as, fromList, toList);

    if (laik_log_begin(LAIK_LL_Debug)) {
        laik_log_ActionSeq(as);
        laik_log_flush(0);
    }
    //return as;

    Laik_ActionSeq* as2 = laik_actions_cloneSeq(as);
    laik_actions_optSeq(as, as2);
    //laik_actions_copySeq(as, as2);
    laik_actions_free(as);

    if (laik_log_begin(LAIK_LL_Debug)) {
        laik_log_append("After optimization:\n");
        laik_log_ActionSeq(as2);
        laik_log_flush(0);
    }

    return as2;
}

static void laik_mpi_cleanup(Laik_ActionSeq* as)
{
    laik_actions_free(as);
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
    assert(as->actionCount > 0);

    laik_mpi_exec_actions(as, d->stat);
}


#endif // USE_MPI
