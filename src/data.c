/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// forward decl
int laik_pack_def(const Laik_Mapping* m, const Laik_Slice* s, Laik_Index* idx,
                char* buf, int size);
int laik_unpack_def(const Laik_Mapping* m, const Laik_Slice* s, Laik_Index* idx,
                  char* buf, int size);

// initialize the LAIK data module, called from laik_new_instance
void laik_data_init()
{
    laik_type_init();
}


Laik_SwitchStat* laik_newSwitchStat()
{
    Laik_SwitchStat* ss;
    ss = malloc(sizeof(Laik_SwitchStat));
    if (!ss) {
        laik_panic("Out of memory allocating Laik_SwitchStat object");
        exit(1); // not actually needed, laik_panic never returns
    }

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

Laik_Data* laik_new_data(Laik_Space* space, Laik_Type* type)
{
    Laik_Data* d = malloc(sizeof(Laik_Data));
    if (!d) {
        laik_panic("Out of memory allocating Laik_Data object");
        exit(1); // not actually needed, laik_panic never returns
    }

    d->id = data_id++;
    d->name = strdup("data-0     ");
    sprintf(d->name, "data-%d", d->id);

    d->space = space;
    d->type = type;
    assert(type && (type->size > 0));
    d->elemsize = type->size; // TODO: other than POD types

    d->backend_data = 0;
    d->activePartitioning = 0;
    d->activeFlow = LAIK_DF_None;
    d->activeAccessPhase = 0;
    d->nextAccessPhaseUser = 0;
    d->activeMappings = 0;
    d->allocator = 0; // default: malloc/free
    d->stat = laik_newSwitchStat();

    d->activeReservation = 0;

    laik_log(1, "new data '%s':\n"
             " type '%s' (elemsize %d), space '%s' (%lu elems, %.3f MB)\n",
             d->name, type->name, d->elemsize, space->name,
             (unsigned long) laik_space_size(space),
             0.000001 * laik_space_size(space) * d->elemsize);

    laik_addDataForInstance(space->inst, d);

    return d;
}

Laik_Data* laik_new_data_1d(Laik_Instance* i, Laik_Type* t, int64_t s1)
{
    Laik_Space* space = laik_new_space_1d(i, s1);
    return laik_new_data(space, t);
}

Laik_Data* laik_new_data_2d(Laik_Instance* i, Laik_Type* t,
                            int64_t s1, int64_t s2)
{
    Laik_Space* space = laik_new_space_2d(i, s1, s2);
    return laik_new_data(space, t);
}

// set a data name, for debug output
void laik_data_set_name(Laik_Data* d, char* n)
{
    laik_log(1, "data '%s' renamed to '%s'", d->name, n);

    d->name = n;
}

// get space used for data
Laik_Space* laik_data_get_space(Laik_Data* d)
{
    return d->space;
}

//  get process group among data currently is distributed
Laik_Group* laik_data_get_group(Laik_Data* d)
{
    if (d->activePartitioning)
        return d->activePartitioning->group;
    return 0;
}

// get instance managing data
Laik_Instance* laik_data_get_inst(Laik_Data* d)
{
    return d->space->inst;
}

// get active partitioning of data container
Laik_Partitioning* laik_data_get_partitioning(Laik_Data* d)
{
    return d->activePartitioning;
}

// get active access phase of data container
Laik_AccessPhase* laik_data_get_accessphase(Laik_Data* d)
{
    return d->activeAccessPhase;
}

static
void initMapping(Laik_Mapping* m, Laik_Data* d)
{
    m->data = d;
    m->mapNo = -1;
    m->reusedFor = -1;

    // set requiredSlice to empty
    m->requiredSlice.from.i[0] = 0;
    m->requiredSlice.to.i[0] = 0;

    m->count = 0;
    m->size[0] = 0;
    m->size[1] = 0;
    m->size[2] = 0;

    // not backed by memory yet
    m->capacity = 0;
    m->start = 0;
    m->base = 0;
    m->layout = 0;

    // not embedded in another mapping
    m->baseMapping = 0;
}

static
Laik_MappingList* prepareMaps(Laik_Data* d, Laik_Partitioning* p,
                              Laik_Layout* l)
{
    if (!p) return 0; // without partitioning borders there are no mappings

    int myid = laik_myid(p->group);
    if (myid == -1) return 0; // this task is not part of the task group
    assert(myid < p->group->size);
    int dims = d->space->dims;

    // reserved and already allocated?
    Laik_Reservation* r = d->activeReservation;
    if (r) {
        for(int i = 0; i < r->count; i++) {
            if ((r->entry[i].p == p) && (r->entry[i].mList != 0)) {
                Laik_MappingList* ml = r->entry[i].mList;
                assert(ml->res == r);
                return ml;
            }
        }
    }

    // number of local slices
    int sn = p->off[myid+1] - p->off[myid];
    if (sn == 0) return 0;

    // number of maps
    int n = p->tslice[p->off[myid+1] - 1].mapNo + 1;
    assert(n > 0);

    Laik_MappingList* ml;
    ml = malloc(sizeof(Laik_MappingList) + n * sizeof(Laik_Mapping));
    if (!ml) {
        laik_panic("Out of memory allocating Laik_MappingList object");
        exit(1); // not actually needed, laik_panic never returns
    }
    ml->res = 0; // not part of a reservation
    ml->count = n;

    int firstOff, lastOff;
    int mapNo = 0;
    for(int o = p->off[myid]; o < p->off[myid+1]; o++, mapNo++) {
        assert(mapNo == p->tslice[o].mapNo);
        Laik_Mapping* m = &(ml->map[mapNo]);
        initMapping(m, d);
        m->mapNo = mapNo;
        // remember layout request as hint
        m->layout = l;

        // required space
        Laik_Slice slc = p->tslice[o].s;
        firstOff = o;
        while((o+1 < p->off[myid+1]) && (p->tslice[o+1].mapNo == mapNo)) {
            o++;
            laik_slice_expand(dims, &slc, &(p->tslice[o].s));
        }
        lastOff = o;
        m->requiredSlice = slc;
        m->count = laik_slice_size(dims, &slc);
        m->size[0] = slc.to.i[0] - slc.from.i[0];
        m->size[1] = (dims > 1) ? (slc.to.i[1] - slc.from.i[1]) : 0;
        m->size[2] = (dims > 2) ? (slc.to.i[2] - slc.from.i[2]) : 0;


        if (laik_log_begin(1)) {
            laik_log_append("prepare map for '%s'/%d: req.slice ",
                            d->name, mapNo);
            laik_log_Slice(dims, &slc);
            laik_log_flush(" (off %d - %d, count %d, elemsize %d)\n",
                           firstOff, lastOff, m->count, d->elemsize);
        }
    }
    assert(n == mapNo);

    return ml;
}

static
void freeMap(Laik_Mapping* m, Laik_Data* d, Laik_SwitchStat* ss)
{
    assert(d == m->data);

    if (m->reusedFor == -1) {
        laik_log(1, "free map for '%s'/%d (capacity %llu, base %p, start %p)\n",
                 d->name, m->mapNo,
                 (unsigned long long) m->capacity, (void*) m->base, (void*) m->start);

        // concrete, fixed layouts are only used once: free
        if (m->layout && m->layout->isFixed) {
            free(m->layout);
            m->layout = 0;
        }

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

static
void freeMaps(Laik_MappingList* ml, Laik_SwitchStat* ss)
{
    if (ml == 0) return;

    // never free mappings from a reservation
    if (ml->res != 0) return;

    for(int i = 0; i < ml->count; i++) {
        Laik_Mapping* m = &(ml->map[i]);
        assert(m != 0);

        freeMap(m, m->data, ss);
    }

    free(ml);
}

// always the same layout
static
Laik_Layout* laik_new_layout_def_1d()
{
    static Laik_Layout* l = 0;
    if (!l) {
        l = laik_new_layout(LAIK_LT_Default);
        l->dims = 1;
        l->stride[0] = 1;
        l->stride[1] = 0;
        l->stride[2] = 0;
        l->isFixed = false; // this prohibits free() on layout
        l->pack = laik_pack_def;
        l->unpack = laik_unpack_def;
    }
    return l;
}

static
Laik_Layout* laik_new_layout_def_2d(uint64_t stride)
{
    Laik_Layout* l = laik_new_layout(LAIK_LT_Default);
    l->dims = 2;
    l->stride[0] = 1;
    l->stride[1] = stride;
    l->stride[2] = 0;
    l->isFixed = true;
    l->pack = laik_pack_def;
    l->unpack = laik_unpack_def;

    return l;
}

static
Laik_Layout* laik_new_layout_def_3d(uint64_t stride1, uint64_t stride2)
{
    Laik_Layout* l = laik_new_layout(LAIK_LT_Default);
    l->dims = 3;
    l->stride[0] = 1;
    l->stride[1] = stride1;
    l->stride[2] = stride1 * stride2;
    l->isFixed = true;
    l->pack = laik_pack_def;
    l->unpack = laik_unpack_def;

    return l;
}


void laik_allocateMap(Laik_Mapping* m, Laik_SwitchStat* ss)
{
    // should only be called if not embedded in another mapping
    assert(m->baseMapping == 0);

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

    if (!m->base) {
        laik_log(LAIK_LL_Panic,
                 "Out of memory allocating memory for mapping "
                 "(data '%s', mapNo %d, size %llu)",
                 m->data->name, m->mapNo, (unsigned long long int) m->capacity);
        exit(1); // not actually needed, laik_log never returns
    }

    // no space around valid indexes
    m->start = m->base;
    m->allocatedSlice = m->requiredSlice;
    m->allocCount = m->count;

    // if a layout is given, it must be a layout hint: not fixed
    if (m->layout) assert(m->layout->isFixed == false);

    // TODO: for now, we always set a new, concrete layout
    switch(d->space->dims) {
    case 1:
        m->layout = laik_new_layout_def_1d();
        break;
    case 2: {
        uint64_t s = m->requiredSlice.to.i[0] - m->requiredSlice.from.i[0];
        m->layout = laik_new_layout_def_2d(s);
        break;
    }
    case 3:  {
        uint64_t s1 = m->requiredSlice.to.i[0] - m->requiredSlice.from.i[0];
        uint64_t s2 = m->requiredSlice.to.i[1] - m->requiredSlice.from.i[1];
        m->layout = laik_new_layout_def_3d(s1, s2);
        break;
    }
    default: assert(0);
    }

    laik_log(1, "allocated memory for '%s'/%d: %llu x %d (%llu B) at %p"
             "\n  layout: %dd, strides (%llu/%llu/%llu)",
             d->name, m->mapNo, (unsigned long long int) m->count, d->elemsize,
             (unsigned long long) m->capacity, (void*) m->base,
             m->layout->dims,
             (unsigned long long) m->layout->stride[0],
             (unsigned long long) m->layout->stride[1],
             (unsigned long long) m->layout->stride[2]);
}

static
void copyMaps(Laik_Transition* t,
              Laik_MappingList* toList, Laik_MappingList* fromList,
              Laik_SwitchStat* ss)
{
    assert(t->localCount > 0);
    assert(fromList != 0);
    assert(toList != 0);

    // no copy required if we stay in same reservation
    if ((fromList->res != 0) && (fromList->res == toList->res)) return;

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

        for(int64_t i3 = 0; i3 < count.i[2]; i3++) {
            char *fromPtr2 = fromPtr;
            char *toPtr2 = toPtr;
            for(int64_t i2 = 0; i2 < count.i[1]; i2++) {
                memcpy(toPtr2, fromPtr2, count.i[0] * d->elemsize);
                fromPtr2 += fromMap->layout->stride[1] * d->elemsize;
                toPtr2   += toMap->layout->stride[1] * d->elemsize;
            }
            fromPtr += fromMap->layout->stride[2] * d->elemsize;
            toPtr   += toMap->layout->stride[2] * d->elemsize;
        }
    }
}

// given that memory for a mapping is already allocated,
// we want to re-use part of it for another mapping <toMap>.
// given required slice in <toMap>, it must be part of allocation in <fromMap>
// the original base mapping descriptor can be removed afterwards if it
// is ensured that memory is not deleted beyound the new mapping
static
void initEmbeddedMapping(Laik_Mapping* toMap, Laik_Mapping* fromMap)
{
    Laik_Data* data = toMap->data;
    int dims = data->space->dims;
    assert(data == fromMap->data);

    assert(laik_slice_within_slice(dims, &(toMap->requiredSlice),
                                   &(fromMap->allocatedSlice)));

    // take over allocation into new mapping descriptor
    toMap->start = fromMap->start;
    toMap->allocatedSlice = fromMap->allocatedSlice;
    toMap->allocCount = fromMap->allocCount;
    toMap->capacity = fromMap->capacity;
    toMap->layout = fromMap->layout;

    // set <base> of embedded mapping according to required vs. allocated
    Laik_Index idx;
    laik_sub_index(&idx,
                   &(toMap->requiredSlice.from),
                   &(toMap->allocatedSlice.from));
    uint64_t off = laik_offset(&idx, toMap->layout);
    toMap->base = toMap->start + off * data->elemsize;
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

    // no automatic reuse check allowed if reservations are involved
    // (if we stay in same reservation, space is reused anyways)
    if ((fromList->res != 0) || (toList->res != 0)) return;

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
            initEmbeddedMapping(toMap, fromMap);

            // mark as reused by slice <i>: this prohibits delete of memory
            fromMap->reusedFor = i;

            if (laik_log_begin(1)) {
                laik_log_append("map reuse for '%s'/%d ", toMap->data->name, i);
                laik_log_Slice(dims, &(toMap->requiredSlice));
                laik_log_append(" (in ");
                laik_log_Slice(dims, &(toMap->allocatedSlice));
                laik_log_flush(" with byte-off %llu), %llu Bytes at %p)\n",
                               (unsigned long long) (toMap->base - toMap->start),
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
    (void) fromList; /* FIXME: Why have this parameter if it's never used */

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
        int from = s->from.i[0];
        int to = s->to.i[0];
        int elemCount = to - from;

        char* toBase = toMap->base;
        assert(from >= toMap->requiredSlice.from.i[0]);
        toBase += (from - toMap->requiredSlice.from.i[0]) * d->elemsize;

        if (ss)
            ss->initedBytes += elemCount * d->elemsize;

        if (d->type->init)
            (d->type->init)(toBase, elemCount, op->redOp);
        else {
            laik_log(LAIK_LL_Panic,
                     "Need initialization function for type '%s'. Not set!",
                     d->type->name);
            assert(0);
        }

        laik_log(1, "init map for '%s' slc/map %d/%d: %d entries in [%d;%d[ from %p\n",
                 d->name, op->sliceNo, op->mapNo, elemCount, from, to, (void*) toBase);
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
            if (inst->profiling->do_profiling)
                inst->profiling->timer_backend = laik_wtime();

            if (inst->backend->prepare) {
                // backend driver supports asynchronous communication
                // TODO: do prepare as early as possible
                Laik_TransitionPlan* p = (inst->backend->prepare)(d, t);
                (inst->backend->exec)(d, t, p, fromList, toList);
                // TODO: only wait in laik_map/laik_wait
                (inst->backend->wait)(p, -1);
                (inst->backend->cleanup)(p);
            }
            else {
                (inst->backend->exec)(d, t, 0, fromList, toList);
            }

            if (inst->profiling->do_profiling)
                inst->profiling->time_backend += laik_wtime() - inst->profiling->timer_backend;
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

// make data container aware of reservation
void laik_data_use_reservation(Laik_Data* d, Laik_Reservation* r)
{
    assert(r->data == d);
    d->activeReservation = r;
}


//
// Reservation
//

// create a reservation object for <data>
Laik_Reservation* laik_reservation_new(Laik_Data* d)
{
    Laik_Reservation* r = malloc(sizeof(Laik_Reservation));
    r->data = d;

    r->count = 0;
    r->capacity = 0;
    r->entry = 0;
    r->mappingCount = 0;
    r->mapping = 0;

    return r;
}

// register a partitioning for inclusion in a reservation:
// this will include space required for this partitioning on allocation
void laik_reservation_add(Laik_Reservation* r, Laik_Partitioning* p)
{
    if (r->count == r->capacity) {
        r->capacity = 10 + r->capacity * 2;
        r->entry = realloc(r->entry,
                         sizeof(Laik_ReservationEntry) * r->capacity);
        if (!r->entry) {
            laik_panic("Out of memory allocating memory for Laik_Reservation");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    Laik_ReservationEntry* re = &(r->entry[r->count]);
    r->count++;

    re->p = p;
    re->mList = 0;
}

// free the memory space allocated in this reservation
void laik_reservation_free(Laik_Reservation* r)
{
    for(int i = 0; i < r->count; i++) {
        assert(r->entry[i].mList != 0);
        free(r->entry[i].mList);
        r->entry[i].mList = 0;
    }
    r->count = 0;

    // free memory space
    for(int i = 0; i < r->mappingCount; i++)
        freeMap(&(r->mapping[i]), r->data, r->data->stat);
    free(r->mapping);
    r->mappingCount = 0;
    r->mapping = 0;
}


// helpers for laik_reservation_alloc

// entry for a slice group
struct mygroup {
    int partIndex; // index in reservation specifying partitioning of group
    int partMapNo; // mapping number within partitioning
    int resMapNo;  // mapping number within reservation
    int tag;       // tag given by partitioner, used to identify same mappings
};

static
int mygroup_cmp(const void *p1, const void *p2)
{
    const struct mygroup* g1 = (const struct mygroup*) p1;
    const struct mygroup* g2 = (const struct mygroup*) p2;

    return g1->tag - g2->tag;
}


// allocate space for all partitionings registered in a reservation
void laik_reservation_alloc(Laik_Reservation* res)
{
    if (res->count == 0) {
        // nothing reserved, nothing to do
        return;
    }

    Laik_Data* data = res->data;

    // (1) detect how many different mappings (= slice groups) are needed
    //     in this process over all partitionings in the reservation.
    //     slice groups using same tag in partitionings go into same mapping.
    //     We do this by adding slice groups over all partitionings into a
    //     list, sort by tag and count the number of different tags used

    // (1a) calculate list length needed:
    //      number of my slice groups in all partitionings
    int groupCount = 0;
    for(int i = 0; i < res->count; i++) {
        Laik_Partitioning* p = res->entry[i].p;
        // this process must be part of all partitionings to reserve for
        assert(p->group->myid >= 0);
        laik_updateMyMapOffsets(p); // could be done always, not just lazy
        assert(p->myMapCount >= 0);
        assert(p->myMapOff != 0);
        groupCount += p->myMapCount;
    }

    // (1b) allocate list and add entries for slice groups to list
    struct mygroup *glist = malloc(groupCount * sizeof(struct mygroup));
    int gOff = 0;
    for(int i = 0; i < res->count; i++) {
        Laik_Partitioning* p = res->entry[i].p;
        for(int mapNo = 0; mapNo < p->myMapCount; mapNo++) {
            int off = p->myMapOff[mapNo];
            int tag = p->tslice[off].tag;
            // for reservation, tag >0 to specify partitioning relations
            // TODO: tag == 0 means to use heuristic, but not implemented yet
            //       but we are fine with just one slice group and tag 0
            if (p->myMapCount > 1)
                assert(tag > 0);
            glist[gOff].partIndex = i;
            glist[gOff].partMapNo = mapNo;
            glist[gOff].resMapNo = -1; // not calculated yet
            glist[gOff].tag = tag;
            gOff++;
        }
    }
    assert(gOff == groupCount);

    // (1c) sort list by tags and count number of different tags
    qsort(glist, groupCount, sizeof(struct mygroup), mygroup_cmp);
    int resMapNo = -1;
    int lastTag = -1;
    for(int i = 0; i < groupCount; i++) {
        if (glist[i].tag != lastTag) {
            lastTag = glist[i].tag;
            resMapNo++;
        }
        assert(resMapNo >= 0);
        glist[i].resMapNo = resMapNo;
    }
    int mCount = resMapNo + 1;
    res->mappingCount = mCount;

    // (2) allocate mapping descriptors, both for
    //     - combined descriptors for same tag in all partitionings, and
    //     - per-partitioning descriptors

    Laik_Mapping* mList = malloc(mCount * sizeof(Laik_Mapping));
    for(int i = 0; i < mCount; i++) {
        initMapping(&(mList[i]), res->data);
        mList[i].mapNo = i;
    }
    res->mapping = mList;

    for(int i = 0; i < res->count; i++) {
        Laik_Partitioning* p = res->entry[i].p;
        Laik_MappingList* mList = malloc(sizeof(Laik_MappingList) +
                                      p->myMapCount * sizeof(Laik_Mapping));
        mList->count = p->myMapCount;
        mList->res = res;
        res->entry[i].mList = mList;
        for(int i = 0; i < p->myMapCount; i++) {
            initMapping(&(mList->map[i]), res->data);
            mList->map[i].mapNo = i;
        }
    }

    // (3) link per-partitioning descriptor to corresponding combined one
    //     and determine required space for each mapping

    int dims = data->space->dims;
    for(int i = 0; i < groupCount; i++) {
        int idx = glist[i].partIndex;

        int partMapNo = glist[i].partMapNo;
        assert(partMapNo < res->entry[idx].mList->count);
        Laik_Mapping* pMap = &(res->entry[idx].mList->map[partMapNo]);

        int resMapNo = glist[i].resMapNo;
        assert(resMapNo < mCount);
        Laik_Mapping* rMap = &(mList[resMapNo]);

        assert(pMap->baseMapping == 0);
        pMap->baseMapping = rMap;

        // go over all slices in this slice group (same tag) and extend
        Laik_Partitioning* p = res->entry[idx].p;
        for(int o = p->myMapOff[partMapNo]; o < p->myMapOff[partMapNo+1]; o++) {
            assert(p->tslice[o].mapNo == partMapNo);
            assert(p->tslice[o].tag == glist[i].tag);
            assert(laik_slice_size(dims, &(p->tslice[o].s)) > 0);
            if (laik_slice_isEmpty(dims, &(pMap->requiredSlice)))
                pMap->requiredSlice = p->tslice[o].s;
            else
                laik_slice_expand(dims, &(pMap->requiredSlice), &(p->tslice[o].s));
        }

        // extend combined mapping descriptor by required size
        if (laik_slice_isEmpty(dims, &(rMap->requiredSlice)))
            rMap->requiredSlice = pMap->requiredSlice;
        else
            laik_slice_expand(dims, &(rMap->requiredSlice), &(pMap->requiredSlice));
    }

    free(glist);

    laik_log(2, "Allocated reservations for '%s'", data->name);

    // (4) set final sizes of base mappings, and do allocation
    for(int i = 0; i < mCount; i++) {
        Laik_Mapping* m = &(mList[i]);
        Laik_Slice* slc = &(m->requiredSlice);
        int count = laik_slice_size(dims, slc);
        assert(count > 0);

        m->count = count;
        m->size[0] = slc->to.i[0] - slc->from.i[0];
        m->size[1] = (dims > 1) ? (slc->to.i[1] - slc->from.i[1]) : 0;
        m->size[2] = (dims > 2) ? (slc->to.i[2] - slc->from.i[2]) : 0;

        laik_allocateMap(m, data->stat);

        if (laik_log_begin(2)) {
            laik_log_append(" map [%d] ", m->mapNo);
            laik_log_Slice(dims, &(m->allocatedSlice));
            laik_log_flush(0);
        }
    }

    // (5) set parameters for embedded mappings
    for(int r = 0; r < res->count; r++) {
        Laik_Partitioning* p = res->entry[r].p;
        laik_log(2, " part '%s':", p->name);
        for(int mapNo = 0; mapNo < p->myMapCount; mapNo++) {
            Laik_Mapping* m = &(res->entry[r].mList->map[mapNo]);

            m->allocatedSlice = m->baseMapping->requiredSlice;
            m->allocCount = m->baseMapping->count;

            Laik_Slice* slc = &(m->requiredSlice);
            m->count = laik_slice_size(dims, slc);
            m->size[0] = slc->to.i[0] - slc->from.i[0];
            m->size[1] = (dims > 1) ? (slc->to.i[1] - slc->from.i[1]) : 0;
            m->size[2] = (dims > 2) ? (slc->to.i[2] - slc->from.i[2]) : 0;

            initEmbeddedMapping(m, m->baseMapping);

            if (laik_log_begin(2)) {
                laik_log_append("  [%d] ", m->mapNo);
                laik_log_Slice(dims, &(m->requiredSlice));
                laik_log_flush(" in map [%d] with byte-off %llu",
                               m->baseMapping->mapNo,
                               (unsigned long long) (m->base - m->start));
            }
        }
    }
}

// execute a previously calculated transition on a data container
void laik_exec_transition(Laik_Data* d, Laik_Transition* t)
{
    if (laik_log_begin(1)) {
        laik_log_append("exec transition (");
        laik_log_DataFlow(t->fromFlow);
        laik_log_append("/'%s' => ", t->fromPartitioning ? t->fromPartitioning->name : "(none)");
        laik_log_DataFlow(t->toFlow);
        laik_log_append("/'%s') on data '%s' (in ",
                        t->toPartitioning ? t->toPartitioning->name : "(none)", d->name);
        laik_log_DataFlow(d->activeFlow);
        laik_log_flush("/'%s')",
                        d->activePartitioning ? d->activePartitioning->name : "(none)");
    }

    // we only can execute transtion if start state in transition is correct
    if ((d->activeFlow != t->fromFlow) ||
        (d->activePartitioning != t->fromPartitioning)) {
        laik_panic("laik_exec_transition starts in wrong phase!");
        exit(1);
    }

    Laik_MappingList* toList = prepareMaps(d, t->toPartitioning, 0);
    doTransition(d, t, d->activeMappings, toList);

    // set new mapping/partitioning active
    d->activePartitioning = t->toPartitioning;
    d->activeFlow = t->toFlow;
    d->activeMappings = toList;
}

// switch to new partitioning borders
// new flow is derived from previous flow when set to LAIK_DF_Previous
void laik_switchto_partitioning(Laik_Data* d,
                                Laik_Partitioning* toP, Laik_DataFlow toFlow)
{
    // calculate actions to be done for switching

    if (d->activePartitioning) {
        if (toP)
            assert(d->activePartitioning->group == toP->group);
    }
    else {
        if (!toP) {
            // nothing to switch from/to
            return;
        }
    }

    // if data is in an access phase, it must be consistent with partitioning
    Laik_AccessPhase* ap = d->activeAccessPhase;
    if (ap) {
        // active partitioning must have borders set
        assert(ap->hasValidPartitioning);
        assert(d->activePartitioning == ap->partitioning);
    }

    if (toFlow == LAIK_DF_Previous) {
        if (laik_do_copyout(d->activeFlow) || laik_is_reduction(d->activeFlow))
            toFlow = LAIK_DF_CopyIn | LAIK_DF_CopyOut;
        else
            toFlow = LAIK_DF_None;
    }

    Laik_MappingList* toList = prepareMaps(d, toP, 0);
    Laik_Transition* t = laik_calc_transition(d->space,
                                              d->activePartitioning,
                                              d->activeFlow,
                                              toP, toFlow);

    doTransition(d, t, d->activeMappings, toList);

    // set new mapping/partitioning active
    d->activePartitioning = toP;
    d->activeFlow = toFlow;
    d->activeMappings = toList;
}

// switch from active to another partitioning
void laik_switchto_phase(Laik_Data* d,
                         Laik_AccessPhase* toAP, Laik_DataFlow toFlow)
{
    if (d->space->inst->profiling->do_profiling)
        d->space->inst->profiling->timer_total = laik_wtime();

    // calculate borders with configured partitioner if borders not set
    if (toAP && (!toAP->hasValidPartitioning))
        laik_phase_run_partitioner(toAP);

    // calculate actions to be done for switching
    Laik_Partitioning *fromP = 0, *toP = 0;
    Laik_AccessPhase* fromAP = d->activeAccessPhase;
    if (fromAP) {
        // active partitioning must have borders set
        assert(fromAP->hasValidPartitioning);
        fromP = fromAP->partitioning;
    }
    if (toAP) {
        // new partitioning needs to be defined over same LAIK task group
        assert(toAP->hasValidPartitioning);
        toP = toAP->partitioning;
    }

    Laik_MappingList* toList = prepareMaps(d, toP, 0);
    Laik_Transition* t = laik_calc_transition(d->space,
                                              fromP, d->activeFlow,
                                              toP, toFlow);

    if (laik_log_begin(1)) {
        laik_log_append("switch access phase for data '%s':\n"
                        "  %s/",
                        d->name, fromAP ? fromAP->name : "(none)");
        laik_log_DataFlow(d->activeFlow);
        laik_log_append(" => %s/", toAP ? toAP->name : "(none)");
        laik_log_DataFlow(toFlow);
        laik_log_append(":\n    ");
        laik_log_Transition(t, true);
        laik_log_flush(0);
    }
#if 0 // don't pollute log level 2, should introduce more...
    else
        laik_log(2, "switch to '%s' (data '%s'): "
                 "%d local, %d init, %d send, %d recv, %d red",
                 d->name, toP ? toP->name : "(none)",
                 t->localCount, t->initCount,
                 t->sendCount, t->recvCount, t->redCount);
#endif

    doTransition(d, t, d->activeMappings, toList);

    if (d->activeAccessPhase) {
        laik_removeDataFromAccessPhase(d->activeAccessPhase, d);
        laik_free_accessphase(d->activeAccessPhase);
    }

    // set new mapping/partitioning active
    d->activeAccessPhase = toAP;
    d->activePartitioning = toP;
    d->activeFlow = toFlow;
    d->activeMappings = toList;
    if (toAP)
        laik_addDataForAccessPhase(toAP, d);

    if (d->space->inst->profiling->do_profiling)
        d->space->inst->profiling->time_total += laik_wtime() -
                                     d->space->inst->profiling->timer_total;
}

// switch to another data flow, keep partitioning
void laik_switchto_flow(Laik_Data* d, Laik_DataFlow toFlow)
{
    if (!d->activePartitioning) {
        // makes no sense without partitioning
        laik_panic("laik_switch_flow without active partitioning!");
    }
    laik_switchto_partitioning(d, d->activePartitioning, toFlow);
}


// get slice number <n> in own partition
Laik_TaskSlice* laik_data_slice(Laik_Data* d, int n)
{
    if (d->activePartitioning == 0) return 0;
    return laik_my_slice(d->activePartitioning, n);
}

Laik_AccessPhase* laik_switchto_new_phase(Laik_Data* d, Laik_Group* g,
                                          Laik_Partitioner* pr,
                                          Laik_DataFlow flow)
{
    if (laik_myid(g) < 0) return 0;

    Laik_AccessPhase* ap;
    ap = laik_new_accessphase(g, d->space, pr, 0);

    laik_log(1, "switch data '%s' to new access phase '%s'",
             d->name, ap->name);

    laik_switchto_phase(d, ap, flow);
    return ap;
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
    laik_switchto_phase(d, 0, LAIK_DF_None);
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
    if (!l) {
        laik_panic("Out of memory allocating Laik_Layout object");
        exit(1); // not actually needed, laik_panic never returns
    }

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
int64_t laik_offset(Laik_Index* idx, Laik_Layout* l)
{
    assert(l);

    // TODO: only default layout with order 1/2/3
    assert(l->stride[0] == 1);
    if (l->dims > 1) {
        assert(l->stride[0] <= l->stride[1]);
        if (l->dims > 2)
            assert(l->stride[1] <= l->stride[2]);
    }

    int64_t off = idx->i[0];
    if (l->dims > 1) {
        off += idx->i[1] * l->stride[1];
        if (l->dims > 2) {
            off += idx->i[2] * l->stride[2];
        }
    }
    return off;
}

// pack/unpack routines for default layout
int laik_pack_def(const Laik_Mapping* m, const Laik_Slice* s, Laik_Index* idx,
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

    int64_t i0, i1, i2, from0, from1, to0, to1, to2, count;
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
    int64_t skip0 = m->layout->stride[1] - (to0 - from0);
    // elements to skip after to1 reached
    int64_t skip1 = m->layout->stride[2] - m->layout->stride[1] * (to1 - from1);

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

int laik_unpack_def(const Laik_Mapping* m, const Laik_Slice* s, Laik_Index* idx,
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

    int64_t i0, i1, i2, from0, from1, to0, to1, to2, count;
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
    // we must have an active partitioning
    assert(d->activePartitioning);
    Laik_Group* g = d->activePartitioning->group;
    if (g->myid == -1) {
        laik_log(LAIK_LL_Error,
                 "laik_map called for data '%s' defined on process group %d.\n"
                 "This task is NOT part of the group. Fix your application!\n"
                 "(may crash now if returned address is dereferenced)",
                 d->name, g->gid);
    }

    if (!d->activeMappings) {
        // lazy allocation
        d->activeMappings = prepareMaps(d, d->activePartitioning, layout);
        if (d->activeMappings == 0)
            return 0;
    }

    if (n >= d->activeMappings->count)
        return 0;

    Laik_Mapping* m = &(d->activeMappings->map[n]);
    // ensure the mapping is backed by real memory
    if (m->base == 0)
        laik_allocateMap(m, d->stat);

    return m;
}

// similar to laik_map, but force a default mapping
Laik_Mapping* laik_map_def(Laik_Data* d, int n, void** base, uint64_t* count)
{
    static Laik_Layout* def_layout = 0;
    if (!def_layout)
        def_layout = laik_new_layout(LAIK_LT_Default);

    Laik_Mapping* m = laik_map(d, n, def_layout);

    if (base) *base = m ? m->base : 0;
    if (count) *count = m ? m->count : 0;
    return m;
}


// similar to laik_map, but force a default mapping with only 1 slice
Laik_Mapping* laik_map_def1(Laik_Data* d, void** base, uint64_t* count)
{
    static Laik_Layout* def_layout = 0;
    if (!def_layout)
        def_layout = laik_new_layout(LAIK_LT_Default1Slice);

    Laik_Mapping* m = laik_map(d, 0, def_layout);
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


Laik_Mapping* laik_global2local_1d(Laik_Data* d, int64_t gidx, uint64_t* lidx)
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

Laik_Mapping* laik_global2maplocal_1d(Laik_Data* d, int64_t gidx,
                                      int* mapNo, uint64_t* lidx)
{
    assert(d->space->dims == 1);
    if (!d->activeMappings) return 0;
    for(int i = 0; i < d->activeMappings->count; i++) {
        Laik_Mapping* m = &(d->activeMappings->map[i]);

        if (gidx < m->requiredSlice.from.i[0]) continue;
        if (gidx >= m->requiredSlice.to.i[0]) continue;

        if (lidx) *lidx = gidx - m->requiredSlice.from.i[0];
        if (mapNo) *mapNo = i;
        return m;
    }
    return 0;
}


int64_t laik_local2global_1d(Laik_Data* d, uint64_t off)
{
    assert(d->space->dims == 1);
    assert(d->activeMappings && (d->activeMappings->count == 1));

    // TODO: check all mappings, not just map 0
    Laik_Mapping* m = &(d->activeMappings->map[0]);
    assert(off < m->count);

    // TODO: take layout into account
    return m->requiredSlice.from.i[0] + off;
}


int64_t laik_maplocal2global_1d(Laik_Data* d, int mapNo, uint64_t li)
{
    assert(d->space->dims == 1);
    assert(d->activeMappings);

    // TODO: check all mappings, not just map 0
    Laik_Mapping* m = &(d->activeMappings->map[mapNo]);
    assert(li < m->count);

    // TODO: take layout into account
    return m->requiredSlice.from.i[0] + li;
}

int laik_map_get_mapNo(const Laik_Mapping* map)
{
    assert(map);

    return map->mapNo;
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
    if (!a) {
        laik_panic("Out of memory allocating Laik_Allocator object");
        exit(1); // not actually needed, laik_panic never returns
    }

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

