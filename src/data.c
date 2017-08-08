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
    d->activePartitioning = 0;
    d->activeFlow = LAIK_DF_None;
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
void laik_data_set_name(Laik_Data* d, char* n)
{
    d->name = n;
}

// get space used for data
Laik_Space* laik_get_dspace(Laik_Data* d)
{
    return d->space;
}

// get task group used for data
Laik_Group* laik_get_dgroup(Laik_Data* d)
{
    return d->group;
}

// get active partitioning of data container
Laik_Partitioning* laik_get_active(Laik_Data* d)
{
    return d->activePartitioning;
}


static
Laik_MappingList* prepareMaps(Laik_Data* d, Laik_BorderArray* ba,
                              Laik_Layout* l)
{
    if (!ba) return 0; // without borders, no mappings

    assert(ba->group == d->group);
    int myid = laik_myid(ba->group);
    if (myid == -1) return 0; // this task is not part of the task group
    assert(myid < ba->group->size);

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
        switch(d->space->dims) {
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
            laik_getIndexStr(s, d->space->dims, &(m->baseIdx));
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

static
void doTransition(Laik_Data* d, Laik_Transition* t,
                  Laik_MappingList* fromList, Laik_MappingList* toList)
{
    if (t) {
        Laik_Instance* inst = d->space->inst;
        // let backend do send/recv/reduce actions
        if (inst->do_profiling)
            inst->timer_backend = laik_wtime();

        assert(d->space->inst->backend->execTransition);
        (d->space->inst->backend->execTransition)(d, t, fromList, toList);

        if (inst->do_profiling)
            inst->time_backend += laik_wtime() - inst->timer_backend;

        // local copy action (may use old mappings)
        if (t->localCount > 0)
            copyMaps(t, toList, fromList);

        // local init action (may use old mappings)
        if (t->initCount > 0)
            initMaps(t, toList, fromList);
    }
    // free old mapping/partitioning
    if (fromList)
        freeMaps(fromList);
}

// switch to new borders (new flow is derived from previous flow)
void laik_switchto_borders(Laik_Data* d, Laik_BorderArray* toBA)
{
    // calculate actions to be done for switching
    Laik_BorderArray *fromBA = 0;
    Laik_Partitioning* part = d->activePartitioning;
    if (part) {
        // active partitioning must have borders set
        assert(part->bordersValid);
        fromBA = part->borders;
    }

    Laik_DataFlow toFlow;
    if (laik_do_copyout(d->activeFlow) || laik_is_reduction(d->activeFlow))
        toFlow = LAIK_DF_CopyIn | LAIK_DF_CopyOut;
    else
        toFlow = LAIK_DF_None;

    Laik_MappingList* toList = prepareMaps(d, toBA, 0);
    Laik_Transition* t = laik_calc_transition(d->group, d->space,
                                              fromBA, d->activeFlow,
                                              toBA, toFlow);

    if (laik_logshown(1)) {
        int o;
        char s1[1000];
        o = sprintf(s1, "transition (data '%s', partition '%s'):\n",
                    d->name, part ? part->name : "(none)");
        o += sprintf(s1+o, "  from ");
        o += laik_getDataFlowStr(s1+o, d->activeFlow);
        o += sprintf(s1+o, ", ");
        o += laik_getBorderArrayStr(s1+o, fromBA);
        o += sprintf(s1+o, "\n  to ");
        o += laik_getDataFlowStr(s1+o, toFlow);
        o += sprintf(s1+o, ", ");
        o += laik_getBorderArrayStr(s1+o, toBA);
        o += sprintf(s1+o, "\n  actions:");

        char s2[1000];
        int len = laik_getTransitionStr(s2, t);

        if (len == 0)
            laik_log(1, "%s (nothing)\n", s1);
        else
            laik_log(1, "%s\n%s", s1, s2);
    }

    doTransition(d, t, d->activeMappings, toList);

    // set new mapping/partitioning active
    d->activeFlow = toFlow;
    d->activeMappings = toList;
}

// switch from active to another partitioning
void laik_switchto(Laik_Data* d,
                   Laik_Partitioning* toP, Laik_DataFlow toFlow)
{
    if (d->space->inst->do_profiling)
        d->space->inst->timer_total = laik_wtime();

    // calculate borders with configured partitioner if borders not set
    if (toP && (!toP->bordersValid))
        laik_calc_partitioning(toP);

    // calculate actions to be done for switching
    Laik_BorderArray *fromBA = 0, *toBA = 0;
    Laik_Partitioning* fromP = d->activePartitioning;
    if (fromP) {
        // active partitioning must have borders set
        assert(fromP->bordersValid);
        fromBA = fromP->borders;
    }
    if (toP) {
        // new partitioning needs to be defined over same LAIK task group
        assert(toP->group == d->group);
        assert(toP->bordersValid);
        toBA = toP->borders;
    }

    Laik_MappingList* toList = prepareMaps(d, toBA, 0);
    Laik_Transition* t = laik_calc_transition(d->group, d->space,
                                              fromBA, d->activeFlow,
                                              toBA, toFlow);

    if (laik_logshown(1)) {
        int o;
        char s1[1000];
        o = sprintf(s1, "switch partitionings for data '%s':\n"
                    "  %s/",
                    d->name, fromP ? fromP->name : "(none)");
        o += laik_getDataFlowStr(s1+o, d->activeFlow);
        o += sprintf(s1+o, " => %s/", toP ? toP->name : "(none)");
        o += laik_getDataFlowStr(s1+o, toFlow);

        char s2[1000];
        int len = laik_getTransitionStr(s2, t);

        if (len == 0)
            laik_log(1, "%s: (nothing)\n", s1);
        else
            laik_log(1, "%s:\n%s", s1, s2);
    }

    doTransition(d, t, d->activeMappings, toList);

    if (d->activePartitioning) {
        laik_removeDataFromPartitioning(d->activePartitioning, d);
        laik_free_partitioning(d->activePartitioning);
    }

    // set new mapping/partitioning active
    d->activePartitioning = toP;
    d->activeFlow = toFlow;
    d->activeMappings = toList;
    if (toP)
        laik_addDataForPartitioning(toP, d);

    if (d->space->inst->do_profiling)
        d->space->inst->time_total += laik_wtime() -
                                     d->space->inst->timer_total;
}

// switch to another data flow, keep partitioning
void laik_switchto_flow(Laik_Data* d, Laik_DataFlow toFlow)
{
    if (!d->activePartitioning) {
        // makes no sense without partitioning
        laik_panic("laik_switch_flow without active partitioning!");
    }
    laik_switchto(d, d->activePartitioning, toFlow);
}


// get slice number <n> in own partition
Laik_Slice* laik_data_slice(Laik_Data* d, int n)
{
    if (d->activePartitioning == 0) return 0;
    return laik_my_slice(d->activePartitioning, n);
}

Laik_Partitioning* laik_switchto_new(Laik_Data* d,
                                     Laik_Partitioner* pr,
                                     Laik_DataFlow flow)
{
    Laik_Partitioning* p;
    p = laik_new_partitioning(d->group, d->space, pr);

    laik_log(1, "switch data '%s' to new partitioning '%s'",
             d->name, p->name);

    laik_switchto(d, p, flow);
    return p;
}

// migrate data container to use another group
// (only possible if data does not have to be preserved)
void laik_migrate_data(Laik_Data* d, Laik_Group* g)
{
    // we only support migration if data does not need to preserved
    assert(!laik_do_copyout(d->activeFlow));

    laik_log(1, "migrate data '%s' => group %d (size %d, myid %d)",
             d->name, g->gid, g->size, g->myid);

    // switch to invalid partitioning
    laik_switchto(d, 0, LAIK_DF_None);

    // FIXME: new user of group !
    d->group = g;
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
    if (d->group->myid == -1) {
        laik_log(LAIK_LL_Error,
                 "laik_map called for data '%s' defined on task group %d.\n"
                 "This task is NOT part of the group. Fix your application!\n"
                 "(may crash now if returned address is dereferenced)",
                 d->name, d->group->gid);
    }
    // we must be an active partitioning
    assert(d->activePartitioning);

    Laik_Partitioning* p = d->activePartitioning;

    if (!d->activeMappings) {
        // lazy allocation
        d->activeMappings = prepareMaps(d, p->borders, layout);
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
        laik_log(LAIK_LL_Panic, "Request for one continuous mapping, "
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

