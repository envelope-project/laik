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
    d->defaultFlow = LAIK_DF_None;
    d->activePartitioning = 0;
    d->nextPartitioningUser = 0;
    d->activeMappings = 0;
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

// get task group used for data
Laik_Group* laik_get_group(Laik_Data* d)
{
    return d->group;
}


static
Laik_MappingList* prepareMaps(Laik_Data* d, Laik_Partitioning* p,
                              Laik_Layout* l)
{
    int myid = laik_myid(d->group);
    assert(myid < d->group->size);
    if (myid < 0) return 0; // this task is not part of the task group

    Laik_BorderArray* ba = p->borders;
    // number of own slices = number of separate maps
    int n = ba->off[myid+1] - ba->off[myid];
    if (n == 0) return 0;

    Laik_MappingList* ml;
    ml = (Laik_MappingList*) malloc(sizeof(Laik_MappingList) +
                                    (n-1) * sizeof(Laik_Mapping));
    ml->count = n;

    for(int i = 0; i < n; i++) {
        Laik_Mapping* m = &(ml->map[i]);
        int o = ba->off[myid] + i;
        Laik_Slice* s = &(ba->tslice[o].s);

        uint64_t count = 1;
        switch(p->space->dims) {
        case 3:
            count *= s->to.i[2] - s->from.i[2];
            // fall-through
        case 2:
            count *= s->to.i[1] - s->from.i[1];
            // fall-through
        case 1:
            count *= s->to.i[0] - s->from.i[0];
            break;
        }

        m->data = d;
        m->partitioning = p;
        m->sliceNo = i;
        m->count = count;
        m->baseIdx = s->from;
        m->base = 0; // allocation happens lazy in copyMaps()

        if (l) {
            // TODO: actually use the requested order, eventually convert
            m->layout = l;
        }
        else {
            // default mapping order (1,2,3)
            m->layout = laik_new_layout(LAIK_LT_Default);
        }

        if (laik_logshown(1)) {
            char s[100];
            laik_getIndexStr(s, p->space->dims, &(m->baseIdx), false);
            laik_log(1, "prepare map for '%s'/%d: from %s, count %d, elemsize %d\n",
                     d->name, i, s, m->count, d->elemsize);
        }
    }

    return ml;
}

static
void freeMaps(Laik_MappingList* ml)
{
    if (ml == 0) return;

    for(int i = 0; i < ml->count; i++) {
        Laik_Mapping* m = &(ml->map[i]);
        assert(m != 0);

        Laik_Data* d = m->data;

        if (m->base) {
            laik_log(1, "free map for '%s'/%d (count %d, base %p)\n",
                     d->name, m->sliceNo, m->count, m->base);

            // TODO: different policies
            if ((!d->allocator) || (!d->allocator->free))
                free(m->base);
            else
                (d->allocator->free)(d, m->base);

            m->base = 0;
        }
        else
            laik_log(1, "free map for '%s'/%d (count %d): not needed\n",
                     d->name, m->sliceNo, m->count);
    }

    free(ml);
}

void laik_allocateMap(Laik_Mapping* m)
{
    if (m->base) return;
    if (m->count == 0) return;
    Laik_Data* d = m->data;

    // TODO: different policies
    if ((!d->allocator) || (!d->allocator->malloc))
        m->base = malloc(m->count * d->elemsize);
    else
        m->base = (d->allocator->malloc)(d, m->count * d->elemsize);

    laik_log(1, "allocated memory for '%s'/%d: %d x %d at %p\n",
             d->name, m->sliceNo, m->count, d->elemsize, m->base);
}

static
void copyMaps(Laik_Transition* t,
              Laik_MappingList* toList, Laik_MappingList* fromList)
{
    assert(t->localCount > 0);
    for(int i = 0; i < t->localCount; i++) {
        struct localTOp* op = &(t->local[i]);
        assert(op->fromSliceNo < fromList->count);
        Laik_Mapping* fromMap = &(fromList->map[op->fromSliceNo]);
        assert(op->toSliceNo < toList->count);
        Laik_Mapping* toMap = &(toList->map[op->toSliceNo]);

        assert(toMap->data == fromMap->data);
        if (toMap->count == 0) {
            // no elements to copy to
            continue;
        }
        if (fromMap->base == 0) {
            // nothing to copy from
            assert(fromMap->count == 0);
            continue;
        }

        // calculate overlapping range between fromMap and toMap
        assert(fromMap->data->space->dims == 1); // only for 1d now
        Laik_Data* d = toMap->data;
        Laik_Slice* s = &(op->slc);
        int count = s->to.i[0] - s->from.i[0];
        assert(count > 0);
        uint64_t fromStart = s->from.i[0] - fromMap->baseIdx.i[0];
        uint64_t toStart   = s->from.i[0] - toMap->baseIdx.i[0];

        // we can use old mapping if we would copy complete old to new mapping
        if ((fromStart == 0) && (fromMap->count == count) &&
            (toStart == 0) && (toMap->count == count)) {
            // should not be allocated yet
            assert(toMap->base == 0);
            toMap->base = fromMap->base;
            fromMap->base = 0; // nothing to free

            laik_log(1, "copy map for '%s': %d x %d "
                        "from global [%lu local [%lu/%d ==> [%lu/%d, using old map\n",
                     d->name, count, d->elemsize, s->from.i[0],
                     fromStart, op->fromSliceNo, toStart, op->toSliceNo);
            continue;
        }

        if (toMap->base == 0) {
            // need to allocate memory
            laik_allocateMap(toMap);
        }
        char*    fromPtr   = fromMap->base + fromStart * d->elemsize;
        char*    toPtr     = toMap->base   + toStart * d->elemsize;

        laik_log(1, "copy map for '%s': %d x %d "
                    "from global [%lu local %p + [%lu/%d ==> %p + [%lu/%d\n",
                 d->name, count, d->elemsize, s->from.i[0],
                 fromMap->base, fromStart, op->fromSliceNo,
                 toMap->base, toStart, op->toSliceNo);

        memcpy(toPtr, fromPtr, count * d->elemsize);
    }
}

static
void initMaps(Laik_Transition* t,
              Laik_MappingList* toList, Laik_MappingList* fromList)
{
    assert(t->initCount > 0);
    for(int i = 0; i < t->initCount; i++) {
        struct initTOp* op = &(t->init[i]);
        assert(op->sliceNo < toList->count);
        Laik_Mapping* toMap = &(toList->map[op->sliceNo]);

        if (toMap->count == 0) {
            // no elements to initialize
            continue;
        }

        int dims = toMap->data->space->dims;
        if (toMap->base == 0) {
            // if we find a fitting mapping in fromList, use that
            if (fromList) {
                for(int sNo = 0; sNo < fromList->count; sNo++) {
                    Laik_Mapping* fromMap = &(fromList->map[sNo]);
                    if (fromMap->base == 0) continue;
                    if (!laik_index_isEqual(dims,
                                            &(toMap->baseIdx),
                                            &(fromMap->baseIdx))) continue;
                    if (toMap->count != fromMap->count) continue;

                    toMap->base = fromMap->base;
                    fromMap->base = 0; // taken over

                    laik_log(1, "during init for '%s'/%d: used old %d at %p\n",
                             toMap->data->name, op->sliceNo, sNo, toMap->base);
                    break;
                }
            }
            if (toMap->base == 0) {
                // nothing found, allocate memory
                laik_allocateMap(toMap);
            }
        }

        assert(dims == 1); // only for 1d now
        Laik_Data* d = toMap->data;
        Laik_Slice* s = &(op->slc);
        int count = s->to.i[0] - s->from.i[0];

        if (d->type == laik_Double) {
            double v;
            double* dbase = (double*) toMap->base;

            switch(t->init[i].redOp) {
            case LAIK_RO_Sum: v = 0.0; break;
            default:
                assert(0);
            }
            for(int j = s->from.i[0]; j < s->to.i[0]; j++)
                dbase[j] = v;
        }
        else if (d->type == laik_Float) {
            float v;
            float* dbase = (float*) toMap->base;

            switch(t->init[i].redOp) {
            case LAIK_RO_Sum: v = 0.0; break;
            default:
                assert(0);
            }
            for(int j = s->from.i[0]; j < s->to.i[0]; j++)
                dbase[j] = v;
        }
        else assert(0);

        laik_log(1, "init map for '%s'/%d: %d x at [%lu\n",
                 d->name, op->sliceNo, count, s->from.i[0]);
    }
}


// set and enforce partitioning
void laik_set_partitioning(Laik_Data* d, Laik_Partitioning* p)
{
    // calculate borders (TODO: may need global communication)
    laik_update_partitioning(p);

    // TODO: convert to realloc (with taking over layout)
    Laik_MappingList* fromList = d->activeMappings;
    Laik_MappingList* toList = prepareMaps(d, p, 0);

    // calculate actions to be done for switching
    Laik_BorderArray *fromBA = 0, *toBA = 0;
    Laik_DataFlow fromFlow = LAIK_DF_None, toFlow = LAIK_DF_None;
    Laik_Partitioning* fromP = d->activePartitioning;
    if (fromP) {
        // active partitioning must have borders set
        assert(fromP->bordersValid);
        fromBA = fromP->borders;
        fromFlow = fromP->flow;
    }
    if (p) {
        // new partitioning needs to be defined over same LAIK task group
        assert(p->group == d->group);
        assert(p->bordersValid);
        toBA = p->borders;
        toFlow = p->flow;
    }
    Laik_Transition* t;
    t = laik_calc_transition(d->group, d->space,
                             fromBA, fromFlow, toBA, toFlow);

    if (laik_logshown(1)) {
        char s[1000];
        int len = laik_getTransitionStr(s, t);
        if (len == 0)
            laik_log(1, "transition %s => %s: (nothing)\n",
                     fromP ? fromP->name : "(none)", p ? p->name : "(none)");
        else
            laik_log(1, "transition %s => %s:\n%s",
                     fromP ? fromP->name : "(none)", p ? p->name : "(none)",
                     s);
    }

    // let backend do send/recv/reduce actions
    // TODO: use async interface
    assert(p->space->inst->backend->execTransition);
    (p->space->inst->backend->execTransition)(d, t, fromList, toList);

    // local copy action (may use old mappings)
    if (t->localCount > 0)
        copyMaps(t, toList, fromList);

    // local init action (may use old mappings)
    if (t->initCount > 0)
        initMaps(t, toList, fromList);

    // free old mapping/partitioning
    if (fromList)
        freeMaps(fromList);

    if (d->activePartitioning) {
        laik_removePartitioningUser(d->activePartitioning, d);
        laik_free_partitioning(d->activePartitioning);
    }

    // set new mapping/partitioning active
    d->activePartitioning = p;
    if (p)
        laik_addPartitioningUser(p, d);
    d->activeMappings = toList;
}

// get slice number <n> in own partition
Laik_Slice* laik_data_slice(Laik_Data* d, int n)
{
    if (d->activePartitioning == 0) return 0;
    return laik_my_slice(d->activePartitioning, n);
}

Laik_Partitioning* laik_set_new_partitioning(Laik_Data* d,
                                             Laik_PartitionType pt,
                                             Laik_DataFlow flow)
{
    Laik_Partitioning* p;
    p = laik_new_base_partitioning(d->group, d->space, pt, flow);
    laik_set_partitioning(d, p);

    return p;
}


void laik_fill_double(Laik_Data* d, double v)
{
    double* base;
    uint64_t count, i;

    laik_map_def1(d, (void**) &base, &count);
    // TODO: partitioning can have multiple slices
    assert(laik_my_slicecount(d->activePartitioning) == 1);
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
Laik_Mapping* laik_map(Laik_Data* d, int n, Laik_Layout* layout)
{
    Laik_Partitioning* p;

    if (!d->activePartitioning)
        laik_set_new_partitioning(d,
                                  d->defaultPartitionType,
                                  d->defaultFlow);

    p = d->activePartitioning;

    // lazy allocation
    if (!d->activeMappings) {
        d->activeMappings = prepareMaps(d, p, layout);
        if (d->activeMappings == 0)
            return 0;
    }

    if (n >= d->activeMappings->count)
        return 0;

    Laik_Mapping* m = &(d->activeMappings->map[n]);
    // ensure the mapping is backed by real memory
    laik_allocateMap(m);

    return m;
}

// similar to laik_map, but force a default mapping
Laik_Mapping* laik_map_def(Laik_Data* d, int n, void** base, uint64_t* count)
{
    Laik_Layout* l = laik_new_layout(LAIK_LT_Default);
    Laik_Mapping* m = laik_map(d, n, l);

    if (base) *base = m ? m->base : 0;
    if (count) *count = m ? m->count : 0;
    return m;
}


// similar to laik_map, but force a default mapping with only 1 slice
Laik_Mapping* laik_map_def1(Laik_Data* d, void** base, uint64_t* count)
{
    Laik_Layout* l = laik_new_layout(LAIK_LT_Default1Slice);
    Laik_Mapping* m = laik_map(d, 0, l);
    int n = laik_my_slicecount(d->activePartitioning);
    if (n > 1)
        laik_log(LAIK_LL_Panic, "Request for single continuous mapping, "
                                "but partition with %d slices!\n", n);

    if (base) *base = m ? m->base : 0;
    if (count) *count = m ? m->count : 0;
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

