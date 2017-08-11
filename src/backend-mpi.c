/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"
#include "laik-backend-mpi.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#ifndef USE_MPI

Laik_Instance* laik_init_mpi(int* argc, char*** argv)
{
    printf("ERROR: LAIK is not compiled with MPI backend.");
    exit(1);
}

void laik_mpi_finalize() {}

#else // USE_MPI

#include <mpi.h>

void laik_mpi_finalize();
void laik_mpi_execTransition(Laik_Data* d, Laik_Transition *t,
                             Laik_MappingList* fromList,
                             Laik_MappingList* toList);
// update backend specific data for group if needed
void laik_mpi_updateGroup(Laik_Group*);

static Laik_Backend laik_backend_mpi = {"MPI Backend",
                                        laik_mpi_finalize,
                                        laik_mpi_execTransition,
                                        laik_mpi_updateGroup};
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

Laik_Instance* laik_init_mpi(int* argc, char*** argv)
{
    if (mpi_instance) return mpi_instance;

    MPIData* d = (MPIData*) malloc(sizeof(MPIData));
    d->didInit = false;
    d->comm = MPI_COMM_WORLD;

    if (argc) {
        MPI_Init(argc, argv);
        d->didInit = true;
    }

    int size, rank;
    MPI_Comm_size(d->comm, &size);
    MPI_Comm_rank(d->comm, &rank);

    // Get the name of the processor
    char processor_name[MPI_MAX_PROCESSOR_NAME];
    int name_len;
    MPI_Get_processor_name(processor_name, &name_len);

    Laik_Instance* inst;
    inst = laik_new_instance(&laik_backend_mpi, size, rank,
                             processor_name, d);

    // group world

    MPIGroupData* gd = (MPIGroupData*) malloc(sizeof(MPIGroupData));
    gd->comm = MPI_COMM_WORLD;

    Laik_Group* g = laik_create_group(inst);
    g->inst = inst;
    g->gid = 0;
    g->size = inst->size;
    g->myid = inst->myid;
    g->backend_data = gd;

    laik_log(1, "MPI backend initialized (location '%s')\n", inst->mylocation);

    // for intentionally buggy MPI backend behavior
    char* str = getenv("LAIK_MPI_BUG");
    if (str) mpi_bug = atoi(str);

    mpi_instance = inst;
    return inst;
}

MPIData* mpiData(Laik_Instance* i)
{
    return (MPIData*) i->backend_data;
}

MPIGroupData* mpiGroupData(Laik_Group* g)
{
    return (MPIGroupData*) g->backend_data;
}

void laik_mpi_finalize()
{
    if (mpiData(mpi_instance)->didInit)
        MPI_Finalize();
}

// calculate MPI communicator for group <g>
void laik_mpi_updateGroup(Laik_Group* g)
{
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
    gd = (MPIGroupData*) malloc(sizeof(MPIGroupData));
    g->backend_data = gd;

    laik_log(1, "MPI Comm_split: old myid %d => new myid %d",
             g->parent->myid, g->fromParent[g->parent->myid]);

    MPI_Comm_split(gdParent->comm,
                   (g->fromParent[g->parent->myid] < 0) ? MPI_UNDEFINED : 0,
                   g->parent->myid, &(gd->comm));
}


void laik_mpi_execTransition(Laik_Data* d, Laik_Transition* t,
                             Laik_MappingList* fromList, Laik_MappingList* toList)
{
    int myid  = d->group->myid;
    Laik_SwitchStat* ss = d->stat;

    laik_log(1, "MPI backend execute transition:\n"
             "  data '%s', group %d (size %d, myid %d)\n"
             "  actions: %d reductions, %d sends, %d recvs",
             d->name, d->group->gid, d->group->size, myid,
             t->redCount, t->sendCount, t->recvCount);

    if (myid < 0) {
        // this task is not part of the communicator to use
        return;
    }

    Laik_Instance* inst = d->group->inst;
    MPIGroupData* gd = mpiGroupData(d->group);
    assert(gd); // must have been updated by laik_mpi_updateGroup()
    MPI_Comm comm = gd->comm;

    if (t->redCount > 0) {
        assert(d->space->dims == 1);
        for(int i=0; i < t->redCount; i++) {
            struct redTOp* op = &(t->red[i]);
            uint64_t from = op->slc.from.i[0];
            uint64_t to   = op->slc.to.i[0];

            // reductions go over all indexes => one slice covering all
            assert((fromList != 0) && (fromList->count == 1));
            Laik_Mapping* fromMap = &(fromList->map[0]);
            char* fromBase = fromMap ? fromMap->base : 0;

            // result goes to all tasks or just one => toList may be 0
            if (toList)
                assert(toList->count == 1);
            Laik_Mapping* toMap = toList ? &(toList->map[0]) : 0;
            if (toMap && (toMap->base == 0)) {
                // we can do IN_PLACE reduction
                toMap->base = fromBase;
                if (fromMap) fromMap->base = 0; // take over memory
            }
            char* toBase = toMap ? toMap->base : 0;

            assert(fromBase != 0);
            // if current task is receiver, toBase should be allocated
            if ((op->rootTask == -1) || (op->rootTask == inst->myid))
                assert(toBase != 0);

            MPI_Op mpiRedOp;
            switch(op->redOp) {
            case LAIK_RO_Sum: mpiRedOp = MPI_SUM; break;
            default: assert(0);
            }

            MPI_Datatype mpiDataType;
            if      (d->type == laik_Double) mpiDataType = MPI_DOUBLE;
            else if (d->type == laik_Float) mpiDataType = MPI_FLOAT;
            else assert(0);

            if (laik_logshown(1)) {
                char rootstr[10];
                sprintf(rootstr, "%d", op->rootTask);
                laik_log(1, "MPI Reduce (root %s%s): from %lu, to %lu, "
                            "elemsize %d, baseptr from/to %p/%p\n",
                         (op->rootTask == -1) ? "ALL" : rootstr,
                         (fromBase == toBase) ? ", IN_PLACE" : "",
                         from, to, d->elemsize, fromBase, toBase);
            }

            if (ss) {
                ss->reduceCount++;
                ss->reducedBytes += (to - from) * d->elemsize;
            }

            if (op->rootTask == -1) {
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
                               mpiDataType, mpiRedOp, op->rootTask, comm);
                else
                    MPI_Reduce(fromBase, toBase, to - from,
                               mpiDataType, mpiRedOp, op->rootTask, comm);
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

    int count = d->group->size;
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

            assert(myid != op->fromTask);
            assert(d->space->dims == 1);

            assert(op->sliceNo < toList->count);
            Laik_Mapping* toMap = &(toList->map[op->sliceNo]);
            if (toMap) laik_allocateMap(toMap, ss);
            char* toBase = toMap ? toMap->base : 0;

            // from global to receiver-local indexes
            uint64_t from = op->slc.from.i[0] - toMap->baseIdx.i[0];
            uint64_t to   = op->slc.to.i[0] - toMap->baseIdx.i[0];
            assert(toBase != 0);

            MPI_Datatype mpiDataType;
            if      (d->type == laik_Double) mpiDataType = MPI_DOUBLE;
            else if (d->type == laik_Float) mpiDataType = MPI_FLOAT;
            else assert(0);

            laik_log(1, "MPI Recv from T%d: global [%lu;%lu[,\n"
                        "  to local [%lu;%lu[ in slice %d, elemsize %d, baseptr %p\n",
                     op->fromTask,
                     op->slc.from.i[0], op->slc.to.i[0],
                     from, to, op->sliceNo, d->elemsize, toBase);

            if (ss) {
                ss->recvCount++;
                ss->receivedBytes += (to - from) * d->elemsize;
            }

            MPI_Status s;
            // TODO:
            // - tag 1 may conflict with application
            // - check status

            if (mpi_bug > 0) {
                // intentional bug: ignore small amounts of data received
                if (to-from < 1000) {
                    char dummy[8000];
                    MPI_Recv(dummy, to - from,
                             mpiDataType, op->fromTask, 1, comm, &s);
                    continue;
                }
            }

            MPI_Recv(toBase + from * d->elemsize, to - from,
                     mpiDataType, op->fromTask, 1, comm, &s);
        }

        // send
        for(int i=0; i < t->sendCount; i++) {
            struct sendTOp* op = &(t->send[i]);
            if (task != op->toTask) continue;
            if (sendToLower  && (myid < task)) continue;
            if (sendToHigher && (myid > task)) continue;

            assert(myid != op->toTask);
            assert(d->space->dims == 1);

            assert(op->sliceNo < fromList->count);
            Laik_Mapping* fromMap = &(fromList->map[op->sliceNo]);
            char* fromBase = fromMap ? fromMap->base : 0;

            // from global to sender-local indexes
            uint64_t from = op->slc.from.i[0] - fromMap->baseIdx.i[0];
            uint64_t to   = op->slc.to.i[0] - fromMap->baseIdx.i[0];
            assert(fromBase != 0);

            MPI_Datatype mpiDataType;
            if      (d->type == laik_Double) mpiDataType = MPI_DOUBLE;
            else if (d->type == laik_Float) mpiDataType = MPI_FLOAT;
            else assert(0);

            laik_log(1, "MPI Send to T%d: global [%lu;%lu[,\n"
                     "  from local [%lu;%lu[ in slice %d, elemsize %d, baseptr %p\n",
                     op->toTask,
                     op->slc.from.i[0], op->slc.to.i[0],
                     from, to, op->sliceNo, d->elemsize, fromBase);

            if (ss) {
                ss->sendCount++;
                ss->sentBytes += (to - from) * d->elemsize;
            }

            // TODO: tag 1 may conflict with application
            MPI_Send(fromBase + from * d->elemsize, to - from,
                     mpiDataType, op->toTask, 1, comm);
        }
    
    }
}

#endif // USE_MPI
