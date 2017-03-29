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

Laik_Data* laik_alloc(Laik_Group* g, Laik_Space* s, int elemsize)
{
    assert(g->inst == s->inst);

    Laik_Data* d = (Laik_Data*) malloc(sizeof(Laik_Data));

    d->id = data_id++;
    d->name = strdup("data-0     ");
    sprintf(d->name, "data-%d", d->id);

    d->group = g;
    d->space = s;
    d->elemsize = elemsize;

    d->backend_data = 0;
    d->defaultPartitionType = LAIK_PT_Stripe;
    d->defaultPermission = LAIK_AP_ReadWrite;
    d->activePartitioning = 0;
    d->activeMapping = 0;
    d->allocator = 0; // default: malloc/free

    return d;
}

Laik_Data* laik_alloc_1d(Laik_Group* g, int elemsize, uint64_t s1)
{
    Laik_Space* space = laik_new_space_1d(g->inst, s1);
    Laik_Data* d = laik_alloc(g, space, elemsize);

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
    Laik_Data* d = laik_alloc(g, space, elemsize);

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

// get space used for data
Laik_Space* laik_get_space(Laik_Data* d)
{
    return d->space;
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
    if (count == 0)
        m->base = 0;
    else {
        // TODO: different policies
        if ((!d->allocator) || (!d->allocator->malloc))
            m->base = malloc(count * d->elemsize);
        else
            m->base = (d->allocator->malloc)(d, count * d->elemsize);
    }

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

    if (m && m->base) {
        // TODO: different policies
        if ((!d->allocator) || (!d->allocator->free))
            free(m->base);
        else
            (d->allocator->free)(d, m->base);
    }

    free(m);
}

static
void copyMap(Laik_Transition* t, Laik_Mapping* toMap, Laik_Mapping* fromMap)
{
    assert(t->localCount > 0);
    assert(toMap->data == fromMap->data);
    if (toMap->count == 0) {
        // no elements to copy to
        return;
    }
    if (fromMap->base == 0) {
        // nothing to copy from
        assert(fromMap->count == 0);
        return;
    }

    // calculate overlapping range between fromMap and toMap
    assert(fromMap->data->space->dims == 1); // only for 1d now
    Laik_Data* d = toMap->data;
    for(int i = 0; i < t->localCount; i++) {
        Laik_Slice* s = &(t->local[i]);
        int count = s->to.i[0] - s->from.i[0];
        uint64_t fromStart = s->from.i[0] - fromMap->baseIdx.i[0];
        uint64_t toStart   = s->from.i[0] - toMap->baseIdx.i[0];
        char*    fromPtr   = fromMap->base + fromStart * d->elemsize;
        char*    toPtr     = toMap->base   + toStart * d->elemsize;

#ifdef LAIK_DEBUG
        printf("LAIK %d/%d - copy map for '%s': "
               "%d x %d from [%lu global [%lu to [%lu, %p => %p\n",
               d->space->inst->myid, d->space->inst->size, d->name,
               count, d->elemsize, fromStart, s->from.i[0], toStart,
               fromPtr, toPtr);
#endif

        if (count>0)
            memcpy(toPtr, fromPtr, count * d->elemsize);
    }
}

static
void initMap(Laik_Transition* t, Laik_Mapping* toMap)
{
    assert(t->initCount > 0);
    if (toMap->count == 0) {
        // no elements to initialize
        return;
    }

    assert(toMap->data->space->dims == 1); // only for 1d now
    Laik_Data* d = toMap->data;
    for(int i = 0; i < t->initCount; i++) {
        Laik_Slice* s = &(t->init[i]);
        int count = s->to.i[0] - s->from.i[0];
        double v;
        double* dbase = (double*) toMap->base;

        assert(d->elemsize == 8); // FIXME: we assume "double"
        switch(t->initRedOp[i]) {
        case LAIK_AP_Sum: v = 0.0; break;
        case LAIK_AP_Prod: v = 1.0; break;
        case LAIK_AP_Min: v = 9e99; break; // should be largest double val
        case LAIK_AP_Max: v = -9e99; break; // should be smallest double val
        default:
            assert(0);
        }

#ifdef LAIK_DEBUG
        printf("LAIK %d/%d - init map for '%s': double %f => %d x at [%lu, %p\n",
               d->space->inst->myid, d->space->inst->size, d->name,
               v, count, s->from.i[0], dbase);
#endif

        for(int j = s->from.i[0]; j < s->to.i[0]; j++)
            dbase[j] = v;
    }
}


// set and enforce partitioning
void laik_set_partitioning(Laik_Data* d, Laik_Partitioning* p)
{
    // calculate borders (TODO: may need global communication)
    laik_update_partitioning(p);

    // TODO: convert to realloc (with taking over layout)
    Laik_Mapping* toMap = allocMap(d, p, 0);

    if (d->activePartitioning) {
        // calculate elements which need to be sent/received by this task
        Laik_Transition* t = laik_calc_transitionP(d->activePartitioning, p);

        // TODO: use async interface
        assert(p->space->inst->backend->execTransition);
        (p->space->inst->backend->execTransition)(d, t, toMap);

        if (t->localCount > 0)
            copyMap(t, toMap, d->activeMapping);

        if (t->initCount > 0)
            initMap(t, toMap);

        freeMap(d->activeMapping);
    }

    if (d->activePartitioning) {
        laik_free_partitioning(d->activePartitioning);
    }

    // set active
    d->activePartitioning = p;
    d->activeMapping = toMap;
}

Laik_Partitioning* laik_set_new_partitioning(Laik_Data* d,
                                             Laik_PartitionType pt,
                                             Laik_AccessPermission ap)
{
    Laik_Partitioning* p = laik_new_base_partitioning(d->space, pt, ap);
    laik_set_partitioning(d, p);

    return p;
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
        laik_set_new_partitioning(d,
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



// Allocator interface

// returns an allocator with default policy LAIK_MP_NewAllocOnRepartition
Laik_Allocator* laik_new_allocator()
{
    Laik_Allocator* a = (Laik_Allocator*) malloc(sizeof(Laik_Allocator));

    a->policy = LAIK_MP_NewAllocOnRepartition;
    a->malloc = 0;  // use malloc
    a->free = 0;    // use free
    a->realloc = 0; // use malloc/free for reallocation
    a->unmap = 0;   // no notification

    return a;
}

void laik_set_allocator(Laik_Data* d, Laik_Allocator* a)
{
    // TODO: decrement reference count for existing allocator

    d->allocator = a;
}

Laik_Allocator* laik_get_allocator(Laik_Data* d)
{
    return d->allocator;
}

