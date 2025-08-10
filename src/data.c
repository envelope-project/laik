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

// for string.h to declare strdup
#define __STDC_WANT_LIB_EXT2__ 1

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


// provided allocators
Laik_Allocator *laik_allocator_def = 0;


// initialize the LAIK data module, called from laik_new_instance
void laik_data_init()
{
    laik_type_init();

    // default allocator used by containers
    laik_allocator_def = laik_new_allocator_def();
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
    assert(laik_allocator_def);
    d->allocator = laik_allocator_def; // malloc/free + reuse if possible
    d->layout_factory = laik_new_layout_lex; // by default, use lex layouts
    d->stat = laik_newSwitchStat();

    d->activeReservation = 0;
    d->map0_base = 0;
    d->map0_size = 0;

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

// change layout factory to use for generating mapping layouts
void laik_data_set_layout_factory(Laik_Data* d, laik_layout_factory_t lf)
{
    d->layout_factory = lf;
}

static
void initMapping(Laik_Mapping* m, Laik_Data* d)
{
    m->data = d;
    m->mapNo = -1;
    m->reusedFor = -1;

    // mark requiredRange to be invalid
    m->requiredRange.space = 0;
    m->layout = 0;
    m->layoutSection = 0;
    m->count = 0;

    // not backed by memory yet
    m->capacity = 0;
    m->start = 0;
    m->base = 0;

    // use default allocater of container to allocate memory
    m->allocator = d->allocator;

    // not embedded in another mapping
    m->baseMapping = 0;
}

// create mapping descriptors for <n> maps for data container <d>
// resulting mappings are not backed by memory (yet)
Laik_MappingList* laik_mappinglist_new(Laik_Data* d, int n, Laik_Layout* l)
{
    Laik_MappingList* ml;
    ml = malloc(sizeof(Laik_MappingList) + n * sizeof(Laik_Mapping));
    if (!ml) {
        laik_panic("Out of memory allocating Laik_MappingList object");
        exit(1); // not actually needed, laik_panic never returns
    }
    ml->res = 0;
    ml->count = n;
    ml->layout = l;

    for(int mapNo = 0; mapNo < n; mapNo++) {
        Laik_Mapping* m = &(ml->map[mapNo]);
        initMapping(m, d);
        m->mapNo = mapNo;
    }

    return ml;
}

// helper for prepareMaps
// alloc list of ranges required for mappings of a range list
static
Laik_Range* coveringRanges(int n, Laik_RangeList* list, int myid)
{
    if (n == 0) return 0;
    Laik_Range* ranges = (Laik_Range*) malloc(n * sizeof(Laik_Range));

    laik_log(1, "coveringRanges: %d maps", n);

    int mapNo = 0;
    for(unsigned int o = list->off[myid]; o < list->off[myid+1]; o++, mapNo++) {
        unsigned int firstOff = o;
        assert(mapNo == list->trange[o].mapNo);
        Laik_Range* range = &(ranges[mapNo]);

        // range covering all task ranges for a given map number
        *range = list->trange[o].range;
        while((o+1 < list->off[myid+1]) && (list->trange[o+1].mapNo == mapNo)) {
            o++;
            laik_range_expand(range, &(list->trange[o].range));
        }

        if (laik_log_begin(1)) {
            laik_log_append("    mapNo %d: covering range ", mapNo);
            laik_log_Range(range);
            laik_log_flush(", task ranges %d - %d\n", firstOff, o);
        }
    }
    return ranges;
}

// forward decl
static void laik_map_set_allocation(Laik_Mapping*, char*, uint64_t, Laik_Allocator*);

static
Laik_MappingList* prepareMaps(Laik_Data* d, Laik_Partitioning* p)
{
    if (!p) return 0; // without partitioning borders there are no mappings

    int myid = laik_myid(p->group);
    if (myid == -1) return 0; // this task is not part of the task group
    assert(myid < p->group->size);

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

    // we need a range list with own ranges
    Laik_RangeList* list = laik_partitioning_myranges(p);
    assert(list != 0); // TODO: API user error

    // number of local ranges
    int sn = list->off[myid+1] - list->off[myid];

    // number of maps
    int n = 0;
    if (sn > 0)
        n = list->trange[list->off[myid+1] - 1].mapNo + 1;

    laik_log(1, "prepareMaps: %d maps for data '%s' (partitioning '%s')",
             n, d->name, p->name);

    // create layout
    Laik_Range* ranges = coveringRanges(n, list, myid);
    Laik_Layout* layout = (n>0) ? (d->layout_factory)(n, ranges) : 0;

    Laik_MappingList* ml = laik_mappinglist_new(d, n, layout);

    for(int mapNo = 0; mapNo < n; mapNo++) {
        Laik_Mapping* m = &(ml->map[mapNo]);
        m->requiredRange = ranges[mapNo];
        m->count = laik_range_size(&(ranges[mapNo]));
        m->layout = layout;       // all maps use same layout
        m->layoutSection = mapNo; // but different sections of it

        if ((mapNo == 0) && (d->map0_base != 0)) {
            laik_log(1, "  using provided memory (%lld bytes at %p with layout %s)",
                     (unsigned long long int) d->map0_size, d->map0_base,
                     layout->describe(m->layout));
            laik_map_set_allocation(m, d->map0_base, d->map0_size, 0);
        }
    }

    free(ranges);  // just allocated here for layout factory function

    return ml;
}

// free memory allocated for mapping <m>
// return number of bytes freed
static
uint64_t freeMap(Laik_Mapping* m, Laik_Data* d, Laik_SwitchStat* ss)
{
    assert(d == m->data);

    if (!m->start) {
        laik_log(1, "free map for data '%s' mapNo %d: nothing was allocated\n",
                 m->data->name, m->mapNo);
        return 0;
    }

    if (m->reusedFor >= 0) {
        laik_log(1, "free map for data '%s' mapNo %d: nothing to do (reused for %d)\n",
                 d->name, m->mapNo, m->reusedFor);
        return 0;
    }                 

    laik_log(1, "free map for data '%s' mapNo %d (capacity %llu, base %p, start %p)\n",
             d->name, m->mapNo,
             (unsigned long long) m->capacity, (void*) m->base, (void*) m->start);

    // if allocator is given, use it to free memory
    uint64_t freed = 0;
    if (m->allocator) {
        laik_switchstat_free(ss, m->capacity);
        freed = m->capacity;

        assert(m->allocator->free);
        (m->allocator->free)(d, m->start);
    }
    m->base = 0;
    m->start = 0;

    return freed;
}

// free memory allocated for all mappings in mapping list <ml>
// return number of bytes freed
static
uint64_t freeMappingList(Laik_MappingList* ml, Laik_SwitchStat* ss)
{
    if (ml == 0) return 0;

    // never free mappings and mapping list from a reservation
    assert(ml->res == 0);

    uint64_t freed = 0;
    for(int i = 0; i < ml->count; i++) {
        Laik_Mapping* m = &(ml->map[i]);
        assert(m != 0);

        if (ml->layout != m->layout) free(m->layout);
        freed += freeMap(m, m->data, ss);
    }

    free(ml->layout);
    free(ml);

    return freed;
}


// provide memory resources covering the required range for a mapping
// - if a non-zero allocator is given, the mapping becomes owner of the
//   provided memory allocation. On destruction mapping->free() is called
// - TODO: support other layouts (this expects lexicographic layout)
static
void laik_map_set_allocation(Laik_Mapping* m,
                             char* start, uint64_t size, Laik_Allocator* a)
{
    // should only be called if not embedded in another mapping
    assert(m->baseMapping == 0);

    // must not be allocated yet
    // here, <base> (first used index) and <start> (allocation address) are actually
    // the same, as we assume "dense" allocation just covering required ranges
    assert(m->start == 0);
    assert(m->base == 0);

    // count should be number of indexes in required range
    assert(m->count == laik_range_size(&(m->requiredRange)));
    // make sure provided memory buffer is large enough
    assert(size >=  m->count * m->data->elemsize);

    // allocated size/count is same as size/count of required range
    m->allocCount = m->count;
    m->allocatedRange = m->requiredRange;

    m->base = start;
    m->start = start;
    m->capacity = size;

    // use given allocator for deallocation
    m->allocator = a;
}



void laik_allocateMap(Laik_Mapping* m, Laik_SwitchStat* ss)
{
    // should only be called if not embedded in another mapping
    assert(m->baseMapping == 0);

    if (m->base) return;
    if (m->count == 0) return;
    Laik_Data* d = m->data;

    // number of bytes to allocate: no space around required indexes
    uint64_t size = m->count * d->elemsize;
    laik_switchstat_malloc(ss, size);

    // use the allocator of the mapping
    Laik_Allocator* a = m->allocator;
    assert(a != 0);
    assert(a->malloc != 0);
    char* start = (a->malloc)(d, size);

    if (!start) {
        laik_log(LAIK_LL_Panic,
                 "Out of memory allocating memory for mapping "
                 "(data '%s', mapNo %d, size %llu)",
                 m->data->name, m->mapNo, (unsigned long long int) size);
        exit(1); // not actually needed, laik_log never returns
    }

    laik_map_set_allocation(m, start, size, a);

    laik_log(1, "allocateMap: for '%s'/%d: %llu x %d (%llu B) at %p",
             d->name, m->mapNo, (unsigned long long int) m->count, d->elemsize,
             (unsigned long long) m->capacity, (void*) m->base);
}

// copy data in a range between mappings
void laik_data_copy(Laik_Range* range,
                    Laik_Mapping* from, Laik_Mapping* to)
{
    if (from->layout->copy && (from->layout->copy == to->layout->copy)) {
        // same layout providing specific copy implementation: use it
        (from->layout->copy)(range, from, to);
        return;
    }

    // different layouts in mappings or no specific copy implementation:
    // use generic variant
    laik_layout_copy_gen(range, from, to);
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
        if (toMap->count == 0) {
            // no elements to copy to
            continue;
        }
        if (fromMap->base == 0) {
            // nothing to copy from
            continue;
        }

        Laik_Data* d = toMap->data;
        Laik_Range* s = &(op->range);

        laik_log(1, "copy data for '%s': range/map %d/%d ==> %d/%d",
                 d->name, op->fromRangeNo, op->fromMapNo,
                 op->toRangeNo, op->toMapNo);

        // no copy needed if mapping reused
        if (fromMap->reusedFor == op->toMapNo) {
            // check that start address of source and destination is same
            uint64_t fromOff = laik_offset(fromMap->layout, fromMap->layoutSection, &(s->from));
            uint64_t toOff = laik_offset(toMap->layout, toMap->layoutSection, &(s->from));
            assert(fromMap->start + fromOff * d->elemsize ==
                   toMap->start   + toOff * d->elemsize);

            laik_log(1, " mapping reused, no copy done");
            continue;
        }

        if (ss)
            ss->copiedBytes += laik_range_size(s) * d->elemsize;

        laik_data_copy(s, fromMap, toMap);
    }
}

// given that memory for a mapping is already allocated,
// we want to re-use part of it for another mapping <toMap>.
// given required range in <toMap>, it must be part of allocation in <fromMap>
// the original base mapping descriptor can be removed afterwards if it
// is ensured that memory is not deleted beyound the new mapping
static
void initEmbeddedMapping(Laik_Mapping* toMap, Laik_Mapping* fromMap)
{
    Laik_Data* data = toMap->data;
    assert(data == fromMap->data);

    assert(laik_range_within_range(&(toMap->requiredRange),
                                   &(fromMap->allocatedRange)));

    // take over allocation into new mapping descriptor
    toMap->start = fromMap->start;
    toMap->allocatedRange = fromMap->allocatedRange;
    toMap->allocCount = fromMap->allocCount;
    toMap->capacity = fromMap->capacity;

    // use allocator of fromMap to deallocate memory
    toMap->allocator = fromMap->allocator;
    fromMap->allocator = 0;

    // set <base> of embedded mapping according to required vs. allocated
    uint64_t off = laik_offset(toMap->layout, toMap->layoutSection, &(toMap->requiredRange.from));
    toMap->base = toMap->start + off * data->elemsize;
}

// try to reuse already allocated memory from old mapping
// we reuse mapping if it has same or larger size
// and if old mapping covers all indexes needed in new mapping.
// If no allocator is set, memory must be reusable
static
void checkMapReuse(Laik_MappingList* toList, Laik_MappingList* fromList)
{
    // reuse only possible if old mappings exist
    if ((fromList == 0) || (fromList->count ==0)) return;
    if ((toList == 0) || (toList->count ==0)) return;

    // no automatic reuse check allowed if reservations are involved
    // (if we stay in same reservation, space is reused anyways)
    if ((fromList->res != 0) || (toList->res != 0)) return;

    // reuse-check implemented in layout interface and same layout type?
    if ((fromList->layout->reuse == 0) ||
        (fromList->layout->reuse != toList->layout->reuse)) return;

    for(int i = 0; i < toList->count; i++) {
        Laik_Mapping* toMap = &(toList->map[i]);

        Laik_Mapping* fromMap = 0;
        int sNo;
        for(sNo = 0; sNo < fromList->count; sNo++) {
            fromMap = &(fromList->map[sNo]);
            if (fromMap->base == 0) continue;
            if (fromMap->reusedFor >= 0) continue; // only reuse once

            // does new mapping fit into old?
            bool reuse = (toList->layout->reuse)(toList->layout, i, fromList->layout, sNo);
            if (!reuse) continue;
            break; // found
        }
        if (sNo == fromList->count) continue;

        // always reuse larger mapping
        initEmbeddedMapping(toMap, fromMap);

        // mark as reused by range <i>: this prohibits delete of memory
        fromMap->reusedFor = i;

        if (laik_log_begin(1)) {
            laik_log_append("map reuse for '%s'/%d ", toMap->data->name, i);
            laik_log_Range(&(toMap->requiredRange));
            laik_log_append(" (in ");
            laik_log_Range(&(toMap->allocatedRange));
            laik_log_flush(" with byte-off %llu), %llu Bytes at %p)\n",
                           (unsigned long long) (toMap->base - toMap->start),
                           (unsigned long long) fromMap->capacity,
                           toMap->base);
        }

        if (toMap->allocator == 0) {
            // When allocator is not set, no re-allocation is
            // possible, so old memory must be reusable
            // FIXME: failed assertion is a user error
            assert(toMap->start != 0);
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
        Laik_Range* s = &(op->range);
        int from = s->from.i[0];
        int to = s->to.i[0];
        int elemCount = to - from;

        char* toBase = toMap->base;
        assert(from >= toMap->requiredRange.from.i[0]);
        toBase += (from - toMap->requiredRange.from.i[0]) * d->elemsize;

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

        laik_log(1, "init map for '%s' range/map %d/%d: %d entries in [%d;%d[ from %p\n",
                 d->name, op->rangeNo, op->mapNo, elemCount, from, to, (void*) toBase);
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

        // to be able to allocate memory, an allocator must be set
        assert(map->allocator != 0);

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

        // only free mappings if not part of a reservation
        if (fromList->res == 0)
            freeMappingList(fromList, d->stat);
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
    if (fromList) {
        // only free mappings if not part of a reservation
        if (fromList->res == 0)
            freeMappingList(fromList, d->stat);
    }
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
    r->mList = 0;

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

// free reservation and the memory space allocated
void laik_reservation_free(Laik_Reservation* r)
{
    for(int i = 0; i < r->count; i++) {
        assert(r->entry[i].mList != 0);
        free(r->entry[i].mList);
        r->entry[i].mList = 0;
    }
    r->count = 0;

    // free allocations done for reservation
    uint64_t bytesFreed = 0;
    if (r->mList) {
        r->mList->res = 0; // unlink to allow freeing
        bytesFreed = freeMappingList(r->mList, r->data->stat);
        r->mList = 0;
    }

    laik_log(1, "reservation '%s' (data '%s'): freed %llu bytes\n",
                 r->name, r->data->name, (unsigned long long) bytesFreed);
                
    free(r);
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

// entry for a range group
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

    // (1) detect how many different mappings (= range groups) are needed
    //     in this process over all partitionings in the reservation.
    //     range groups using same tag in partitionings go into same mapping.
    //     We do this by adding range groups over all partitionings into a
    //     list, sort by tag and count the number of different tags used

    // (1a) calculate list length needed:
    //      number of my range groups in all partitionings
    unsigned int groupCount = 0;
    for(int i = 0; i < res->count; i++) {
        Laik_Partitioning* p = res->entry[i].p;
        // this process must be part of all partitionings to reserve for
        assert(p->group->myid >= 0);
        Laik_RangeList* list = laik_partitioning_myranges(p);
        laik_updateMapOffsets(list, p->group->myid); // could be done always, not just lazy
        assert(list->map_tid == p->group->myid);
        if (list->map_count > 0) assert(list->map_off != 0);
        groupCount += list->map_count;
    }

    // (1b) allocate list and add entries for range groups to list
    struct mygroup *glist = malloc(groupCount * sizeof(struct mygroup));
    unsigned int gOff = 0;
    for(int i = 0; i < res->count; i++) {
        Laik_Partitioning* p = res->entry[i].p;
        Laik_RangeList* list = laik_partitioning_myranges(p);
        for(int mapNo = 0; mapNo < (int) list->map_count; mapNo++) {
            unsigned int off = list->map_off[mapNo];
            int tag = list->trange[off].tag;
            // for reservation, tag >0 to specify partitioning relations
            // TODO: tag == 0 means to use heuristic, but not implemented yet
            //       but we are fine with just one range group and tag 0
            if (list->map_count > 1)
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

    // (2) allocate mapping descriptors, both for
    //     - combined descriptors for same tag in all partitionings, and
    //     - per-partitioning descriptors

    res->mList = laik_mappinglist_new(res->data, mCount, 0);
    for(int i = 0; i < res->count; i++) {
        Laik_Partitioning* p = res->entry[i].p;
        Laik_RangeList* list = laik_partitioning_myranges(p);
        Laik_MappingList* mList = laik_mappinglist_new(res->data, list->map_count, 0);
        mList->res = res;
        res->entry[i].mList = mList;
    }

    // (3) link per-partitioning descriptor to corresponding combined one
    //     and determine required space for each mapping

    for(unsigned int i = 0; i < groupCount; i++) {
        int idx = glist[i].partIndex;

        int partMapNo = glist[i].partMapNo;
        assert(partMapNo < res->entry[idx].mList->count);
        Laik_Mapping* pMap = &(res->entry[idx].mList->map[partMapNo]);

        int resMapNo = glist[i].resMapNo;
        assert(resMapNo < mCount);
        Laik_Mapping* rMap = &(res->mList->map[resMapNo]);

        assert(pMap->baseMapping == 0);
        pMap->baseMapping = rMap;

        // go over all ranges in this range group (same tag) and extend
        Laik_Partitioning* p = res->entry[idx].p;
        Laik_RangeList* list = laik_partitioning_myranges(p);
        for(unsigned int o = list->map_off[partMapNo]; o < list->map_off[partMapNo+1]; o++) {
            assert(list->trange[o].range.space != 0);
            assert(list->trange[o].mapNo == partMapNo);
            assert(list->trange[o].tag == glist[i].tag);
            assert(laik_range_size(&(list->trange[o].range)) > 0);
            if (laik_range_isEmpty(&(pMap->requiredRange)))
                pMap->requiredRange = list->trange[o].range;
            else
                laik_range_expand(&(pMap->requiredRange), &(list->trange[o].range));
        }

        // extend combined mapping descriptor by required size
        assert(pMap->requiredRange.space == data->space);
        if (laik_range_isEmpty(&(rMap->requiredRange)))
            rMap->requiredRange = pMap->requiredRange;
        else
            laik_range_expand(&(rMap->requiredRange), &(pMap->requiredRange));
    }

    free(glist);

    laik_log(1, "reservation '%s': do allocation for '%s'", res->name, data->name);

    // (4) set final sizes of base mappings, and do allocation
    uint64_t total = 0;
    for(int i = 0; i < mCount; i++) {
        Laik_Mapping* m = &(res->mList->map[i]);
        Laik_Range* range = &(m->requiredRange);

        uint64_t count = laik_range_size(range);
        assert(count > 0);
        total += count;
        m->count = count;

        // generate layout using layout factory given in data object
        m->layout = (data->layout_factory)(1, range);
        m->layoutSection = 0;

        laik_allocateMap(m, data->stat);

        if (laik_log_begin(1)) {
            laik_log_append(" map [%d] ", m->mapNo);
            laik_log_Range(&(m->allocatedRange));
            laik_log_flush(", layout %s", (m->layout->describe)(m->layout));
        }
    }

    laik_log(2, "Alloc reservations for '%s': %.3f MB",
             data->name, 0.000001 * (total * data->elemsize));

    // (5) set parameters for embedded mappings
    for(int r = 0; r < res->count; r++) {
        Laik_Partitioning* p = res->entry[r].p;
        Laik_RangeList* list = laik_partitioning_myranges(p);
        laik_log(1, " part '%s':", p->name);
        for(unsigned int mapNo = 0; mapNo < list->map_count; mapNo++) {
            Laik_Mapping* m = &(res->entry[r].mList->map[mapNo]);

            m->allocatedRange = m->baseMapping->requiredRange;
            m->allocCount = m->baseMapping->count;

            Laik_Range* range = &(m->requiredRange);
            m->count = laik_range_size(range);

            m->layout = m->baseMapping->layout;
            m->layoutSection = m->baseMapping->layoutSection;

            initEmbeddedMapping(m, m->baseMapping);

            if (laik_log_begin(1)) {
                laik_log_append("  [%d] ", m->mapNo);
                laik_log_Range(&(m->requiredRange));
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

    Laik_MappingList* toList = prepareMaps(d, t->toPartitioning);
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

    Laik_MappingList* toList = prepareMaps(d, t->toPartitioning);

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

    Laik_Group *toGroup = 0, *fromGroup = 0, *commonGroup = 0;
    if (d->activePartitioning) {
        if (toP && (d->activePartitioning->group != toP->group)) {
            // to a partitioning based on another group? migrate to common group first
            toGroup = toP->group;
            fromGroup = d->activePartitioning->group;
            commonGroup = laik_new_union_group(fromGroup, toGroup);
            laik_partitioning_migrate(d->activePartitioning, commonGroup);
            laik_partitioning_migrate(toP, commonGroup);
        }
    }
    else {
        if (!toP) {
            // nothing to switch from/to
            return;
        }
    }

    Laik_MappingList* toList = prepareMaps(d, toP);
    Laik_Transition* t = do_calc_transition(d->space,
                                            d->activePartitioning, toP,
                                            flow, redOp);

    doTransition(d, t, 0, d->activeMappings, toList);

    // if we migrated to common group before, migrate back
    if (commonGroup) {
        laik_partitioning_migrate(d->activePartitioning, fromGroup);
        laik_partitioning_migrate(toP, toGroup);
    }

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


// get range number <n> in own partition
Laik_TaskRange* laik_data_range(Laik_Data* d, int n)
{
    if (d->activePartitioning == 0) return 0;
    return laik_my_range(d->activePartitioning, n);
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

// set an initial partitioning for a container.
void laik_set_initial_partitioning(Laik_Data* d, Laik_Partitioning* p)
{
    assert(d->activePartitioning == 0);
    assert(d->activeMappings == 0);

    laik_log(1, "set initial partitioning of data '%s' to '%s'",
             d->name, p->name);

    d->activeMappings = prepareMaps(d, p);
    d->activePartitioning = p;
}


void laik_fill_double(Laik_Data* d, double v)
{
    double* base;
    uint64_t count, i;

    // FIXME: this assumes 1d lexicographical layout
    // TODO: partitioning can have multiple ranges
    laik_get_map_1d(d, 0, (void**) &base, &count);
    assert(laik_my_rangecount(d->activePartitioning) == 1);
    for (i = 0; i < count; i++)
        base[i] = v;
}

// for a local index (1d/2d/3d), return offset into memory mapping
// e.g. for (0) / (0,0) / (0,0,0) it returns offset 0
int64_t laik_offset(Laik_Layout* l, int section, Laik_Index* idx)
{
    assert(l && l->offset);
    return (l->offset)(l, section, idx);
}

// get address of entry for index <idx> in mapping <n>
char* laik_get_map_addr(Laik_Data* d, int n, Laik_Index* idx)
{
    Laik_Mapping* m = laik_get_map(d, n);
    if (!m) return 0;

    int64_t off = laik_offset(m->layout, m->layoutSection, idx);
    return m->base + off * d->elemsize;
}



// make sure this process has own partition and mapping descriptors for container <d>
static
void checkOwnParticipation(Laik_Data* d)
{
    // we must have an active partitioning
    assert(d->activePartitioning);
    Laik_Group* g = d->activePartitioning->group;
    if (g->myid == -1) {
        laik_log(LAIK_LL_Error,
                 "laik_map called for data '%s' defined on process group %d.\n"
                 "This process is NOT part of the group. Fix your application!\n"
                 "(may crash now if returned address is dereferenced)",
                 d->name, g->gid);
    }

    // we must have existing mappings
    assert(d->activeMappings != 0);
}

// provide memory resources for a mapping of own partition
void laik_data_provide_memory(Laik_Data* d, void* start, uint64_t size)
{
    d->map0_base = start;
    d->map0_size = size;
}

// get mapping of own partition into local memory for direct access
Laik_Mapping* laik_get_map(Laik_Data* d, int n)
{
    checkOwnParticipation(d);

    if ((n<0) || (n >= d->activeMappings->count))
        return 0;

    Laik_Mapping* m = &(d->activeMappings->map[n]);
    // space always should be allocated
    assert(m->base);

    return m;
}

// for 1d mapping with ID n, return base pointer and count
Laik_Mapping* laik_get_map_1d(Laik_Data* d, int n, void** base, uint64_t* count)
{
    Laik_Mapping* m = laik_get_map(d, n);
    if (!m) {
        if (base) *base = 0;
        if (count) *count = 0;
        return 0;
    }

    if (base) *base = m->base;
    if (count) *count = m->count;
    return m;
}

// for 2d mapping with ID n, describe mapping in output parameters
// this requires lexicographical layout
Laik_Mapping* laik_get_map_2d(Laik_Data* d, int n,
                              void** base, uint64_t* ysize,
                              uint64_t* ystride, uint64_t* xsize)
{
    Laik_Mapping* m = laik_get_map(d, n);
    if (!m) {
        if (base) *base = 0;
        if (xsize) *xsize = 0;
        if (ysize) *ysize = 0;
        return 0;
    }

    Laik_Layout* l = m->layout;
    assert(l);
    if (l->dims != 2)
        laik_log(LAIK_LL_Error, "Querying 2d mapping of an %dd space!",
                 l->dims);

    if (base)
        *base = m->base;
    if (xsize)
        *xsize = m->requiredRange.to.i[0] - m->requiredRange.from.i[0];
    if (ysize)
        *ysize = m->requiredRange.to.i[1] - m->requiredRange.from.i[1];
    if (ystride)
        *ystride = laik_layout_lex_stride(l, m->layoutSection, 1);
    return m;
}

// for 3d mapping with ID n, describe mapping in output parameters
// this requires lexicographical layout
Laik_Mapping* laik_get_map_3d(Laik_Data* d, int n, void** base,
                          uint64_t* zsize, uint64_t* zstride,
                          uint64_t* ysize, uint64_t* ystride,
                          uint64_t* xsize)
{
    Laik_Mapping* m = laik_get_map(d, n);
    if (!m) {
        if (base) *base = 0;
        if (xsize) *xsize = 0;
        if (ysize) *ysize = 0;
        if (zsize) *zsize = 0;
        return 0;
    }

    Laik_Layout* l = m->layout;
    assert(l);
    if (l->dims != 3)
        laik_log(LAIK_LL_Error, "Querying 3d mapping of %dd space!",
                 l->dims);

    if (base)
        *base = m->base;
    if (xsize)
        *xsize = m->requiredRange.to.i[0] - m->requiredRange.from.i[0];
    if (ysize)
        *ysize = m->requiredRange.to.i[1] - m->requiredRange.from.i[1];
    if (ystride)
        *ystride = laik_layout_lex_stride(l, m->layoutSection, 1);
    if (zsize)
        *zsize = m->requiredRange.to.i[2] - m->requiredRange.from.i[2];
    if (zstride)
        *zstride = laik_layout_lex_stride(l, m->layoutSection, 2);
    return m;
}


Laik_Mapping* laik_global2local_1d(Laik_Data* d, int64_t gidx, uint64_t* lidx)
{
    assert(d->space->dims == 1);
    if (!d->activeMappings) return 0;
    for(int i = 0; i < d->activeMappings->count; i++) {
        Laik_Mapping* m = &(d->activeMappings->map[i]);

        if (gidx < m->requiredRange.from.i[0]) continue;
        if (gidx >= m->requiredRange.to.i[0]) continue;

        if (lidx) *lidx = gidx - m->requiredRange.from.i[0];
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

        if (gidx < m->requiredRange.from.i[0]) continue;
        if (gidx >= m->requiredRange.to.i[0]) continue;

        if (lidx) *lidx = gidx - m->requiredRange.from.i[0];
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
    return m->requiredRange.from.i[0] + off;
}


int64_t laik_maplocal2global_1d(Laik_Data* d, int mapNo, uint64_t li)
{
    assert(d->space->dims == 1);
    assert(d->activeMappings);

    // TODO: check all mappings, not just map 0
    Laik_Mapping* m = &(d->activeMappings->map[mapNo]);
    assert(li < m->count);

    // TODO: take layout into account
    return m->requiredRange.from.i[0] + li;
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


//
// Allocator interface
//

// default malloc/free functions
void* def_malloc(Laik_Data* d, size_t size)
{
    (void)d; // not used in this implementation of interface

    return malloc(size);
}

void def_free(Laik_Data* d, void* ptr)
{
    (void)d; // not used in this implementation of interface

    free(ptr);
}


Laik_Allocator* laik_new_allocator(Laik_malloc_t malloc_func,
                                   Laik_free_t free_func,
                                   Laik_realloc_t realloc_func)
{
    Laik_Allocator* a = malloc(sizeof(Laik_Allocator));
    if (!a) {
        laik_panic("Out of memory allocating Laik_Allocator object");
        exit(1); // not actually needed, laik_panic never returns
    }

    a->policy = LAIK_MP_None;
    a->malloc = malloc_func;
    a->free = free_func;
    a->realloc = realloc_func;
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

// returns an allocator with default policy LAIK_MP_NewAllocOnRepartition
Laik_Allocator* laik_new_allocator_def()
{
    Laik_Allocator* a = laik_new_allocator(def_malloc, def_free, 0);
    a->policy = LAIK_MP_NewAllocOnRepartition;

    return a;
}
