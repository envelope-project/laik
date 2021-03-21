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


#include "laik-internal.h"
#include "laik-backend-single.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// forward decl
void laik_single_exec(Laik_ActionSeq* as);
void laik_single_sync(Laik_KVStore* kvs);

// C guarantees that unset function pointers are NULL
static Laik_Backend laik_backend_single = {
    .name = "Single Process Backend Driver",
    .exec = laik_single_exec,
    .sync = laik_single_sync
};

static Laik_Instance* single_instance = 0;

Laik_Instance* laik_init_single()
{
    if (single_instance)
        return single_instance;

    Laik_Instance* inst;
    inst = laik_new_instance(&laik_backend_single, 1, 0, 0, 0, "local", 0);

    // create and attach initial world group
    Laik_Group* world = laik_create_group(inst, 1);
    world->size = 1;
    world->myid = 0;
    world->locationid[0] = 0;
    inst->world = world;

    laik_log(2, "Single backend initialized\n");

    single_instance = inst;
    return inst;
}

Laik_Group* laik_single_world()
{
    if (!single_instance)
        laik_init_single();

    assert(single_instance->group_count > 0);
    return single_instance->group[0];
}

void laik_single_exec(Laik_ActionSeq* as)
{
    if (as->backend == 0) {
        as->backend = &laik_backend_single;
        laik_aseq_calc_stats(as);
    }
    // we only support 1 transition exec action
    assert(as->actionCount == 1);
    assert(as->action[0].type == LAIK_AT_TExec);
    Laik_TransitionContext* tc = as->context[0];
    Laik_Data* d = tc->data;
    Laik_Transition* t = tc->transition;
    Laik_MappingList* fromList = tc->fromList;
    Laik_MappingList* toList = tc->toList;

    if (t->redCount > 0) {
        assert(fromList->count == 1);
        assert(toList->count == 1);
        Laik_Mapping* fromMap = &(fromList->map[0]);
        Laik_Mapping* toMap = &(toList->map[0]);
        char* fromBase = fromMap ? fromMap->base : 0;
        char* toBase = toMap ? toMap->base : 0;

        for(int i=0; i < t->redCount; i++) {
            assert(d->space->dims == 1);
            struct redTOp* op = &(t->red[i]);
            int64_t from = op->slc.from.i[0];
            int64_t to   = op->slc.to.i[0];
            assert(fromBase != 0);
            assert(laik_trans_isInGroup(t, op->outputGroup, t->group->myid));
            assert(toBase != 0);
            assert(to > from);

            laik_log(1, "Single reduce: "
                        "from %lld, to %lld, elemsize %d, base from/to %p/%p\n",
                     (long long int) from, (long long int) to,
                     d->elemsize, (void*) fromBase, (void*) toBase);

            memcpy(toBase, fromBase, (to-from) * fromMap->data->elemsize);
        }
    }

    // the single backend should never need to do send/recv actions
    assert(t->recvCount == 0);
    assert(t->sendCount == 0);
}

void laik_single_sync(Laik_KVStore* kvs)
{
    // nothing to do
    (void) kvs;
}
