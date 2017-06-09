/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

Laik_Type *laik_Char;
Laik_Type *laik_Int32;
Laik_Type *laik_Int64;
Laik_Type *laik_Float;
Laik_Type *laik_Double;

static int type_id = 0;

Laik_Type* laik_new_type(char* name, Laik_TypeKind kind, int size)
{
    Laik_Type* t = (Laik_Type*) malloc(sizeof(Laik_Type));

    t->id = type_id++;
    if (name)
        t->name = name;
    else {
        t->name = strdup("type-0     ");
        sprintf(t->name, "type-%d", t->id);
    }

    t->kind = kind;
    t->size = size;
    t->init = 0;    // reductions not supported
    t->reduce = 0;
    t->getLength = 0; // not needed for POD type
    t->convert = 0;

    return t;
}

void laik_data_init()
{
    if (type_id > 0) return;

    laik_Char   = laik_new_type("char",  LAIK_TK_POD, 1);
    laik_Int32  = laik_new_type("int32", LAIK_TK_POD, 4);
    laik_Int64  = laik_new_type("int64", LAIK_TK_POD, 8);
    laik_Float  = laik_new_type("float", LAIK_TK_POD, 4);
    laik_Double = laik_new_type("double", LAIK_TK_POD, 8);
}



static int data_id = 0;

Laik_Data* laik_alloc(Laik_Group* g, Laik_Space* s, Laik_Type* t)
{
    assert(g->inst == s->inst);

    Laik_Data* d = (Laik_Data*) malloc(sizeof(Laik_Data));

    d->id = data_id++;
    d->name = strdup("data-0     ");
    sprintf(d->name, "data-%d", d->id);

    d->group = g;
    d->space = s;
    d->type = t;
    assert(t && (t->size > 0));
    d->elemsize = t->size; // TODO: other than POD types

    d->backend_data = 0;
    d->defaultPartitionType = LAIK_PT_Block;
    d->defaultAccess = LAIK_AB_ReadWrite;
    d->activePartitioning = 0;
    d->activeMapping = 0;
    d->allocator = 0; // default: malloc/free

    return d;
}

Laik_Data* laik_alloc_1d(Laik_Group* g, Laik_Type* t, uint64_t s1)
{
    Laik_Space* space = laik_new_space_1d(g->inst, s1);
    Laik_Data* d = laik_alloc(g, space, t);

    laik_log(1, "new 1d data '%s': elemsize %d, space '%s'\n",
             d->name, d->elemsize, space->name);

    return d;
}

Laik_Data* laik_alloc_2d(Laik_Group* g, Laik_Type* t, uint64_t s1, uint64_t s2)
{
    Laik_Space* space = laik_new_space_2d(g->inst, s1, s2);
    Laik_Data* d = laik_alloc(g, space, t);

    laik_log(1, "new 2d data '%s': elemsize %d, space '%s'\n",
             d->name, d->elemsize, space->name);

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
    m->count = count;
    m->baseIdx = p->borders[t].from;

    if (l) {
        // TODO: actually use the requested order, eventually convert
        m->layout = l;
    }
    else {
        // TODO: we promise 1 slice, default mapping order (1,2,3)
        m->layout = laik_new_layout(LAIK_LT_Default1Slice);
    }

    if (count == 0)
        m->base = 0;
    else {
        // TODO: different policies
        if ((!d->allocator) || (!d->allocator->malloc))
            m->base = malloc(count * d->elemsize);
        else
            m->base = (d->allocator->malloc)(d, count * d->elemsize);
    }

    if (laik_logshown(1)) {
        char s[100];
        laik_getIndexStr(s, p->space->dims, &(m->baseIdx), false);
        laik_log(1, "new map for '%s': [%s+%d, elemsize %d, base %p\n",
                 d->name, s, m->count, d->elemsize, m->base);
    }

    return m;
}

static
void freeMap(Laik_Mapping* m)
{
    Laik_Data* d = m->data;

    laik_log(1, "free map for '%s' (count %d, base %p)\n",
             d->name, m->count, m->base);

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

        laik_log(1, "copy map for '%s': "
                    "%d x %d from [%lu global [%lu to [%lu, %p => %p\n",
                 d->name, count, d->elemsize, fromStart, s->from.i[0], toStart,
                fromPtr, toPtr);

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

        if (d->type == laik_Double) {
            double v;
            double* dbase = (double*) toMap->base;

            switch(t->initRedOp[i]) {
            case LAIK_AB_Sum: v = 0.0; break;
            case LAIK_AB_Prod: v = 1.0; break;
            case LAIK_AB_Min: v = 9e99; break; // should be largest double val
            case LAIK_AB_Max: v = -9e99; break; // should be smallest double val
            default:
                assert(0);
            }
            for(int j = s->from.i[0]; j < s->to.i[0]; j++)
                dbase[j] = v;
        }
        else if (d->type == laik_Float) {
            float v;
            float* dbase = (float*) toMap->base;

            switch(t->initRedOp[i]) {
            case LAIK_AB_Sum: v = 0.0; break;
            case LAIK_AB_Prod: v = 1.0; break;
            case LAIK_AB_Min: v = 9e99; break; // should be largest double val
            case LAIK_AB_Max: v = -9e99; break; // should be smallest double val
            default:
                assert(0);
            }
            for(int j = s->from.i[0]; j < s->to.i[0]; j++)
                dbase[j] = v;
        }
        else assert(0);

        laik_log(1, "init map for '%s': %d x at [%lu\n",
                 d->name, count, s->from.i[0]);
    }
}


// set and enforce partitioning, internal version which can also be used for 
// repartitioning
void laik_set_partitioning_internal(Laik_Data* d, Laik_Partitioning* p)
{

#if 0
    if(failing)
      p->bordersValid = false;
#endif

    // calculate borders (TODO: may need global communication)
    laik_update_partitioning_internal(p);

    laik_log(1, "After Update_partitioing_internal\n");

    // TODO: convert to realloc (with taking over layout)
    Laik_Mapping* toMap = allocMap(d, p, 0);

    // calculate actions to be done for switching
    Laik_Transition* t = laik_calc_transitionP(d->activePartitioning, p);

    // let backend do send/recv/reduce actions
    // TODO: use async interface
    assert(p->space->inst->backend->execTransition);
    (p->space->inst->backend->execTransition)(d, t, toMap);

    // local copy action
    if (t->localCount > 0)
        copyMap(t, toMap, d->activeMapping);

    // local init action
    if (t->initCount > 0)
        initMap(t, toMap);

    // free old mapping/partitioning
    if (d->activeMapping)
        freeMap(d->activeMapping);
    if (d->activePartitioning)
        laik_free_partitioning(d->activePartitioning);

    // set new mapping/partitioning active
    d->activePartitioning = p;
    d->activeMapping = toMap;
}

// set and enforce partitioning
void laik_set_partitioning(Laik_Data* d, Laik_Partitioning* p)
{
  laik_set_partitioning_internal(d, p);
}

Laik_Partitioning* laik_set_new_partitioning(Laik_Data* d,
                                             Laik_PartitionType pt,
                                             Laik_AccessBehavior ap)
{
    Laik_Partitioning* p = laik_new_base_partitioning(d->space, pt, ap);
    laik_set_partitioning(d, p);

    return p;
}


void laik_fill_double(Laik_Data* d, double v)
{
    double* base;
    uint64_t count, i;

    laik_map_def1(d, (void**) &base, &count);
    for (i = 0; i < count; i++)
        base[i] = v;
}

// allocate new layout object with a layout hint, to use in laik_map
Laik_Layout* laik_new_layout(Laik_LayoutType t)
{
    Laik_Layout* l = (Laik_Layout*) malloc(sizeof(Laik_Layout));

    l->type = t;
    l->isFixed = false;
    l->dims = 0; // invalid

    return l;
}

// return the layout used by a mapping
Laik_Layout* laik_map_layout(Laik_Mapping* m)
{
    assert(m);
    return m->layout;
}

// return the layout type of a specific layout
Laik_LayoutType laik_layout_type(Laik_Layout* l)
{
    assert(l);
    return l->type;
}

// return the layout type used in a mapping
Laik_LayoutType laik_map_layout_type(Laik_Mapping* m)
{
    assert(m && m->layout);
    return m->layout->type;
}


// make own partition available for direct access in local memory
Laik_Mapping* laik_map(Laik_Data* d, Laik_Layout* layout)
{
    Laik_Partitioning* p;
    Laik_Mapping* m;

    if (!d->activePartitioning)
        laik_set_new_partitioning(d,
                                  d->defaultPartitionType,
                                  d->defaultAccess);

    p = d->activePartitioning;

    // lazy allocation
    if (!d->activeMapping)
        d->activeMapping = allocMap(d, p, layout);

    m = d->activeMapping;
    return m;
}

// similar to laik_map, but force a default mapping with only 1 slice
Laik_Mapping* laik_map_def1(Laik_Data* d, void** base, uint64_t* count)
{
    Laik_Layout* l = laik_new_layout(LAIK_LT_Default1Slice);
    Laik_Mapping* m = laik_map(d, l);
    assert(m->layout->type == LAIK_LT_Default1Slice);

    if (base) *base = m->base;
    if (count) *count = m->count;
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

