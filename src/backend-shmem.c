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
void laik_shmem_panic(int err) {
    laik_log(LAIK_LL_Panic, "SHMEM backend: error '%d'", err);
    exit(1);
}


//----------------------------------------------------------------------------
// backend interface implementation: initialization

Laik_Instance* laik_init_shmem(int* argc, char*** argv)
{
    argc = argc;
    argv = argv; // TODO remove if they weren't used in a later version
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

static void laik_shmem_finalize(Laik_Instance* inst){
    // TODO: Implement needed finalize routines.
    assert(inst == shmem_instance);
}

// calc statistics updates for MPI-specific actions
static void laik_shmem_aseq_calc_stats(Laik_ActionSeq* as){
    as = as; // TODO: implement
}

static void laik_shmem_prepare(Laik_ActionSeq* as){
    // TODO: Adjust method where necessary
    if (laik_log_begin(1)) {
        laik_log_append("SHMEM backend prepare:\n");
        laik_log_ActionSeq(as, false);
        laik_log_flush(0);
    }

    // mark as prepared by SHMEM backend: for SHMEM-specific cleanup + action logging
    as->backend = &laik_backend_shmem;

    bool changed = laik_aseq_splitTransitionExecs(as);
    laik_log_ActionSeqIfChanged(changed, as, "After splitting transition execs");
    if (as->actionCount == 0) {
        laik_aseq_calc_stats(as);
        return;
    }

    changed = laik_aseq_flattenPacking(as);
    laik_log_ActionSeqIfChanged(changed, as, "After flattening actions");

    // TODO add something similar to mpi_reduce if needed

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

    // TODO add something similar to mpi_async if needed
    laik_aseq_freeTempSpace(as);

    laik_aseq_calc_stats(as);
    laik_shmem_aseq_calc_stats(as);
}

static void laik_shmem_cleanup(Laik_ActionSeq* as){
    if (laik_log_begin(1)) {
        laik_log_append("SHMEM backend cleanup:\n");
        laik_log_ActionSeq(as, false);
        laik_log_flush(0);
    }

    assert(as->backend == &laik_backend_shmem);

    //TODO: implement cleanup routines if necessary
}

static void laik_shmem_exec(Laik_ActionSeq* as){
    if (as->actionCount == 0) {
        laik_log(1, "SHMEM backend exec: nothing to do\n");
        return;
    }

    if (as->backend == 0) {
        // no preparation: do minimal transformations, sorting send/recv
        laik_log(1, "SHMEM backend exec: prepare before exec\n");
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
        assert(not_handled == 0); // there should be no SHMEM-specific actions
    }

    if (laik_log_begin(1)) {
        laik_log_append("SHMEM backend exec:\n");
        laik_log_ActionSeq(as, false);
        laik_log_flush(0);
    }

    // TODO: use transition context given by each action
    /* TODO: uncomment when it will be used, commented in to supress warnings
    Laik_TransitionContext* tc = as->context[0];
    Laik_MappingList* fromList = tc->fromList;
    Laik_MappingList* toList = tc->toList;
    int elemsize = tc->data->elemsize;
    */

    //TODO: Implement needed variables

    Laik_Action* a = as->action;
    for(unsigned int i = 0; i < as->actionCount; i++, a = nextAction(a)) {
        // Laik_BackendAction* ba = (Laik_BackendAction*) a; TODO: Uncomment when it will be used.
        if (laik_log_begin(1)) {
            laik_log_Action(a, as);
            laik_log_flush(0);
        }

        switch(a->type) {
            case LAIK_AT_BufReserve:
            case LAIK_AT_Nop:
                // no need to do anything
                break;

            case 0: {
                // TODO: Implement the handling of all the different Laik_Actions
                break;
            }
        }
    }
    assert( ((char*)as->action) + as->bytesUsed == ((char*)a) );
}

static void laik_shmem_updateGroup(Laik_Group* g){
    // calculate SHMEM communicator for group <g>
    // TODO: only supports shrinking of parent for now
    assert(g->parent);
    assert(g->parent->size >= g->size);

    laik_log(1, "SHMEM backend updateGroup: parent %d (size %d, myid %d) "
             "=> group %d (size %d, myid %d)",
             g->parent->gid, g->parent->size, g->parent->myid,
             g->gid, g->size, g->myid);

    // only interesting if this task is still part of parent
    if (g->parent->myid < 0) return;

    //TODO: implement updateGroup functionality
}

static bool laik_shmem_log_action(Laik_Action* a){
    // TODO: Insert needed action types.
    switch(a->type) {
        case 0: {
            laik_log_append("Log the event");
            break;
        }

        default:
            return false;
    }
    a = a;
    return true;
}

static void laik_shmem_sync(Laik_KVStore* kvs){
    // TODO: Add SHMEM related variables
    assert(kvs->inst == shmem_instance);
    Laik_Group* world = kvs->inst->world;
    int myid = world->myid;
    int count[2] = {0,0};
    //int err = 0; TODO: Uncomment when it will be used.

    if (myid > 0) {
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
        //TODO: Implement receiving of data
        laik_log(1, "SHMEM sync: getting %d changes (total %d chars) from T%d",
                 count[0] / 2, count[1], i);
        laik_kvs_changes_set_size(&recvd, 0, 0); // fresh reuse
        laik_kvs_changes_ensure_size(&recvd, count[0], count[1]);
        if (count[0] == 0) {
            assert(count[1] == 0);
            continue;
        }

        assert(count[1] > 0);
        //TODO: Implement receiving of data
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
        laik_log(1, "SHMEM sync: sending %d changes (total %d chars) to T%d",
                 count[0] / 2, count[1], i);
        // TODO: Implement sending back of data
    }

    // TODO: opt - remove own changes from received ones
    laik_kvs_changes_apply(dst, kvs);

    laik_kvs_changes_free(&recvd);
    laik_kvs_changes_free(&changes);
}

#endif // USE_SHMEM