/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"
#include "laik-backend-single.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// forward decl
void laik_single_exec(Laik_Data* d, Laik_Transition* t, Laik_TransitionPlan* p,
                      Laik_MappingList* fromList, Laik_MappingList* toList);

// C guarantees that unset function pointers are NULL
static Laik_Backend laik_backend_single = {
    .name = "Single Process Backend Driver",
    .exec = laik_single_exec
};

static Laik_Instance* single_instance = 0;

Laik_Instance* laik_init_single()
{
    if (single_instance)
        return single_instance;

    Laik_Instance* inst;
    inst = laik_new_instance(&laik_backend_single, 1, 0, "local", 0);

    // group world
    Laik_Group* g = laik_create_group(inst);
    g->inst = inst;
    g->gid = 0;
    g->size = 1;
    g->myid = 0;

    laik_log(1, "Single backend initialized\n");

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

void laik_single_exec(Laik_Data* d, Laik_Transition* t, Laik_TransitionPlan* p,
                      Laik_MappingList* fromList, Laik_MappingList* toList)
{
    assert(p == 0); // does not support transition plans

    Laik_Instance* inst = d->space->inst;
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
            assert(laik_isInGroup(t, op->outputGroup, inst->myid));
            assert(toBase != 0);
            assert(to > from);

            laik_log(1, "Single reduce: "
                        "from %ld, to %ld, elemsize %d, base from/to %p/%p\n",
                     from, to, d->elemsize, fromBase, toBase);

            memcpy(toBase, fromBase, (to-from) * fromMap->data->elemsize);
        }
    }

    // the single backend should never need to do send/recv actions
    assert(t->recvCount == 0);
    assert(t->sendCount == 0);
}
