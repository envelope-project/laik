/*
 * This file is part of the LAIK library.
 * Copyright (c) 2017, 2018 Josef Weidendorfer <Josef.Weidendorfer@gmx.de>
 *
 * LAIK is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 3 or later.
 *
 * LAIK is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "laik-internal.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// forward decl
unsigned int laik_pack_def(const Laik_Mapping* m, const Laik_Slice* s,
                           Laik_Index* idx, char* buf, unsigned int size);
unsigned int laik_unpack_def(const Laik_Mapping* m, const Laik_Slice* s,
                             Laik_Index* idx, char* buf, unsigned int size);

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
    ss->mallocedBytes      = 0;
    ss->freedBytes         = 0;
    ss->currAllocedBytes   = 0;
    ss->maxAllocedBytes    = 0;
    ss->initedBytes        = 0;
    ss->copiedBytes        = 0;

    ss->transitionCount = 0;
    ss->msgSendCount = 0;
    ss->msgRecvCount = 0;
    ss->msgReduceCount = 0;
    ss->msgAsyncSendCount = 0;
    ss->msgAsyncRecvCount = 0;
    ss->elemSendCount = 0;
    ss->elemRecvCount = 0;
    ss->elemReduceCount = 0;
    ss->byteSendCount = 0;
    ss->byteRecvCount = 0;
    ss->byteReduceCount = 0;
    ss->initOpCount = 0;
    ss->reduceOpCount = 0;
    ss->byteBufCopyCount = 0;

    return ss;
}

void laik_addSwitchStat(Laik_SwitchStat* target, Laik_SwitchStat* src)
{
    target->switches           += src->switches           ;
    target->switches_noactions += src->switches_noactions ;
    target->mallocCount        += src->mallocCount        ;
    target->freeCount          += src->freeCount          ;
    target->mallocedBytes      += src->mallocedBytes      ;
    target->freedBytes         += src->freedBytes         ;
    target->maxAllocedBytes    += src->maxAllocedBytes    ;
    target->initedBytes        += src->initedBytes        ;
    target->copiedBytes        += src->copiedBytes        ;

    target->transitionCount    += src->transitionCount;
    target->msgSendCount       += src->msgSendCount;
    target->msgRecvCount       += src->msgRecvCount;
    target->msgReduceCount     += src->msgReduceCount;
    target->msgAsyncSendCount  += src->msgAsyncSendCount;
    target->msgAsyncRecvCount  += src->msgAsyncRecvCount;
    target->elemSendCount      += src->elemSendCount;
    target->elemRecvCount      += src->elemRecvCount;
    target->elemReduceCount    += src->elemReduceCount;
    target->byteSendCount      += src->byteSendCount;
    target->byteRecvCount      += src->byteRecvCount;
    target->byteReduceCount    += src->byteReduceCount;
    target->initOpCount        += src->initOpCount;
    target->reduceOpCount      += src->reduceOpCount;
    target->byteBufCopyCount   += src->byteBufCopyCount;
}

void laik_switchstat_addASeq(Laik_SwitchStat* target, Laik_ActionSeq* as)
{
    assert(as->transitionCount > 0);

    target->transitionCount    += as->transitionCount;
    target->msgSendCount       += as->msgSendCount;
    target->msgRecvCount       += as->msgRecvCount;
    target->msgReduceCount     += as->msgReduceCount;
    target->msgAsyncSendCount  += as->msgAsyncSendCount;
    target->msgAsyncRecvCount  += as->msgAsyncRecvCount;
    target->elemSendCount      += as->elemSendCount;
    target->elemRecvCount      += as->elemRecvCount;
    target->elemReduceCount    += as->elemReduceCount;
    target->byteSendCount      += as->byteSendCount;
    target->byteRecvCount      += as->byteRecvCount;
    target->byteReduceCount    += as->byteReduceCount;
    target->initOpCount        += as->initOpCount;
    target->reduceOpCount      += as->reduceOpCount;
    target->byteBufCopyCount   += as->byteBufCopyCount;
}

void laik_switchstat_malloc(Laik_SwitchStat* ss, uint64_t bytes)
{
    if (!ss) return;

    ss->mallocCount++;
    ss->mallocedBytes += bytes;

    ss->currAllocedBytes += bytes;
    if (ss->currAllocedBytes > ss->maxAllocedBytes)
        ss->maxAllocedBytes = ss->currAllocedBytes;
}

void laik_switchstat_free(Laik_SwitchStat* ss, uint64_t bytes)
{
    if (!ss) return;

    ss->freeCount++;
    ss->freedBytes += bytes;

    ss->currAllocedBytes -= bytes;
}

//-------------------------------------------------------------------

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
    d->activeMappings = 0;
    d->allocator = 0; // default: malloc/free
    d->stat = laik_newSwitchStat();

    d->activeReservation = 0;

    laik_log(1, "new data '%s':\n"
             "  type '%s' (elemsize %d), space '%s' (%lu elems, %.3f MB)\n",
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

static
void initMapping(Laik_Mapping* m, Laik_Data* d)
{
    m->data = d;
    m->mapNo = -1;
    m->reusedFor = -1;

    // set requiredSlice to invalid
    m->requiredSlice.space = 0;

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
                laik_log(1, "prepareMaps: use reservation for data '%s' (partitioning '%s')",
                         d->name, p->name);
                return ml;
            }
        }
    }

    // we need a slice array with own slices
    Laik_SliceArray* sa = laik_partitioning_myslices(p);
    assert(sa != 0); // TODO: API user error

    // number of local slices
    int sn = sa->off[myid+1] - sa->off[myid];

    // number of maps
    int n = 0;
    if (sn > 0)
        n = sa->tslice[sa->off[myid+1] - 1].mapNo + 1;

    Laik_MappingList* ml;
    ml = malloc(sizeof(Laik_MappingList) + n * sizeof(Laik_Mapping));
    if (!ml) {
        laik_panic("Out of memory allocating Laik_MappingList object");
        exit(1); // not actually needed, laik_panic never returns
    }
    ml->res = 0; // not part of a reservation
    ml->count = n;

    laik_log(1, "prepareMaps: %d maps for data '%s' (partitioning '%s')",
             n, d->name, p->name);

    if (n == 0) return ml;

    unsigned int firstOff, lastOff;
    int mapNo = 0;
    for(unsigned int o = sa->off[myid]; o < sa->off[myid+1]; o++, mapNo++) {
        assert(mapNo == sa->tslice[o].mapNo);
        Laik_Mapping* m = &(ml->map[mapNo]);
        initMapping(m, d);
        m->mapNo = mapNo;
        // remember layout request as hint
        m->layout = l;

        // required space
        Laik_Slice slc = sa->tslice[o].s;
        firstOff = o;
        while((o+1 < sa->off[myid+1]) && (sa->tslice[o+1].mapNo == mapNo)) {
            o++;
            laik_slice_expand(&slc, &(sa->tslice[o].s));
        }
        lastOff = o;
        m->requiredSlice = slc;
        m->count = laik_slice_size(&slc);
        m->size[0] = slc.to.i[0] - slc.from.i[0];
        m->size[1] = (dims > 1) ? (slc.to.i[1] - slc.from.i[1]) : 0;
        m->size[2] = (dims > 2) ? (slc.to.i[2] - slc.from.i[2]) : 0;

        if (laik_log_begin(1)) {
            laik_log_append("    mapNo %d: req.slice ", mapNo);
            laik_log_Slice(&slc);
            laik_log_flush(", tslices %d - %d, count %d, elemsize %d\n",
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
        laik_log(1, "free map for data '%s' mapNo %d (capacity %llu, base %p, start %p)\n",
                 d->name, m->mapNo,
                 (unsigned long long) m->capacity, (void*) m->base, (void*) m->start);

        // concrete, fixed layouts are only used once: free
        if (m->layout && m->layout->isFixed) {
            free(m->layout);
            m->layout = 0;
        }

        laik_switchstat_free(ss, m->capacity);

        // TODO: different policies
        if ((!d->allocator) || (!d->allocator->free))
            free(m->start);
        else
            (d->allocator->free)(d, m->start);

        m->base = 0;
        m->start = 0;
    }
    else
        laik_log(1, "free map for data '%s' mapNo %d: nothing to do (reused for %d)\n",
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
    laik_switchstat_malloc(ss, m->capacity);

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

    laik_log(1, "allocateMap: for '%s'/%d: %llu x %d (%llu B) at %p"
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

        assert(toMap->base);

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
    assert(data == fromMap->data);

    assert(laik_slice_within_slice(&(toMap->requiredSlice),
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

    for(int i = 0; i < toList->count; i++) {
        Laik_Mapping* toMap = &(toList->map[i]);
        for(int sNo = 0; sNo < fromList->count; sNo++) {
            Laik_Mapping* fromMap = &(fromList->map[sNo]);
            if (fromMap->base == 0) continue;
            if (fromMap->reusedFor >= 0) continue; // only reuse once

            // does index range fit into old?
            if (!laik_slice_within_slice(&(toMap->requiredSlice),
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
                laik_log_Slice(&(toMap->requiredSlice));
                laik_log_append(" (in ");
                laik_log_Slice(&(toMap->allocatedSlice));
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

        assert(toMap->base);

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
void allocateMappings(Laik_MappingList* toList, Laik_SwitchStat* ss)
{
    for(int i = 0; i < toList->count; i++) {
        Laik_Mapping* map = &(toList->map[i]);
        if (map->base) continue;

        // with reservation, all allocations must have happened before
        assert(toList->res == 0);

        laik_allocateMap(map, ss);
    }
}

static
Laik_ActionSeq* createTransASeq(Laik_Data* d, Laik_Transition* t,
                                Laik_MappingList* fromList,
                                Laik_MappingList* toList)
{
    // never create a sequence with an invalid transition
    assert(t != 0);

    // create the action sequence for requested transition
    Laik_ActionSeq* as = laik_aseq_new(d->space->inst);
    int tid = laik_aseq_addTContext(as, d, t, fromList, toList);
    laik_aseq_addTExec(as, tid);
    laik_aseq_activateNewActions(as);

    return as;
}


static
void doTransition(Laik_Data* d, Laik_Transition* t, Laik_ActionSeq* as,
                  Laik_MappingList* fromList, Laik_MappingList* toList)
{
    if (d->stat) {
        d->stat->switches++;
        if (!t || (t->actionCount == 0))
            d->stat->switches_noactions++;
    }

    if (t == 0) {
        // no transition to exec, just free old mappings
        freeMaps(fromList, d->stat);
        return;
    }

    // be careful when reusing mappings:
    // the backend wants to send/receive data in arbitrary order
    // (to avoid deadlocks), but it never should overwrite data
    // before it gets sent by data already received.
    // thus it is bad to reuse a mapping for different index ranges.
    // but reusing mappings such that same indexes go to same address
    // is fine.
    checkMapReuse(toList, fromList);

    // allocate space for mappings for which reuse is not possible
    allocateMappings(toList, d->stat);

    bool doASeqCleanup = false;
    if (as) {
        // we are given a prepared action sequence:
        // check that <as> has actions for given transition
        Laik_TransitionContext* tc = as->context[0];
        assert(tc->data == d);
        assert(tc->transition == t);
        // provide current mappings to context
        tc->toList = toList;
        tc->fromList = fromList;
        // if sequence was prepared with mappings, they must be the same
        if (tc->prepFromList) assert(tc->prepFromList == fromList);
        if (tc->prepToList) assert(tc->prepToList == toList);
    }
    else {
        // create the action sequence for requested transition on the fly
        as = createTransASeq(d, t, fromList, toList);
#if 1
        const Laik_Backend* backend = d->space->inst->backend;
        if (backend->prepare)
            (backend->prepare)(as);
        else {
            // for statistics: usually called in backend prepare function
            laik_aseq_calc_stats(as);
        }
#endif
        doASeqCleanup = true;
    }

    if (t->sendCount + t->recvCount + t->redCount > 0) {
        // let backend do send/recv/reduce actions

        Laik_Instance* inst = d->space->inst;
        if (inst->profiling->do_profiling)
            inst->profiling->timer_backend = laik_wtime();

        (inst->backend->exec)(as);

        if (inst->profiling->do_profiling)
            inst->profiling->time_backend += laik_wtime() - inst->profiling->timer_backend;

    }

    if (d->stat)
        laik_switchstat_addASeq(d->stat, as);

    if (doASeqCleanup)
        laik_aseq_free(as);

    // local copy actions
    if (t->localCount > 0)
        copyMaps(t, toList, fromList, d->stat);

    // local init action
    if (t->initCount > 0)
        initMaps(t, toList, fromList, d->stat);

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

static int res_id = 0;

// create a reservation object for <data>
Laik_Reservation* laik_reservation_new(Laik_Data* d)
{
    Laik_Reservation* r = malloc(sizeof(Laik_Reservation));
    if (!r) {
        laik_panic("Out of memory allocating Laik_Reservation object");
        exit(1); // not actually needed, laik_panic never returns
    }

    r->id = res_id++;
    r->name = strdup("res-0     ");
    sprintf(r->name, "res-%d", r->id);

    r->data = d;
    r->count = 0;
    r->capacity = 0;
    r->entry = 0;
    r->mappingCount = 0;
    r->mapping = 0;

    laik_log(1, "new reservation '%s' for data '%s'", r->name, d->name);

    return r;
}

// register a partitioning for inclusion in a reservation:
// this will include space required for this partitioning on allocation
void laik_reservation_add(Laik_Reservation* r, Laik_Partitioning* p)
{
    if (p->group->myid < 0) return;

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

    laik_log(1, "reservation '%s' (data '%s'): added partition '%s'",
             r->name, r->data->name, p->name);
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
    uint64_t bytesFreed = 0;
    for(int i = 0; i < r->mappingCount; i++) {
        Laik_Mapping* m = &(r->mapping[i]);
        bytesFreed += m->capacity;
        freeMap(m, r->data, r->data->stat);
    }
    free(r->mapping);
    r->mappingCount = 0;
    r->mapping = 0;

    laik_log(1, "reservation '%s' (data '%s'): freed %llu bytes\n",
                 r->name, r->data->name, (unsigned long long) bytesFreed);
}

// get mapping list allocated in a reservation for a given partitioning
Laik_MappingList* laik_reservation_getMList(Laik_Reservation* r,
                                            Laik_Partitioning* p)
{
    for(int i = 0; i < r->count; i++) {
        if ((r->entry[i].p == p) && (r->entry[i].mList != 0)) {
            Laik_MappingList* ml = r->entry[i].mList;
            assert(ml->res == r);
            return ml;
        }
    }
    return 0;
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

    Laik_Group* g = 0;
    for(int i = 0; i < res->count; i++) {
        Laik_Partitioning* p = res->entry[i].p;
        if (!g) g = p->group;
        else {
            // make sure all partitionings refer to the same task group
            assert(p->group == g);
        }
    }
    // if this process is not in the task group, just do not reserve anything
    if (g->myid < 0) return;

    // (1) detect how many different mappings (= slice groups) are needed
    //     in this process over all partitionings in the reservation.
    //     slice groups using same tag in partitionings go into same mapping.
    //     We do this by adding slice groups over all partitionings into a
    //     list, sort by tag and count the number of different tags used

    // (1a) calculate list length needed:
    //      number of my slice groups in all partitionings
    unsigned int groupCount = 0;
    for(int i = 0; i < res->count; i++) {
        Laik_Partitioning* p = res->entry[i].p;
        // this process must be part of all partitionings to reserve for
        assert(p->group->myid >= 0);
        Laik_SliceArray* sa = laik_partitioning_myslices(p);
        laik_updateMapOffsets(sa, p->group->myid); // could be done always, not just lazy
        assert(sa->map_tid == p->group->myid);
        if (sa->map_count > 0) assert(sa->map_off != 0);
        groupCount += sa->map_count;
    }

    // (1b) allocate list and add entries for slice groups to list
    struct mygroup *glist = malloc(groupCount * sizeof(struct mygroup));
    unsigned int gOff = 0;
    for(int i = 0; i < res->count; i++) {
        Laik_Partitioning* p = res->entry[i].p;
        Laik_SliceArray* sa = laik_partitioning_myslices(p);
        for(int mapNo = 0; mapNo < (int) sa->map_count; mapNo++) {
            unsigned int off = sa->map_off[mapNo];
            int tag = sa->tslice[off].tag;
            // for reservation, tag >0 to specify partitioning relations
            // TODO: tag == 0 means to use heuristic, but not implemented yet
            //       but we are fine with just one slice group and tag 0
            if (sa->map_count > 1)
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
    for(unsigned int i = 0; i < groupCount; i++) {
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
        Laik_SliceArray* sa = laik_partitioning_myslices(p);
        Laik_MappingList* mList = malloc(sizeof(Laik_MappingList) +
                                         sa->map_count * sizeof(Laik_Mapping));
        mList->count = (int) sa->map_count;
        mList->res = res;
        res->entry[i].mList = mList;
        for(unsigned int i = 0; i < sa->map_count; i++) {
            initMapping(&(mList->map[i]), res->data);
            mList->map[i].mapNo = (int) i;
        }
    }

    // (3) link per-partitioning descriptor to corresponding combined one
    //     and determine required space for each mapping

    int dims = data->space->dims;
    for(unsigned int i = 0; i < groupCount; i++) {
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
        Laik_SliceArray* sa = laik_partitioning_myslices(p);
        for(unsigned int o = sa->map_off[partMapNo]; o < sa->map_off[partMapNo+1]; o++) {
            assert(sa->tslice[o].s.space != 0);
            assert(sa->tslice[o].mapNo == partMapNo);
            assert(sa->tslice[o].tag == glist[i].tag);
            assert(laik_slice_size(&(sa->tslice[o].s)) > 0);
            if (laik_slice_isEmpty(&(pMap->requiredSlice)))
                pMap->requiredSlice = sa->tslice[o].s;
            else
                laik_slice_expand(&(pMap->requiredSlice), &(sa->tslice[o].s));
        }

        // extend combined mapping descriptor by required size
        assert(pMap->requiredSlice.space == data->space);
        if (laik_slice_isEmpty(&(rMap->requiredSlice)))
            rMap->requiredSlice = pMap->requiredSlice;
        else
            laik_slice_expand(&(rMap->requiredSlice), &(pMap->requiredSlice));
    }

    free(glist);

    laik_log(1, "reservation '%s': do allocation for '%s'", res->name, data->name);

    // (4) set final sizes of base mappings, and do allocation
    uint64_t total = 0;
    for(int i = 0; i < mCount; i++) {
        Laik_Mapping* m = &(mList[i]);
        Laik_Slice* slc = &(m->requiredSlice);
        uint64_t count = laik_slice_size(slc);
        assert(count > 0);
        total += count;

        m->count = count;
        m->size[0] = slc->to.i[0] - slc->from.i[0];
        m->size[1] = (dims > 1) ? (slc->to.i[1] - slc->from.i[1]) : 0;
        m->size[2] = (dims > 2) ? (slc->to.i[2] - slc->from.i[2]) : 0;

        laik_allocateMap(m, data->stat);

        if (laik_log_begin(1)) {
            laik_log_append(" map [%d] ", m->mapNo);
            laik_log_Slice(&(m->allocatedSlice));
            laik_log_flush(0);
        }
    }

    laik_log(2, "Alloc reservations for '%s': %.3f MB",
             data->name, 0.000001 * (total * data->elemsize));

    // (5) set parameters for embedded mappings
    for(int r = 0; r < res->count; r++) {
        Laik_Partitioning* p = res->entry[r].p;
        Laik_SliceArray* sa = laik_partitioning_myslices(p);
        laik_log(1, " part '%s':", p->name);
        for(unsigned int mapNo = 0; mapNo < sa->map_count; mapNo++) {
            Laik_Mapping* m = &(res->entry[r].mList->map[mapNo]);

            m->allocatedSlice = m->baseMapping->requiredSlice;
            m->allocCount = m->baseMapping->count;

            Laik_Slice* slc = &(m->requiredSlice);
            m->count = laik_slice_size(slc);
            m->size[0] = slc->to.i[0] - slc->from.i[0];
            m->size[1] = (dims > 1) ? (slc->to.i[1] - slc->from.i[1]) : 0;
            m->size[2] = (dims > 2) ? (slc->to.i[2] - slc->from.i[2]) : 0;

            initEmbeddedMapping(m, m->baseMapping);

            if (laik_log_begin(1)) {
                laik_log_append("  [%d] ", m->mapNo);
                laik_log_Slice(&(m->requiredSlice));
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
        laik_log_append("exec transition ");
        laik_log_Transition(t, false);
        laik_log_flush(" on data '%s'", d->name);
    }

    // we only can execute transtion if start state in transition is correct
    if (d->activePartitioning != t->fromPartitioning) {
        laik_panic("laik_exec_transition starts in wrong partitioning!");
        exit(1);
    }

    Laik_MappingList* toList = prepareMaps(d, t->toPartitioning, 0);
    doTransition(d, t, 0, d->activeMappings, toList);

    // set new mapping/partitioning active
    d->activePartitioning = t->toPartitioning;
    d->activeMappings = toList;
}

Laik_ActionSeq* laik_calc_actions(Laik_Data* d,
                                  Laik_Transition* t,
                                  Laik_Reservation* fromRes,
                                  Laik_Reservation* toRes)
{
    // never create a sequence with an invalid transition
    if (t == 0) return 0;

    Laik_MappingList* fromList = 0;
    Laik_MappingList* toList = 0;
    if (fromRes)
        fromList = laik_reservation_getMList(fromRes, t->fromPartitioning);
    if (toRes)
        toList = laik_reservation_getMList(toRes, t->toPartitioning);


    Laik_ActionSeq* as = createTransASeq(d, t, fromList, toList);
    const Laik_Backend* backend = d->space->inst->backend;
    if (backend->prepare) {
        (backend->prepare)(as);

        // remember mappings at prepare time
        Laik_TransitionContext* tc = as->context[0];
        tc->prepFromList = fromList;
        tc->prepToList = toList;
    }
    else {
        // for statistics: usually called in backend prepare function
        laik_aseq_calc_stats(as);
    }

    if (laik_log_begin(2)) {
        laik_log_append("calculated ");
        laik_log_ActionSeq(as, laik_log_shown(1));
        laik_log_flush(0);
    }

    return as;
}

// execute a previously calculated transition on a data container
void laik_exec_actions(Laik_ActionSeq* as)
{
    Laik_TransitionContext* tc = as->context[0];
    Laik_Transition* t = tc->transition;
    Laik_Data* d = tc->data;

    if (laik_log_begin(1)) {
        laik_log_append("exec action seq '%s' for transition ", as->name);
        laik_log_Transition(t, false);
        laik_log_flush(" on data '%s'", d->name);
    }

    // we only can execute transtion if start state in transition is correct
    if (d->activePartitioning != t->fromPartitioning) {
        laik_panic("laik_exec_actions starts in wrong partitioning!");
        exit(1);
    }

    Laik_MappingList* toList = prepareMaps(d, t->toPartitioning, 0);

    if (tc->prepFromList && (tc->prepFromList != d->activeMappings)) {
        laik_panic("laik_exec_actions: start mappings mismatch!");
        exit(1);
    }
    if (tc->prepToList && (tc->prepToList != toList)) {
        laik_panic("laik_exec_actions: end mappings mismatch!");
        exit(1);
    }

    // only execute by backend which optimized the sequence
    if (as->backend)
        assert(as->backend == d->space->inst->backend);

    doTransition(d, t, as, d->activeMappings, toList);

    // set new mapping/partitioning active
    d->activePartitioning = t->toPartitioning;
    d->activeMappings = toList;
}


// switch to given partitioning
void laik_switchto_partitioning(Laik_Data* d,
                                Laik_Partitioning* toP, Laik_DataFlow flow,
                                Laik_ReductionOperation redOp)
{
    // calculate actions to be done for switching

    Laik_Group* toGroup = toP ? toP->group : 0;
    if (d->activePartitioning) {
        if (toP && (d->activePartitioning->group != toP->group)) {
            // to a partitioning based on another group? migrate to old first
            laik_partitioning_migrate(toP, d->activePartitioning->group);
        }
    }
    else {
        if (!toP) {
            // nothing to switch from/to
            return;
        }
    }

    Laik_MappingList* toList = prepareMaps(d, toP, 0);
    Laik_Transition* t = do_calc_transition(d->space,
                                            d->activePartitioning, toP,
                                            flow, redOp);

    doTransition(d, t, 0, d->activeMappings, toList);

    // if we migrated "toP" to old group before, migrate back to new
    if (toGroup != toP->group)
        laik_partitioning_migrate(toP, toGroup);

    // set new mapping/partitioning active
    d->activePartitioning = toP;
    d->activeMappings = toList;
}


// switch to another data flow, keep partitioning
void laik_switchto_flow(Laik_Data* d,
                        Laik_DataFlow flow, Laik_ReductionOperation redOp)
{
    if (!d->activePartitioning) {
        // makes no sense without partitioning
        laik_panic("laik_switch_flow without active partitioning!");
    }
    laik_switchto_partitioning(d, d->activePartitioning, flow, redOp);
}


// get slice number <n> in own partition
Laik_TaskSlice* laik_data_slice(Laik_Data* d, int n)
{
    if (d->activePartitioning == 0) return 0;
    return laik_my_slice(d->activePartitioning, n);
}

Laik_Partitioning* laik_switchto_new_partitioning(Laik_Data* d, Laik_Group* g,
                                                  Laik_Partitioner* pr,
                                                  Laik_DataFlow flow,
                                                  Laik_ReductionOperation redOp)
{
    if (laik_myid(g) < 0) return 0;

    Laik_Partitioning* p;
    p = laik_new_partitioning(pr, g, d->space, 0);

    laik_log(1, "switch data '%s' to new partitioning '%s'",
             d->name, p->name);

    laik_switchto_partitioning(d, p, flow, redOp);
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
unsigned int laik_pack_def(const Laik_Mapping* m, const Laik_Slice* s,
                           Laik_Index* idx, char* buf, unsigned int size)
{
    unsigned int elemsize = m->data->elemsize;
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
    assert(laik_slice_within_slice(s, &(m->requiredSlice)));

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

        laik_log_append("        packing '%s', size (", m->data->name);
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
        laik_index_init(&idx2, i0, i1, i2);

        laik_log_append("        packed '%s': end (", m->data->name);
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

unsigned int laik_unpack_def(const Laik_Mapping* m, const Laik_Slice* s,
                             Laik_Index* idx, char* buf, unsigned int size)
{
    unsigned int elemsize = m->data->elemsize;
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
    assert(laik_slice_within_slice(s, &(m->requiredSlice)));

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

        laik_log_append("        unpacking '%s', size (", m->data->name);
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

#ifdef DEBUG_UNPACK
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
        laik_index_init(&idx2, i0, i1, i2);

        laik_log_append("        unpacked '%s': end (", m->data->name);
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

    if ((n<0) || (n >= d->activeMappings->count))
        return 0;

    Laik_Mapping* m = &(d->activeMappings->map[n]);
    // space always should be allocated
    assert(m->base);

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
        if (base) *base = 0;
        if (xsize) *xsize = 0;
        if (ysize) *ysize = 0;
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
        if (base) *base = 0;
        if (xsize) *xsize = 0;
        if (ysize) *ysize = 0;
        if (zsize) *zsize = 0;
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
    // not found: set mapNo to invalid -1
    if (mapNo) *mapNo = -1;
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

