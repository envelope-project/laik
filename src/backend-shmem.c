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


#ifdef USE_SHMEM

#include "laik-internal.h"
#include "laik-backend-shmem.h"

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

static void laik_shmem_finalize(Laik_Instance* as);
static void laik_shmem_prepare(Laik_ActionSeq* as);
static void laik_shmem_cleanup(Laik_ActionSeq* as);
static void laik_shmem_exec(Laik_ActionSeq* as);
static void laik_shmem_updateGroup(Laik_Group* as);
static bool laik_shmem_log_action(Laik_Action* as);
static void laik_shmem_sync(Laik_KVStore* kvs);

// C guarantees that unset function pointers are NULL
static Laik_Backend laik_backend_shmem = {
    .name        = "SHMEM (two-sided)",
    .finalize    = laik_shmem_finalize,
    .prepare     = laik_shmem_prepare,
    .cleanup     = laik_shmem_cleanup,
    .exec        = laik_shmem_exec,
    .updateGroup = laik_shmem_updateGroup,
    .log_action  = laik_shmem_log_action,
    .sync        = laik_shmem_sync
};

static Laik_Instance* shmem_instance = 0;

//----------------------------------------------------------------------------
// error helpers

static
void laik_shmem_panic(int err)
{
    laik_log(LAIK_LL_Panic, "SHMEM backend: error '%d'", err);
    exit(1);
}


//----------------------------------------------------------------------------
// backend interface implementation: initialization

Laik_Instance* laik_init_shmem(int* argc, char*** argv)
{
    if (shmem_instance)
        return shmem_instance;

    Laik_Instance* inst;
    inst = laik_new_instance(&laik_backend_shmem, 1, 0, 0, 0, "local", 0);

    // create and attach initial world group
    Laik_Group* world = laik_create_group(inst, 1);
    world->size = 1;
    world->myid = 0;
    world->locationid[0] = 0;
    inst->world = world;

    laik_log(2, "SHMEM backend initialized\n");

    shmem_instance = inst;
    return inst;
}

static
void laik_shmem_finalize(Laik_Instance* inst)
{
    assert(inst == shmem_instance);
    return;
}

static
void laik_shmem_prepare(Laik_ActionSeq* as){
    as = as;
    return;
}

static
void laik_shmem_cleanup(Laik_ActionSeq* as){
    as = as;
    return;
}

static
void laik_shmem_exec(Laik_ActionSeq* as){
    as = as;
    return;
}

static
void laik_shmem_updateGroup(Laik_Group* as){
    as = as;
    return;
}

static
bool laik_shmem_log_action(Laik_Action* as){
    as = as;
    return false;
}

static
void laik_shmem_sync(Laik_KVStore* kvs){
    kvs = kvs;
    return;
}

#endif // USE_SHMEM
