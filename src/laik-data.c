/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"

#include <assert.h>
#include <stdlib.h>

Laik_Data* laik_alloc(Laik_Group* g, Laik_Space* s)
{
    assert(g->inst == s->inst);

    Laik_Data* d = (Laik_Data*) malloc(sizeof(Laik_Data));
    d->group = g;
    d->space = s;
    d->elemsize = 0; // invalid

    d->backend_data = 0;
    d->defaultPartitionType = LAIK_PT_Stripe;
    d->defaultPermission = LAIK_AP_ReadWrite;
    d->activePartitioning = 0;
    d->activeMapping = 0;

    return d;
}

Laik_Data* laik_alloc_1d(Laik_Group* g, int elemsize, uint64_t s1)
{
    Laik_Space* s = laik_new_space_1d(g->inst, s1);
    Laik_Data* d = laik_alloc(g, s);
    d->elemsize = elemsize;

    return d;
}

Laik_Data* laik_alloc_2d(Laik_Group* g, int elemsize, uint64_t s1, uint64_t s2)
{
    Laik_Space* s = laik_new_space_2d(g->inst, s1, s2);
    Laik_Data* d = laik_alloc(g, s);
    d->elemsize = elemsize;

    return d;
}

void laik_set_partitioning(Laik_Data* d,
                           Laik_PartitionType pt, Laik_AccessPermission ap)
{
    Laik_Partitioning* p;
    p = laik_new_base_partitioning(d->space, pt, ap);

    // calculate borders (TODO: may need global communication)
    laik_update_partitioning(p);

    if (d->activePartitioning) {
        // calculate elements which need to be sent/received by this task
        Laik_Transition* t = laik_calc_transitionP(d->activePartitioning, p);

        // TODO: use async interface
        if (p->space->inst->backend->execTransition)
            (p->space->inst->backend->execTransition)(t);
    }

    if (d->activePartitioning)
        laik_free_partitioning(d->activePartitioning);

    // set active
    d->activePartitioning = p;
}


void laik_fill_double(Laik_Data* d, double v)
{
    double* base;
    uint64_t count, i;

    laik_map(d, 0, (void**) &base, &count);
    for (i = 0; i < count; i++)
        base[i] = v;
}

Laik_Mapping* laik_map(Laik_Data* d, Laik_Layout* l,
                       void** base, uint64_t* count)
{
    Laik_Partitioning* p;
    Laik_Mapping* m;

    if (!d->activePartitioning)
        laik_set_partitioning(d,
                              d->defaultPartitionType,
                              d->defaultPermission);

    p = d->activePartitioning;

    // TODO: re-map on changes

    if (!d->activeMapping) {
        m = (Laik_Mapping*) malloc(sizeof(Laik_Mapping));

        int t = laik_myid(d->group->inst);
        uint64_t count = 1;
        switch(p->space->dims) {
        case 3:
            count *= p->borders[t].to.i[2] - p->borders[t].from.i[2];
            // fall-through
        case 2:
            count *= p->borders[t].to.i[1] - p->borders[t].from.i[1];
            // fall-through
        case 1:
            count *= p->borders[t].to.i[0] - p->borders[t].from.i[0];
            break;
        }
        m->data = d;
        m->partitioning = p;
        m->task = t;
        m->layout = l; // TODO
        m->count = count;
        m->base = malloc(count * d->elemsize);

        d->activeMapping = m;
    }

    m = d->activeMapping;
    *base = m->base;
    *count = m->count;

    return m;
}

void laik_free(Laik_Data* d)
{
    // TODO: free space, partitionings

    free(d);
}

