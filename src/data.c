/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// forward decl
int laik_pack_def(Laik_Mapping* m, Laik_Slice* s, Laik_Index* idx,
                char* buf, int size);
int laik_unpack_def(Laik_Mapping* m, Laik_Slice* s, Laik_Index* idx,
                  char* buf, int size);


Laik_Type *laik_Char;
Laik_Type *laik_Int32;
Laik_Type *laik_Int64;
Laik_Type *laik_Float;
Laik_Type *laik_Double;

static int type_id = 0;

Laik_Type* laik_new_type(char* name, Laik_TypeKind kind, int size)
{
    Laik_Type* t = malloc(sizeof(Laik_Type));

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

Laik_SwitchStat* laik_newSwitchStat()
{
    Laik_SwitchStat* ss;
    ss = malloc(sizeof(Laik_SwitchStat));

    ss->switches           = 0;
    ss->switches_noactions = 0;
    ss->mallocCount        = 0;
    ss->freeCount          = 0;
    ss->sendCount          = 0;
    ss->recvCount          = 0;
    ss->reduceCount        = 0;
    ss->mallocedBytes      = 0;
    ss->freedBytes         = 0;
    ss->initedBytes        = 0;
    ss->copiedBytes        = 0;
    ss->sentBytes          = 0;
    ss->receivedBytes      = 0;
    ss->reducedBytes       = 0;

    return ss;
}

void laik_addSwitchStat(Laik_SwitchStat* target, Laik_SwitchStat* src)
{
    target->switches           += src->switches           ;
    target->switches_noactions += src->switches_noactions ;
    target->mallocCount        += src->mallocCount        ;
    target->freeCount          += src->freeCount          ;
    target->sendCount          += src->sendCount          ;
    target->recvCount          += src->recvCount          ;
    target->reduceCount        += src->reduceCount        ;
    target->mallocedBytes      += src->mallocedBytes      ;
    target->freedBytes         += src->freedBytes         ;
    target->initedBytes        += src->initedBytes        ;
    target->copiedBytes        += src->copiedBytes        ;
    target->sentBytes          += src->sentBytes          ;
    target->receivedBytes      += src->receivedBytes      ;
    target->reducedBytes       += src->reducedBytes       ;
}


static int data_id = 0;

Laik_Data* laik_new_data(Laik_Group* group, Laik_Space* space, Laik_Type* type)
{
    assert(group->inst == space->inst);

    Laik_Data* d = malloc(sizeof(Laik_Data));

    d->id = data_id++;
    d->name = strdup("data-0     ");
    sprintf(d->name, "data-%d", d->id);

    d->group = group;
    d->space = space;
    d->type = type;
    assert(type && (type->size > 0));
    d->elemsize = type->size; // TODO: other than POD types

    d->backend_data = 0;
    d->activePartitioning = 0;
    d->activeFlow = LAIK_DF_None;
    d->nextPartitioningUser = 0;
    d->activeMappings = 0;
    d->allocator = 0; // default: malloc/free
    d->stat = laik_newSwitchStat();

    laik_log(1, "new data '%s':\n"
             " type '%s' (elemsize %d), space '%s' (%lu elems, %.3f MB)\n",
             d->name, type->name, d->elemsize, space->name,
             (unsigned long) laik_space_size(space),
             0.000001 * laik_space_size(space) * d->elemsize);

    laik_addDataForInstance(space->inst, d);

    return d;
}

Laik_Data* laik_new_data_1d(Laik_Group* g, Laik_Type* t, uint64_t s1)
{
    Laik_Space* space = laik_new_space_1d(g->inst, s1);
    return laik_new_data(g, space, t);
}

Laik_Data* laik_new_data_2d(Laik_Group* g, Laik_Type* t,
                            uint64_t s1, uint64_t s2)
{
    Laik_Space* space = laik_new_space_2d(g->inst, s1, s2);
    return laik_new_data(g, space, t);
}

// set a data name, for debug output
void laik_data_set_name(Laik_Data* d, char* n)
{
    laik_log(1, "data '%s' renamed to '%s'", d->name, n);

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
    int dims = d->space->dims;

    // number of local slices
    int sn = ba->off[myid+1] - ba->off[myid];
    if (sn == 0) return 0;

    // number of maps
    int n = ba->tslice[ba->off[myid+1] - 1].mapNo + 1;
    assert(n > 0);

    Laik_MappingList* ml;
    ml = malloc(sizeof(Laik_MappingList) + (n-1) * sizeof(Laik_Mapping));
    ml->count = n;

    int mapNo = 0;
    for(int o = ba->off[myid]; o < ba->off[myid+1]; o++, mapNo++) {
        assert(mapNo == ba->tslice[o].mapNo);
        Laik_Mapping* m = &(ml->map[mapNo]);
        m->data = d;
        m->mapNo = mapNo;
        m->reusedFor = -1;

        // required space
        Laik_Slice slc = ba->tslice[o].s;
        m->firstOff = o;
        while((o+1 < ba->off[myid+1]) && (ba->tslice[o+1].mapNo == mapNo)) {
            o++;
            laik_slice_expand(dims, &slc, &(ba->tslice[o].s));
        }
        m->lastOff = o;
        m->requiredSlice = slc;
        m->count = laik_slice_size(dims, &slc);
        m->size[0] = slc.to.i[0] - slc.from.i[0];
        m->size[1] = (dims > 1) ? (slc.to.i[1] - slc.from.i[1]) : 0;
        m->size[2] = (dims > 2) ? (slc.to.i[2] - slc.from.i[2]) : 0;

        // not backed by memory yet, allocation happens lazy
        m->capacity = 0;
        m->start = 0;
        m->base = 0;
        // remember layout request as hint
        m->layout = l;

        if (laik_log_begin(1)) {
            laik_log_append("prepare map for '%s'/%d: req.slice ",
                            d->name, mapNo);
            laik_log_Slice(dims, &slc);
            laik_log_flush(" (off %d - %d, count %d, elemsize %d)\n",
                           m->firstOff, m->lastOff, m->count, d->elemsize);
        }
    }
    assert(n == mapNo);

    return ml;
}

static
void freeMaps(Laik_MappingList* ml, Laik_SwitchStat* ss)
{
    if (ml == 0) return;

    for(int i = 0; i < ml->count; i++) {
        Laik_Mapping* m = &(ml->map[i]);
        assert(m != 0);

        Laik_Data* d = m->data;

        if (m->reusedFor == -1) {
            laik_log(1, "free map for '%s'/%d (capacity %llu, base %p, start %p)\n",
                     d->name, m->mapNo,
                     (unsigned long long) m->capacity, m->base, m->start);

            if (ss) {
                ss->freeCount++;
                ss->freedBytes += m->capacity;
            }

            // TODO: different policies
            if ((!d->allocator) || (!d->allocator->free))
                free(m->start);
            else
                (d->allocator->free)(d, m->start);

            m->base = 0;
            m->start = 0;
        }
        else
            laik_log(1, "free map for '%s'/%d: nothing to do (reused for %d)\n",
                     d->name, m->mapNo, m->reusedFor);
    }

    free(ml);
}

void laik_allocateMap(Laik_Mapping* m, Laik_SwitchStat* ss)
{
    if (m->base) return;
    if (m->count == 0) return;
    Laik_Data* d = m->data;

    m->capacity = m->count * d->elemsize;

    if (ss) {
        ss->mallocCount++;
        ss->mallocedBytes += m->capacity;
    }

    // TODO: different policies
    if ((!d->allocator) || (!d->allocator->malloc))
        m->base = malloc(m->capacity);
    else
        m->base = (d->allocator->malloc)(d, m->capacity);

    // no space around valid indexes
    m->start = m->base;
    m->allocatedSlice = m->requiredSlice;
    m->allocCount = m->count;

    // set layout
    if (!m->layout)
         m->layout = laik_new_layout(LAIK_LT_Default);
    Laik_Layout* l = m->layout;
    l->dims = d->space->dims;
    // TODO: assume that default layout was requested:
    // default is: elements on dim0 consecutive, then dim1, then dim2
    l->stride[0] = 1;
    l->stride[1] = m->requiredSlice.to.i[0] - m->requiredSlice.from.i[0];
    l->stride[2] = m->requiredSlice.to.i[1] - m->requiredSlice.from.i[1];
    l->stride[2] *= l->stride[1];
    l->isFixed = true;
    l->pack = laik_pack_def;
    l->unpack = laik_unpack_def;

    laik_log(1, "allocated memory for '%s'/%d: %d x %d (%llu B) at %p"
             "\n  layout: %dd, strides (%lu/%lu/%lu)",
             d->name, m->mapNo, m->count, d->elemsize,
             (unsigned long long) m->capacity, m->base,
             l->dims, l->stride[0], l->stride[1], l->stride[2]);
}

static
void copyMaps(Laik_Transition* t,
              Laik_MappingList* toList, Laik_MappingList* fromList,
              Laik_SwitchStat* ss)
{
    assert(t->localCount > 0);
    assert(fromList != 0);
    assert(toList != 0);
    for(int i = 0; i < t->localCount; i++) {
        struct localTOp* op = &(t->local[i]);
        assert(op->fromMapNo < fromList->count);
        Laik_Mapping* fromMap = &(fromList->map[op->fromMapNo]);
        assert(op->toMapNo < toList->count);
        Laik_Mapping* toMap = &(toList->map[op->toMapNo]);

        assert(toMap->data == fromMap->data);
        int dims = fromMap->data->space->dims;
        assert((dims>0) && (dims<=3));
        if (toMap->count == 0) {
            // no elements to copy to
            continue;
        }
        if (fromMap->base == 0) {
            // nothing to copy from
            continue;
        }

        // calculate overlapping range between fromMap and toMap
        Laik_Data* d = toMap->data;
        Laik_Slice* s = &(op->slc);
        Laik_Index count, fromStart, toStart;
        laik_sub_index(&count, &(s->to), &(s->from));
        laik_sub_index(&fromStart, &(s->from), &(fromMap->requiredSlice.from));
        laik_sub_index(&toStart, &(s->from), &(toMap->requiredSlice.from));
        if (dims < 3) {
            count.i[2] = 1;
            if (dims < 2) {
                count.i[1] = 1;
            }
        }
        uint64_t ccount = count.i[0] * count.i[1] * count.i[2];
        assert(ccount > 0);

        // no copy needed if mapping reused
        if (fromMap->reusedFor == op->toMapNo) {
            uint64_t fromOff = laik_offset(&fromStart, fromMap->layout);
            uint64_t toOff = laik_offset(&toStart, toMap->layout);

            assert(fromMap->base + fromOff * d->elemsize ==
                   toMap->base   + toOff * d->elemsize);

            if (laik_log_begin(1)) {
                laik_log_append("copy map for '%s': (%lu x %lu x %lu)",
                                d->name, count.i[0], count.i[1], count.i[2]);
                laik_log_append(" x %d from global (", d->elemsize);
                laik_log_Index(dims, &(s->from));
                laik_log_append("): local (");
                laik_log_Index(dims, &fromStart);
                laik_log_append(") slc/map %d/%d ==> (",
                                op->fromSliceNo, op->fromMapNo);
                laik_log_Index(dims, &toStart);
                laik_log_flush(") slc/map %d/%d, using old map\n",
                               op->toSliceNo, op->toMapNo);
            }
            continue;
        }

        if (toMap->base == 0) {
            // need to allocate memory
            laik_allocateMap(toMap, ss);
        }
        uint64_t fromOff  = laik_offset(&fromStart, fromMap->layout);
        uint64_t toOff    = laik_offset(&toStart, toMap->layout);
        char*    fromPtr  = fromMap->base + fromOff * d->elemsize;
        char*    toPtr    = toMap->base   + toOff * d->elemsize;

        if (laik_log_begin(1)) {
            laik_log_append("copy map for '%s': (%lu x %lu x %lu)",
                            d->name, count.i[0], count.i[1], count.i[2]);
            laik_log_append(" x %d from global (", d->elemsize);
            laik_log_Index(dims, &(s->from));
            laik_log_append("): local (");
            laik_log_Index(dims, &fromStart);
            laik_log_append(") slc/map %d/%d off %lu/%p ==> (",
                            op->fromSliceNo, op->fromMapNo, fromOff, fromPtr);
            laik_log_Index(dims, &toStart);
            laik_log_flush(") slc/map %d/%d off %lu/%p",
                           op->toSliceNo, op->toMapNo, toOff, toPtr);
        }

        if (ss)
            ss->copiedBytes += ccount * d->elemsize;

        for(uint64_t i3 = 0; i3 < count.i[2]; i3++) {
            char *fromPtr2 = fromPtr;
            char *toPtr2 = toPtr;
            for(uint64_t i2 = 0; i2 < count.i[1]; i2++) {
                memcpy(toPtr2, fromPtr2, count.i[0] * d->elemsize);
                fromPtr2 += fromMap->layout->stride[1] * d->elemsize;
                toPtr2   += toMap->layout->stride[1] * d->elemsize;
            }
            fromPtr += fromMap->layout->stride[2] * d->elemsize;
            toPtr   += toMap->layout->stride[2] * d->elemsize;
        }
    }
}

// try to reuse already allocated memory from old mapping
// we reuse mapping if it has same or larger size
// and if old mapping covered all indexed needed in new mapping.
// TODO: use a policy setting
static
void checkMapReuse(Laik_MappingList* toList, Laik_MappingList* fromList)
{
    // reuse only possible if old mappings exist
    if (!fromList) return;
    if ((toList == 0) || (toList->count ==0)) return;
    Laik_Data* d = toList->map[0].data;
    int dims = d->space->dims;

    for(int i = 0; i < toList->count; i++) {
        Laik_Mapping* toMap = &(toList->map[i]);
        for(int sNo = 0; sNo < fromList->count; sNo++) {
            Laik_Mapping* fromMap = &(fromList->map[sNo]);
            if (fromMap->base == 0) continue;
            if (fromMap->reusedFor >= 0) continue; // only reuse once

            // does index range fit into old?
            if (!laik_slice_within_slice(dims, &(toMap->requiredSlice),
                                         &(fromMap->allocatedSlice))) {
                // no, cannot reuse
                continue;
            }
            // always reuse larger mapping

            toMap->start     = fromMap->start;
            toMap->allocatedSlice = fromMap->allocatedSlice;
            toMap->allocCount = fromMap->allocCount;
            toMap->capacity  = fromMap->capacity;
            toMap->layout    = fromMap->layout;

            // offset of validSlice.from in mapping of fullSlice
            Laik_Index idx;
            laik_sub_index(&idx,
                           &(toMap->requiredSlice.from),
                           &(toMap->allocatedSlice.from));
            uint64_t off = laik_offset(&idx, toMap->layout);

            toMap->base = toMap->start + off * d->elemsize;
            fromMap->reusedFor = i; // mark as reused by slice <i>

            if (laik_log_begin(1)) {
                laik_log_append("map reuse for '%s'/%d ", toMap->data->name, i);
                laik_log_Slice(dims, &(toMap->requiredSlice));
                laik_log_append(" (in ");
                laik_log_Slice(dims, &(toMap->allocatedSlice));
                laik_log_flush(" with off %llu), %llu B at %p)\n",
                               (unsigned long long) off,
                               (unsigned long long) fromMap->capacity,
                               toMap->base);
            }
        }
    }
}

static
void initMaps(Laik_Transition* t,
              Laik_MappingList* toList, Laik_MappingList* fromList,
              Laik_SwitchStat* ss)
{
    assert(t->initCount > 0);
    for(int i = 0; i < t->initCount; i++) {
        struct initTOp* op = &(t->init[i]);
        assert(op->mapNo < toList->count);
        Laik_Mapping* toMap = &(toList->map[op->mapNo]);

        if (toMap->count == 0) {
            // no elements to initialize
            continue;
        }

        if (toMap->base == 0) {
            // allocate memory
            laik_allocateMap(toMap, ss);
        }

        int dims = toMap->data->space->dims;
        assert(dims == 1); // only for 1d now
        Laik_Data* d = toMap->data;
        Laik_Slice* s = &(op->slc);
        int count = s->to.i[0] - s->from.i[0];

        if (ss)
            ss->initedBytes += count * d->elemsize;

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

        laik_log(1, "init map for '%s' slc/map %d/%d: %d x at [%lu\n",
                 d->name, op->sliceNo, op->mapNo, count, s->from.i[0]);
    }
}

static
void doTransition(Laik_Data* d, Laik_Transition* t,
                  Laik_MappingList* fromList, Laik_MappingList* toList)
{
    if (d->stat) {
        d->stat->switches++;
        if (!t || (t->actionCount == 0))
            d->stat->switches_noactions++;
    }

    if (t) {
        // be careful when reusing mappings:
        // the backend wants to send/receive data in arbitrary order
        // (to avoid deadlocks), but it never should overwrite data
        // before it gets sent by data already received.
        // thus it is bad to reuse a mapping for different index ranges.
        // but reusing mappings such that same indexes go to same address
        // is fine.
        checkMapReuse(toList, fromList);

        if (t->sendCount + t->recvCount + t->redCount > 0) {
            // let backend do send/recv/reduce actions

            Laik_Instance* inst = d->space->inst;
            if (inst->do_profiling)
                inst->timer_backend = laik_wtime();

            assert(inst->backend->execTransition);
            (inst->backend->execTransition)(d, t, fromList, toList);

            if (inst->do_profiling)
                inst->time_backend += laik_wtime() - inst->timer_backend;
        }

        // local copy actions
        if (t->localCount > 0)
            copyMaps(t, toList, fromList, d->stat);

        // local init action
        if (t->initCount > 0)
            initMaps(t, toList, fromList, d->stat);
    }

    // free old mapping/partitioning
    if (fromList)
        freeMaps(fromList, d->stat);
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

    if (laik_log_begin(1)) {
        laik_log_append("transition (data '%s', partition '%s'):\n",
                        d->name, part ? part->name : "(none)");
        laik_log_append("  from ");
        laik_log_DataFlow(d->activeFlow);
        laik_log_append(", ");
        laik_log_BorderArray(fromBA);
        laik_log_append("\n  to ");
        laik_log_DataFlow(toFlow);
        laik_log_append(", ");
        laik_log_BorderArray(toBA);
        laik_log_append(": ");
        laik_log_Transition(t);
        laik_log_flush(0);
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

    if (laik_log_begin(1)) {
        laik_log_append("switch partitionings for data '%s':\n"
                        "  %s/",
                        d->name, fromP ? fromP->name : "(none)");
        laik_log_DataFlow(d->activeFlow);
        laik_log_append(" => %s/", toP ? toP->name : "(none)");
        laik_log_DataFlow(toFlow);
        laik_log_append(": ");
        laik_log_Transition(t);
        laik_log_flush(0);
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
Laik_TaskSlice* laik_data_slice(Laik_Data* d, int n)
{
    if (d->activePartitioning == 0) return 0;
    return laik_my_slice(d->activePartitioning, n);
}

Laik_Partitioning* laik_switchto_new(Laik_Data* d,
                                     Laik_Partitioner* pr,
                                     Laik_DataFlow flow)
{
    Laik_Partitioning* p;
    p = laik_new_partitioning(d->group, d->space, pr, 0);

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
    Laik_Layout* l = malloc(sizeof(Laik_Layout));

    l->type = t;
    l->isFixed = false;
    l->dims = 0; // invalid
    l->unpack = 0;
    l->pack = 0;

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

// for a local index (1d/2d/3d), return offset into memory mapping
// e.g. for (0) / (0,0) / (0,0,0) it returns offset 0
uint64_t laik_offset(Laik_Index* idx, Laik_Layout* l)
{
    assert(l);

    // TODO: only default layout with order 1/2/3
    assert(l->stride[0] == 1);
    if (l->dims > 1) {
        assert(l->stride[0] <= l->stride[1]);
        if (l->dims > 2)
            assert(l->stride[1] <= l->stride[2]);
    }

    uint64_t off = idx->i[0];
    if (l->dims > 1) {
        off += idx->i[1] * l->stride[1];
        if (l->dims > 2) {
            off += idx->i[2] * l->stride[2];
        }
    }
    return off;
}

// pack/unpack routines for default layout
int laik_pack_def(Laik_Mapping* m, Laik_Slice* s, Laik_Index* idx,
                  char* buf, int size)
{
    int elemsize = m->data->elemsize;
    int dims = m->layout->dims;

    if (laik_index_isEqual(dims, idx, &(s->to))) {
        // nothing left to pack
        return 0;
    }

    // TODO: only default layout with order 1/2/3
    assert(m->layout->stride[0] == 1);
    if (dims > 1) {
        assert(m->layout->stride[0] <= m->layout->stride[1]);
        if (dims > 2)
            assert(m->layout->stride[1] <= m->layout->stride[2]);
    }

    // slice to pack must within local valid slice of mapping
    assert(laik_slice_within_slice(dims, s, &(m->requiredSlice)));

    // calculate address of starting index
    Laik_Index localIdx;
    laik_sub_index(&localIdx, idx, &(m->requiredSlice.from));
    uint64_t idxOff = laik_offset(&localIdx, m->layout);
    char* idxPtr = m->base + idxOff * elemsize;

    uint64_t i0, i1, i2, from0, from1, to0, to1, to2, count;
    from0 = s->from.i[0];
    from1 = s->from.i[1];
    to0 = s->to.i[0];
    to1 = s->to.i[1];
    to2 = s->to.i[2];
    i0 = idx->i[0];
    i1 = idx->i[1];
    i2 = idx->i[2];
    if (dims < 3) {
        to2 = 1; i2 = 0;
        if (dims < 2) {
            from1 = 0; to1 = 1; i1 = 0;
        }
    }
    count = 0;

    // elements to skip after to0 reached
    uint64_t skip0 = m->layout->stride[1] - (to0 - from0);
    // elements to skip after to1 reached
    uint64_t skip1 = m->layout->stride[2] - m->layout->stride[1] * (to1 - from1);

    if (laik_log_begin(1)) {
        Laik_Index slcsize, localFrom;
        laik_sub_index(&localFrom, &(s->from), &(m->requiredSlice.from));
        laik_sub_index(&slcsize, &(s->to), &(s->from));

        laik_log_append("packing for '%s', size (", m->data->name);
        laik_log_Index(dims, &slcsize);
        laik_log_append(") x %d from global (", elemsize);
        laik_log_Index(dims, &(s->from));
        laik_log_append(") / local (");
        laik_log_Index(dims, &localFrom);
        laik_log_append(")/%d, start (", m->mapNo);
        laik_log_Index(dims, idx);
        laik_log_flush(") off %lu, buf size %d", idxOff, size);
    }

    bool stop = false;
    for(; i2 < to2; i2++) {
        for(; i1 < to1; i1++) {
            for(; i0 < to0; i0++) {
                if (size < elemsize) {
                    stop = true;
                    break;
                }

#if DEBUG_PACK
                laik_log(1, "packing (%lu/%lu/%lu) off %lu: %.3f, left %d",
                         i0, i1, i2,
                         (idxPtr - m->base)/elemsize, *(double*)idxPtr,
                         size - elemsize);
#endif

                // copy element into buffer
                memcpy(buf, idxPtr, elemsize);

                idxPtr += elemsize; // stride[0] is 1
                size -= elemsize;
                buf += elemsize;
                count++;

            }
            if (stop) break;
            idxPtr += skip0 * elemsize;
            i0 = from0;
        }
        if (stop) break;
        idxPtr += skip1 * elemsize;
        i1 = from1;
    }
    if (!stop) {
        // we reached end, set i0/i1 to last positions
        i0 = to0;
        i1 = to1;
    }

    if (laik_log_begin(1)) {
        Laik_Index idx2;
        laik_set_index(&idx2, i0, i1, i2);

        laik_log_append("packed for '%s': end (", m->data->name);
        laik_log_Index(dims, &idx2);
        laik_log_flush("), %lu elems = %lu bytes, %d left",
                       count, count * elemsize, size);
    }

    // save position we reached
    idx->i[0] = i0;
    idx->i[1] = i1;
    idx->i[2] = i2;
    return count;
}

int laik_unpack_def(Laik_Mapping* m, Laik_Slice* s, Laik_Index* idx,
                    char* buf, int size)
{
    int elemsize = m->data->elemsize;
    int dims = m->layout->dims;

    // there should be something to unpack
    assert(size > 0);
    assert(!laik_index_isEqual(dims, idx, &(s->to)));

    // TODO: only default layout with order 1/2/3
    assert(m->layout->stride[0] == 1);
    if (dims > 1) {
        assert(m->layout->stride[0] <= m->layout->stride[1]);
        if (dims > 2)
            assert(m->layout->stride[1] <= m->layout->stride[2]);
    }

    // slice to unpack into must be within local valid slice of mapping
    assert(laik_slice_within_slice(dims, s, &(m->requiredSlice)));

    // calculate address of starting index
    Laik_Index localIdx;
    laik_sub_index(&localIdx, idx, &(m->requiredSlice.from));
    uint64_t idxOff = laik_offset(&localIdx, m->layout);
    char* idxPtr = m->base + idxOff * elemsize;

    uint64_t i0, i1, i2, from0, from1, to0, to1, to2, count;
    from0 = s->from.i[0];
    from1 = s->from.i[1];
    to0 = s->to.i[0];
    to1 = s->to.i[1];
    to2 = s->to.i[2];
    i0 = idx->i[0];
    i1 = idx->i[1];
    i2 = idx->i[2];
    if (dims < 3) {
        to2 = 1; i2 = 0;
        if (dims < 2) {
            from1 = 0; to1 = 1; i1 = 0;
        }
    }
    count = 0;

    // elements to skip after to0 reached
    uint64_t skip0 = m->layout->stride[1] - (to0 - from0);
    // elements to skip after to1 reached
    uint64_t skip1 = m->layout->stride[2] - m->layout->stride[1] * (to1 - from1);

    if (laik_log_begin(1)) {
        Laik_Index slcsize, localFrom;
        laik_sub_index(&localFrom, &(s->from), &(m->requiredSlice.from));
        laik_sub_index(&slcsize, &(s->to), &(s->from));

        laik_log_append("unpacking for '%s', size (", m->data->name);
        laik_log_Index(dims, &slcsize);
        laik_log_append(") x %d from global (", elemsize);
        laik_log_Index(dims, &(s->from));
        laik_log_append(") / local (");
        laik_log_Index(dims, &localFrom);
        laik_log_append(")/%d, start (", m->mapNo);
        laik_log_Index(dims, idx);
        laik_log_flush(") off %lu, buf size %d", idxOff, size);

    }

    bool stop = false;
    for(; i2 < to2; i2++) {
        for(; i1 < to1; i1++) {
            for(; i0 < to0; i0++) {
                if (size < elemsize) {
                    stop = true;
                    break;
                }

#if DEBUG_UNPACK
                laik_log(1, "unpacking (%lu/%lu/%lu) off %lu: %.3f, left %d",
                         i0, i1, i2,
                         (idxPtr - m->base)/elemsize, *(double*)buf,
                         size - elemsize);
#endif
                // copy element from buffer into local data
                memcpy(idxPtr, buf, elemsize);

                idxPtr += elemsize; // stride[0] is 1
                size -= elemsize;
                buf += elemsize;
                count++;

            }
            if (stop) break;
            idxPtr += skip0 * elemsize;
            i0 = from0;
        }
        if (stop) break;
        idxPtr += skip1 * elemsize;
        i1 = from1;
    }
    if (!stop) {
        // we reached end, set i0/i1 to last positions
        i0 = to0;
        i1 = to1;
    }

    if (laik_log_begin(1)) {
        Laik_Index idx2;
        laik_set_index(&idx2, i0, i1, i2);

        laik_log_append("unpacked for '%s': end (", m->data->name);
        laik_log_Index(dims, &idx2);
        laik_log_flush("), %lu elems = %lu bytes, %d left",
                       count, count * elemsize, size);
    }

    // save position we reached
    idx->i[0] = i0;
    idx->i[1] = i1;
    idx->i[2] = i2;
    return count;
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
    laik_allocateMap(m, d->stat);

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
    int n = laik_my_mapcount(d->activePartitioning);
    if (n > 1)
        laik_log(LAIK_LL_Panic, "Request for one continuous mapping, "
                                "but partition with %d slices!\n", n);

    if (base) *base = m ? m->base : 0;
    if (count) *count = m ? m->count : 0;
    return m;
}

Laik_Mapping* laik_map_def1_2d(Laik_Data* d,
                               void** base, uint64_t* ysize,
                               uint64_t* ystride, uint64_t* xsize)
{
    Laik_Mapping* m = laik_map(d, 0, 0);
    if (!m) {
        laik_log(LAIK_LL_Error, "laik_map: could not map slice 0 of data '%s'",
                 d->name);
        return 0;
    }

    int n = laik_my_mapcount(d->activePartitioning);
    if (n > 1)
        laik_log(LAIK_LL_Error, "Request for one continuous mapping, "
                                "but partition with %d slices!", n);

    Laik_Layout* l = m->layout;
    assert(l);
    if (l->dims != 2)
        laik_log(LAIK_LL_Error, "Request for 2d mapping of %dd space!",
                 l->dims);

    if (base)    *base    = m->base;
    if (xsize)   *xsize   = m->size[0];
    if (ysize)   *ysize   = m->size[1];
    if (ystride) *ystride = l->stride[1];
    return m;
}

Laik_Mapping* laik_map_def1_3d(Laik_Data* d, void** base,
                               uint64_t* zsize, uint64_t* zstride,
                               uint64_t* ysize, uint64_t* ystride,
                               uint64_t* xsize)
{
    Laik_Mapping* m = laik_map(d, 0, 0);
    if (!m) {
        laik_log(LAIK_LL_Error, "laik_map: could not map slice 0 of data '%s'",
                 d->name);
        return 0;
    }

    int n = laik_my_mapcount(d->activePartitioning);
    if (n > 1)
        laik_log(LAIK_LL_Error, "Request for one continuous mapping, "
                                "but partition with %d slices!", n);

    Laik_Layout* l = m->layout;
    assert(l);
    if (l->dims != 3)
        laik_log(LAIK_LL_Error, "Request for 3d mapping of %dd space!",
                 l->dims);

    if (base)    *base    = m->base;
    if (xsize)   *xsize   = m->size[0];
    if (ysize)   *ysize   = m->size[1];
    if (ystride) *ystride = l->stride[1];
    if (zsize)   *zsize   = m->size[2];
    if (zstride) *zstride = l->stride[2];
    return m;
}


Laik_Mapping* laik_global2local_1d(Laik_Data* d, uint64_t gidx, uint64_t* lidx)
{
    assert(d->space->dims == 1);
    if (!d->activeMappings) return 0;
    for(int i = 0; i < d->activeMappings->count; i++) {
        Laik_Mapping* m = &(d->activeMappings->map[i]);

        if (gidx < m->requiredSlice.from.i[0]) continue;
        if (gidx >= m->requiredSlice.to.i[0]) continue;

        if (lidx) *lidx = gidx - m->requiredSlice.from.i[0];
        return m;
    }
    return 0;
}

uint64_t laik_local2global_1d(Laik_Data* d, uint64_t off)
{
    assert(d->space->dims == 1);
    assert(d->activeMappings && (d->activeMappings->count == 1));

    Laik_Mapping* m = &(d->activeMappings->map[0]);
    assert(off < m->count);

    // TODO: take layout into account
    return m->requiredSlice.from.i[0] + off;
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
    Laik_Allocator* a = malloc(sizeof(Laik_Allocator));

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

