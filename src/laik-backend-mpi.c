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

void laik_mpi_finalize();
void laik_mpi_execTransition(Laik_Data* d, Laik_Transition *t,
                             Laik_MappingList* fromList, Laik_MappingList* toList);

static Laik_Backend laik_backend_mpi = {"MPI Backend",
                                        laik_mpi_finalize,
                                        laik_mpi_execTransition };
static Laik_Instance* mpi_instance = 0;

#ifndef USE_MPI

Laik_Instance* laik_init_mpi(int* argc, char*** argv)
{
    printf("ERROR: LAIK is not compiled with MPI backend.");
    exit(1);
}

void laik_mpi_finalize() {}

#else // USE_MPI

#include <mpi.h>

typedef struct _MPIData MPIData;
struct _MPIData {
    MPI_Comm comm;
    bool didInit;
};

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
    Laik_Group* g = laik_create_group(inst);
    g->inst = inst;
    g->gid = 0;
    g->size = inst->size;
    g->myid = inst->myid;
    g->task[0] = 0; // TODO

    laik_log(1, "MPI backend initialized (location '%s')\n", inst->mylocation);

    mpi_instance = inst;
    return inst;
}

MPIData* mpiData(Laik_Instance* i)
{
    return (MPIData*) i->backend_data;
}

void laik_mpi_finalize()
{
    if (mpiData(mpi_instance)->didInit)
        MPI_Finalize();
}

void laik_mpi_execTransition(Laik_Data* d, Laik_Transition* t,
                             Laik_MappingList* fromList, Laik_MappingList* toList)
{
    Laik_Instance* inst = d->space->inst;
    MPI_Comm comm = mpiData(inst)->comm;

    // TODO: do group != world

    if (t->redCount > 0) {
        assert(d->space->dims == 1);
        for(int i=0; i < t->redCount; i++) {
            struct redTOp* op = &(t->red[i]);
            uint64_t from = op->slc.from.i[0];
            uint64_t to   = op->slc.to.i[0];

            assert(fromList->count == 1);
            Laik_Mapping* fromMap = &(fromList->map[0]);
            char* fromBase = fromMap ? fromMap->base : 0;

            assert(toList->count == 1);
            Laik_Mapping* toMap = &(toList->map[0]);
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

            MPI_Datatype mpiDateType;
            if      (d->type == laik_Double) mpiDateType = MPI_DOUBLE;
            else if (d->type == laik_Float) mpiDateType = MPI_FLOAT;
            else assert(0);

            laik_log(1, "MPI Reduce: "
                        "from %lu, to %lu, elemsize %d, base from/to %p/%p\n",
                     from, to, d->elemsize, fromBase, toBase);

            if (op->rootTask == -1) {
                MPI_Allreduce(fromBase, toBase, to - from,
                              mpiDateType, mpiRedOp, comm);
            }
            else {
                MPI_Reduce(fromBase, toBase, to - from,
                           mpiDateType, mpiRedOp, op->rootTask, comm);
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
    // TODO: sort transitions actions!

    int myid  = d->space->inst->myid;
    int count = d->space->inst->size;
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
            char* toBase = toMap ? toMap->base : 0;

            // from global to receiver-local indexes
            uint64_t from = op->slc.from.i[0] - toMap->baseIdx.i[0];
            uint64_t to   = op->slc.to.i[0] - toMap->baseIdx.i[0];
            assert(toBase != 0);

            MPI_Datatype mpiDateType;
            switch(d->elemsize) {
            case 8: mpiDateType = MPI_DOUBLE; break;
            default: assert(0);
            }

            laik_log(1, "MPI Recv from T%d: "
                        "local [%lu-%lu], elemsize %d, to base %p\n",
                     op->fromTask, from, to-1, d->elemsize, toBase);

            MPI_Status s;
            // TODO:
            // - tag 1 may conflict with application
            // - check status
            MPI_Recv(toBase + from * d->elemsize, to - from,
                     mpiDateType, op->fromTask, 1, comm, &s);
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

            MPI_Datatype mpiDateType;
            switch(d->elemsize) {
            case 8: mpiDateType = MPI_DOUBLE; break;
            default: assert(0);
            }

            laik_log(1, "MPI Send to T%d: "
                        "local [%lu-%lu], elemsize %d, from base %p\n",
                     op->toTask, from, to-1, d->elemsize, fromBase);

            // TODO: tag 1 may conflict with application
            MPI_Send(fromBase + from * d->elemsize, to - from,
                     mpiDateType, op->toTask, 1, comm);
        }
    
    }
}

#endif // USE_MPI
