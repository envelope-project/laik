//
// Created by Vincent Bode on 13/12/2019.
//

#ifndef LAIK_LAIK_BACKEND_MPI_INTERNAL_H
#define LAIK_LAIK_BACKEND_MPI_INTERNAL_H

#include <mpi.h>

// forward decls, types/structs , global variables

static void laik_mpi_finalize(Laik_Instance*);
static void laik_mpi_prepare(Laik_ActionSeq*);
static void laik_mpi_cleanup(Laik_ActionSeq*);
static void laik_mpi_exec(Laik_ActionSeq* as);
static void laik_mpi_updateGroup(Laik_Group*);
static bool laik_mpi_log_action(Laik_Action* a);
static void laik_mpi_sync(Laik_KVStore* kvs);

static void laik_mpi_panic(int err);

Laik_Instance* laik_init_mpi_generic_backend(int* argc, char*** argv, Laik_Backend* backendStruct);

typedef struct {
    MPI_Comm comm;
    bool didInit;
} MPIData;

typedef struct {
    MPI_Comm comm;
} MPIGroupData;

#endif //LAIK_LAIK_BACKEND_MPI_INTERNAL_H
