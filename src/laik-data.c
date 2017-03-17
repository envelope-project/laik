/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int data_id = 0;

Laik_Data* laik_alloc(Laik_Group* g, Laik_Space* s)
{
    assert(g->inst == s->inst);

    Laik_Data* d = (Laik_Data*) malloc(sizeof(Laik_Data));

    d->id = data_id++;
    d->name = strdup("data-0     ");
    sprintf(d->name, "data-%d", d->id);

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
    Laik_Space* space = laik_new_space_1d(g->inst, s1);
    Laik_Data* d = laik_alloc(g, space);
    d->elemsize = elemsize;

#ifdef LAIK_DEBUG
    printf("LAIK %d/%d - new 1d data '%s': elemsize %d, space '%s'\n",
           space->inst->myid, space->inst->size,
           d->name, d->elemsize, space->name);
#endif

    return d;
}

Laik_Data* laik_alloc_2d(Laik_Group* g, int elemsize, uint64_t s1, uint64_t s2)
{
    Laik_Space* space = laik_new_space_2d(g->inst, s1, s2);
    Laik_Data* d = laik_alloc(g, space);
    d->elemsize = elemsize;

#ifdef LAIK_DEBUG
    printf("LAIK %d/%d - new 2d data '%s': elemsize %d, space '%s'\n",
           space->inst->myid, space->inst->size,
           d->name, d->elemsize, space->name);
#endif

    return d;
}

// set a data name, for debug output
void laik_set_data_name(Laik_Data* d, char* n)
{
    d->name = n;
}

static
Laik_Mapping* allocMap(Laik_Data* d, Laik_Partitioning* p, Laik_Layout* l)
{
    Laik_Mapping* m;
    m = (Laik_Mapping*) malloc(sizeof(Laik_Mapping));

    int t = laik_myid(d->group);
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
    m->baseIdx = p->borders[t].from;
    m->base = (count == 0) ? 0 : malloc(count * d->elemsize);

#ifdef LAIK_DEBUG
    char s[100];
    laik_getIndexStr(s, p->space->dims, &(m->baseIdx), false);
    printf("LAIK %d/%d - new map for '%s': [%s+%d, elemsize %d, base %p\n",
           d->space->inst->myid, d->space->inst->size,
           d->name, s, m->count, d->elemsize, m->base);
#endif

    return m;
}

static
void freeMap(Laik_Mapping* m)
{
#ifdef LAIK_DEBUG
    Laik_Data* d = m->data;
    printf("LAIK %d/%d - free map for '%s' (count %d, base %p)\n",
           d->space->inst->myid, d->space->inst->size,
           d->name, m->count, m->base);
#endif

    if (m && m->base)
        free(m->base);
    free(m);
}

static
void copyMap(Laik_Mapping* to, Laik_Mapping* from)
{
    assert(to->data == from->data);
    if (to->count == 0) {
        // no elements to copy to
        return;
    }
    if (from->base == 0) {
        // nothing to copy from
        assert(from->count == 0);
        return;
    }

#ifdef LAIK_DEBUG
    Laik_Data* d = to->data;
    printf("LAIK %d/%d - copy map for '%s': count %d x %d, %p => %p\n",
           d->space->inst->myid, d->space->inst->size,
           d->name, to->count, d->elemsize, from->base, to->base);
#endif

    assert(to->count <= from->count);
    memcpy(to->base, from->base, to->count * to->data->elemsize);

}

void laik_set_partitioning(Laik_Data* d,
                           Laik_PartitionType pt, Laik_AccessPermission ap)
{
    Laik_Partitioning* p;
    p = laik_new_base_partitioning(d->space, pt, ap);

    // calculate borders (TODO: may need global communication)
    laik_update_partitioning(p);

    // TODO: convert to realloc (with taking over layout)
    Laik_Mapping* toMap = allocMap(d, p, 0);

    if (d->activePartitioning) {
        // calculate elements which need to be sent/received by this task
        Laik_Transition* t = laik_calc_transitionP(d->activePartitioning, p);


        // TODO: use async interface
        if (p->space->inst->backend->execTransition)
            (p->space->inst->backend->execTransition)(d, t, toMap);

        if (laik_is_reduction(d->activePartitioning->permission)) {
            ; // reduction done by communication backend
        }
        else
            copyMap(toMap, d->activeMapping);

        freeMap(d->activeMapping);
    }

    if (d->activePartitioning)
        laik_free_partitioning(d->activePartitioning);

    // set active
    d->activePartitioning = p;
    d->activeMapping = toMap;
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

    // lazy allocation
    if (!d->activeMapping)
        d->activeMapping = allocMap(d, p, l);

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

