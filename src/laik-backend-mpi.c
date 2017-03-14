/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"
#include "laik-backend-mpi.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

void laik_mpi_finalize();
void laik_mpi_execTransition(Laik_Transition *t);

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

Laik_Instance* laik_init_mpi(int* argc, char*** argv)
{
    if (mpi_instance) return mpi_instance;

    if (argc)
        MPI_Init(argc, argv);

    int world_size, world_rank;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    Laik_Instance* inst;
    inst = laik_new_instance(&laik_backend_mpi);
    inst->size = world_size;
    inst->myid = world_rank;

    // group world
    Laik_Group* g = laik_create_group(inst);
    g->inst = inst;
    g->gid = 0;
    g->count = inst->size;
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

void laik_mpi_finalize()
{
    MPI_Finalize();
}

void laik_mpi_execTransition(Laik_Transition* t)
{
    // TODO
}

#endif // LAIK_USEMPI
