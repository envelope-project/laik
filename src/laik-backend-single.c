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
void laik_single_execTransition(Laik_Data* d, Laik_Transition* t,
                                Laik_Mapping* toMap);

static Laik_Backend laik_backend_single = {"Single Task Backend", 0,
                                           laik_single_execTransition };
static Laik_Instance* single_instance = 0;

Laik_Instance* laik_init_single()
{
    if (single_instance)
        return single_instance;

    Laik_Instance* inst;
    inst = laik_new_instance(&laik_backend_single, 1, 0, 0);

    // group world
    Laik_Group* g = laik_create_group(inst);
    g->inst = inst;
    g->gid = 0;
    g->size = 1;
    g->myid = 0;
    g->task[0] = 0;

#ifdef LAIK_DEBUG
    printf("LAIK %d/%d - Single backend initialized\n",
           inst->myid, inst->size);
#endif

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

void laik_single_execTransition(Laik_Data* d, Laik_Transition* t,
                                Laik_Mapping* toMap)
{
    Laik_Instance* inst = d->space->inst;
    Laik_Mapping* fromMap = d->activeMapping;
    char* fromBase = fromMap->base;
    char* toBase = toMap->base;

    for(int i=0; i < t->redCount; i++) {
        assert(d->space->dims == 1);
        uint64_t from = t->red[i].from.i[0];
        uint64_t to   = t->red[i].to.i[0];
        assert(fromBase != 0);
        assert((t->redRoot[i] == -1) || (t->redRoot[i] == inst->myid));
        assert(toBase != 0);
        assert(to > from);

#ifdef LAIK_DEBUG
            printf("LAIK %d/%d - Single reduce: "
                   "from %lu, to %lu, elemsize %d, base from/to %p/%p\n",
                   d->space->inst->myid, d->space->inst->size,
                   from, to, d->elemsize, fromBase, toBase);
#endif

        memcpy(toBase, fromBase, (to-from) * fromMap->data->elemsize);
    }

    // the single backend should never need to do send/recv actions
    assert(t->recvCount == 0);
    assert(t->sendCount == 0);
}
