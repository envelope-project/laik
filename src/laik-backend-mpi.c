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
void laik_mpi_execTransition(Laik_Data* d, Laik_Transition *t);

static Laik_Backend laik_backend_mpi = {"MPI Backend",
                                        laik_mpi_finalize,
                                        laik_mpi_execTransition };
static Laik_Instance* mpi_instance = 0;

#ifndef LAIK_USEMPI

Laik_Instance* laik_init_mpi(int* argc, char*** argv)
{
    printf("ERROR: LAIK is not compiled with MPI backend.");
    exit(1);
}

void laik_mpi_finalize() {}

#else // LAIK_USEMPI

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

    Laik_Instance* inst;
    inst = laik_new_instance(&laik_backend_mpi, size, rank, d);

    // group world
    Laik_Group* g = laik_create_group(inst);
    g->inst = inst;
    g->gid = 0;
    g->size = inst->size;
    g->myid = inst->myid;
    g->task[0] = 0; // TODO

#ifdef LAIK_DEBUG
    // Get the name of the processor
    char processor_name[MPI_MAX_PROCESSOR_NAME];
    int name_len;
    MPI_Get_processor_name(processor_name, &name_len);

    printf("LAIK %d/%d - MPI backend initialized (host %s)\n",
           inst->myid, inst->size, processor_name);
#endif

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

void laik_mpi_execTransition(Laik_Data* d, Laik_Transition* t)
{
    Laik_Instance* inst = d->space->inst;
    MPI_Comm comm = mpiData(inst)->comm;

    if (t->redCount > 0) {
        for(int i=0; i < t->redCount; i++) {
            assert(d->space->dims == 1);
            uint64_t from = t->red[i].from.i[0];
            uint64_t to   = t->red[i].to.i[0];
            char* base = d->activeMapping->base;
            assert(base != 0);
            MPI_Op mpiRedOp;
            switch(t->redOp[i]) {
            case LAIK_AP_Plus: mpiRedOp = MPI_SUM; break;
            default: assert(0);
            }

            MPI_Datatype mpiDateType;
            switch(d->elemsize) {
            case 8: mpiDateType = MPI_DOUBLE; break;
            default: assert(0);
            }

            // TODO: use group

#ifdef LAIK_DEBUG
            printf("LAIK %d/%d - MPI Reduce: "
                   "from %lu, to %lu, elemsize %d, base %p\n",
                   d->space->inst->myid, d->space->inst->size,
                   from, to, d->elemsize, base);
#endif

            if (t->redRoot[i] == -1) {
                void* fromPtr = base + from * d->elemsize;
                if (inst->myid == 0) fromPtr = MPI_IN_PLACE;
                MPI_Allreduce(fromPtr,
                              base + from * d->elemsize,
                              to - from,
                              mpiDateType, mpiRedOp,
                              comm);
            }
            else {
                void* fromPtr = base + from * d->elemsize;
                if (inst->myid == 0) fromPtr = MPI_IN_PLACE;
                MPI_Reduce(fromPtr,
                           base + from * d->elemsize,
                           to - from,
                           mpiDateType, mpiRedOp,
                           t->redRoot[i], comm);
            }
        }
    }

    // TODO: send / recv
}

#endif // LAIK_USEMPI
