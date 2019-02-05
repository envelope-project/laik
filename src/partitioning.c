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


/*
 * partitioning.c
 *
 * This file contains functions related to processing partitionings
 * (Laik_Partitioning objects).
 * It is part of the Index Space module.
*/


#include "laik-internal.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>

/**
 * A partitioning is a set of slices (= consecutive ranges) into a space,
 * with each slice assigned to a process from a LAIK process group.
 *
 * Partitionings are created by
 * - running a partitioner algorithm, or by
 * - reading a pre-created and stored, serialized version from memory
 * Both can be done in a distributed way. Each process of the parallel
 * application may provide only part of the full partitioning, eventually
 * followed by a global synchronization (this is not needed if every process
 * provides the full partitioning).
 *
 * Application processes iteratively ask for the slices assigned to them.
 * Slices are returned in a given normalized order.
 *
 * Partitioner algorithms can be offline and online: an online partitioner
 * is called whenever the application asks for the next slice. In this case,
 * the partitioner is responsible for providing the slices in an adequate
 * order, and coordinating the different called partitioner functions in the
 * parallel processes (e.g. by implementing a global slice queue).
 *
 * In contrast, an offline partitioner algorithm can provide arbitrarily
 * overlapping slices in any arbitrary order. Further, it can group
 * slices by tagging them with group IDs. It is up to the application to make
 * use of such groups. After running the offline partitioner algorithm,
 * LAIK ensures a normalized ordering by optionally splitting/merging slices
 * to remove overlaps and sorting them, grouping the slices if requested.
 *
 * A deterministic offline partitioner algorithm must ensure that it returns
 * the same partitioning, given the same input (space, process group,
 * base partitioning). LAIK may call such partitioners multiple times,
 * especially in different processes to avoid communication, but also in
 * the same process to only store slices currently needed.
 *
 * General useful partitioner algorithms are provided by LAIK.
 * However, the API which needs to be used by a partitioner is specified
 * and applications can provide their own partitioner implementations.
 */

static int partitioning_id = 0;

// internal helper
Laik_Partitioning* laik_partitioning_new(char* name,
                                         Laik_Group* g, Laik_Space* s,
                                         Laik_Partitioner* pr,
                                         Laik_Partitioning* other)
{
    Laik_Partitioning* p;

    assert(s != 0);
    assert(g != 0);

    p = malloc(sizeof(Laik_Partitioning));
    if (p == 0) {
        laik_panic("Out of memory allocating Laik_Partitioning object");
        exit(1); // not actually needed, laik_panic never returns
    }

    p->id = partitioning_id++;
    p->name = strdup("            ");
    sprintf(p->name, "%.10s-%d", name ? name : "part", p->id);

    p->group = g;
    p->space = s;
    p->partitioner = pr;

    p->tslice = 0;
    p->tss1d = 0;

    p->count = 0;
    p->capacity = 0;

    // as long as no offset array is set, this partitioning is invalid
    p->off = 0;

    p->other = other;
    p->fromOther = 0;
    p->intersecting = 0;

    // number of maps still unknown
    p->myMapOff = 0;
    p->myMapCount = -1;

    // no filter set
    p->filter = 0;

    p->consumer = 0;

    p->myfilter = false;
    p->pfilter1 = 0;
    p->pfilter2 = 0;


    return p;
}


/**
 * Create an empty, invalid partitioning on a given space for a given
 * process group.
 *
 * To make the partitioning valid, either a partitioning algorithm has to be
 * run, filling the partitioning with slices, or a partitioner algorithm
 * is set to be run whenever partitioning slices are to be consumed (online).
 *
 * Before running the partitioner, filters on tasks or indexes can be set
 * such that not all slices get stored during the partitioner run.
 * However, such filtered partitionings are not generally useful, e.g.
 * for calculating transitions.
 *
 * Different internal formats are used depending on how a partitioner adds
 * indexes. E.g. adding single indexes are managed differently than slices.
 */
Laik_Partitioning* laik_new_empty_partitioning(Laik_Group* g, Laik_Space* s,
                                               Laik_Partitioner* pr,
                                               Laik_Partitioning* other)
{
    return laik_partitioning_new(0, g, s, pr, other);
}

// create a new empty partitioning using same parameters as in given one
Laik_Partitioning* laik_clone_empty_partitioning(Laik_Partitioning* p)
{
    return laik_partitioning_new(p->name, p->group, p->space,
                                 p->partitioner, p->other);
}


// helper for laik_partitioning_set_myfilter
static
bool myfilter(Laik_Partitioning* p, int task, Laik_Slice* s)
{
    (void) s; // unused parameter of filter signature

    assert(p->myfilter); // only to be called with "my" filter active
    return p->group->myid == task;
}

// set filter to only keep slices for own process when adding slices
void laik_partitioning_set_myfilter(Laik_Partitioning* p)
{
    assert(p->off == 0);

    assert(p->filter == 0); // no filter set yet
    p->myfilter = true;
    p->filter = myfilter;
}


// helper for laik_partitioning_set_pfilter

typedef struct {
    int64_t from, to;
    Laik_TaskSlice_Gen* ts;
    Laik_Partitioning* p;
    int len;
} PFilterPar;

static PFilterPar pfilter_par1, pfilter_par2;

// check if [from;to[ intersects slices given in par
static bool pfilter_check(int64_t from, int64_t to, PFilterPar* par)
{
    assert(par->len > 0);

    laik_log(1,"  filter [%lld;%lld[ check with '%s' in range [%lld;%lld[",
             (long long) from, (long long) to,
             par->p->name, (long long) par->from, (long long) par->to);

    if ((from >= par->to) || (to <= par->from)) {
        laik_log(1,"    no intersection!");
        return false;
    }

    // binary search
    Laik_TaskSlice_Gen* ts = par->ts;
    int off1 = 0, off2 = par->len;
    while(off1 < off2) {
        int mid = (off1 + off2) / 2;
        laik_log(1,"  filter check with '%s' at %d: [%lld;%lld[",
                 par->p->name, mid,
                 (long long) ts[mid].s.from.i[0],
                 (long long) ts[mid].s.to.i[0]);

        if (from >= ts[mid].s.to.i[0]) {
            laik_log(1,"    larger, check against %d: [%lld;%lld[",
                     mid+1,
                     (long long) ts[mid+1].s.from.i[0],
                     (long long) ts[mid+1].s.to.i[0]);
            if (to <= ts[mid+1].s.from.i[0]) {
                laik_log(1,"    no intersection!");
                return false;
            }
            off1 = mid;
            continue;
        }
        if (to <= ts[mid].s.from.i[0]) {
            laik_log(1,"    smaller, check against %d: [%lld;%lld[",
                     mid-1,
                     (long long) ts[mid-1].s.from.i[0],
                     (long long) ts[mid-1].s.to.i[0]);
            if (from >= ts[mid-1].s.to.i[0]) {
                laik_log(1,"    no intersection!");
                return false;
            }
            off2 = mid;
            continue;
        }
        break;
    }
    laik_log(1,"    found intersection!");
    return true;
}


static
bool pfilter(Laik_Partitioning* p, int task, Laik_Slice* s)
{
    (void) task; // unused parameter of filter signature
    (void) p; // unused parameter of filter signature

    int64_t from = s->from.i[0];
    int64_t to = s->to.i[0];

    if ((pfilter_par1.len > 0) && pfilter_check(from, to, &pfilter_par1))
        return true;
    if ((pfilter_par2.len > 0) && pfilter_check(from, to, &pfilter_par2))
        return true;
    return false;
}

// add filter to only keep slices intersecting with own slices in <filter>
void laik_partitioning_add_pfilter(Laik_Partitioning* p, Laik_Partitioning* filter)
{
    PFilterPar* par = 0;
    assert(filter);

    assert(p->off == 0);
    assert(p->space->dims == 1); // TODO: only for 1d for now

    // already set?
    if ((p->pfilter1 == filter) || (p->pfilter2 == filter)) return;

    // check free slot to add pfilter
    if (p->pfilter1 == 0)
        par = &pfilter_par1;
    else if (p->pfilter2 == 0)
        par = &pfilter_par2;
    else {
        assert(0); // no space!
        return;
    }

    // partitioning we intersect with should store own slices
    assert(filter->off != 0);
    assert(filter->myfilter);
    assert(filter->space == p->space);

    int off1 = filter->off[p->group->myid];
    int off2 = filter->off[p->group->myid + 1];
    if (off1 == off2) {
        // no slices, no need to add filter
        return;
    }

    par->len = off2 - off1;
    par->from = filter->tslice[off1].s.from.i[0];
    par->to = filter->tslice[off2 - 1].s.to.i[0];
    par->ts = filter->tslice + off1;
    par->p = p;
    laik_log(1,"Set pfilter for '%s' to intersection with '%s': %d own slices, between [%lld;%lld[",
             p->name, filter->name, par->len,
             (long long) par->from, (long long) par->to);

    if (par == &pfilter_par1)
        p->pfilter1 = filter;
    else
        p->pfilter2 = filter;

    // install filter function
    p->filter = pfilter;
}


// is the partitioning <filter> set to be a pfilter for <p> ?
bool laik_partitioning_has_pfilter(Laik_Partitioning* p,
                                   Laik_Partitioning* filter)
{
    assert(p);
    assert(filter);

    return (p->pfilter1 == filter) || (p->pfilter2 == filter);
}


// partitioner API: add a slice
// - specify slice groups by giving slices the same tag
// - arbitrary data can be attached to slices if no merge step is done
void laik_append_slice(Laik_Partitioning* p, int task, Laik_Slice* s,
                       int tag, void* data)
{
    // if filter is installed, check if we should store the slice
    if (p->filter) {
        bool res = (p->filter)(p, task, s);
        laik_log(1,"filter added slice %d:[%lld;%lld[: %s",
                 task,
                 (long long) s->from.i[0], (long long) s->to.i[0],
                 res ? "keep":"skip");
        if (res == false) return;
    }

    assert(s->space == p->space);

    // TODO: convert previously added single indexes into slices
    assert(p->tss1d == 0);

    if (p->count == p->capacity) {
        p->capacity = (p->capacity + 2) * 2;
        p->tslice = realloc(p->tslice,
                            sizeof(Laik_TaskSlice_Gen) * p->capacity);
        if (!p->tslice) {
            laik_panic("Out of memory allocating memory for Laik_Partitioning");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    assert((task >= 0) && (task < p->group->size));
    assert(laik_slice_within_space(s, p->space));
    assert(p->tslice);

    Laik_TaskSlice_Gen* ts = &(p->tslice[p->count]);
    p->count++;

    ts->task = task;
    ts->s = *s;
    ts->tag = tag;
    ts->data = data;
    ts->mapNo = 0;
}

// partitioner API: add a single 1d index slice, optimized for fast merging
// if a partitioner only uses this method, an optimized internal format is used
void laik_append_index_1d(Laik_Partitioning* p, int task, int64_t idx)
{
    if (p->filter) {
        Laik_Slice slc;
        laik_slice_init_1d(&slc, p->space, idx, idx + 1);
        if ((p->filter)(p, task, &slc) == false) return;
    }
    assert(p->space->dims == 1);

    if (p->tslice) {
        // append as generic slice
        Laik_Slice slc;
        laik_slice_init_1d(&slc, p->space, idx, idx + 1);
        laik_append_slice(p, task, &slc, 1, 0);
        return;
    }

    if (p->count == p->capacity) {
        assert(p->tslice == 0);
        p->capacity = (p->capacity + 2) * 2;
        p->tss1d = realloc(p->tss1d,
                           sizeof(Laik_TaskSlice_Single1d) * p->capacity);
        if (!p->tss1d) {
            laik_panic("Out of memory allocating memory for Laik_Partitioning");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    assert((task >= 0) && (task < p->group->size));
    assert((idx >= p->space->s.from.i[0]) && (idx < p->space->s.to.i[0]));
    assert(p->tss1d);

    Laik_TaskSlice_Single1d* ts = &(p->tss1d[p->count]);
    p->count++;

    ts->task = task;
    ts->idx = idx;
}


// public: return the space a partitioning is used for
Laik_Space* laik_partitioning_get_space(Laik_Partitioning* p)
{
    return p->space;
}

// public: return the process group a partitioning is used for
// slices are associated to processes using their rank in the process group
Laik_Group* laik_partitioning_get_group(Laik_Partitioning* p)
{
    return p->group;
}

// public/partitioner API: total number of slices in this partitioning
// only allowed for offline partitioners, may be expensive
int laik_partitioning_slicecount(Laik_Partitioning* p)
{
    return p->count;
}

// partitioner API: return a slice from an existing partitioning
// useful to implement partitioners deriving their partitions from slices
// of another partitioning, or for incremental partitioners (returning
// a new, slightly changed version of a partitiong e.g. for load balancing)
Laik_TaskSlice* laik_partitioning_get_tslice(Laik_Partitioning* p, int n)
{
    static Laik_TaskSlice ts;

    if (n >= p->count) return 0;

    ts.p = p;
    ts.no = n;
    return &ts;
}


// public: get a custom data pointer from the partitioner
void* laik_partitioner_data(Laik_Partitioner* partitioner)
{
    return partitioner->data;
}


// internal helpers

// sort function, called after partitioner run
static
int tsgen_cmp(const void *p1, const void *p2)
{
    const Laik_TaskSlice_Gen* ts1 = (const Laik_TaskSlice_Gen*) p1;
    const Laik_TaskSlice_Gen* ts2 = (const Laik_TaskSlice_Gen*) p2;
    if (ts1->task == ts2->task) {
        // we want same tags in a row for processing in prepareMaps
        if (ts1->tag == ts2->tag) {
            // sort slices for same task by start index (not really needed)
            return ts1->s.from.i[0] - ts2->s.from.i[0];
        }
        return ts1->tag - ts2->tag;
    }
    return ts1->task - ts2->task;
}

static
int tss1d_cmp(const void *p1, const void *p2)
{
    const Laik_TaskSlice_Single1d* ts1 = (const Laik_TaskSlice_Single1d*) p1;
    const Laik_TaskSlice_Single1d* ts2 = (const Laik_TaskSlice_Single1d*) p2;
    if (ts1->task == ts2->task) {
        // sort slices for same task by index
        return ts1->idx - ts2->idx;
    }
    return ts1->task - ts2->task;
}

static
void sortSlices(Laik_Partitioning* p)
{
    // nothing to sort?
    if (p->count == 0) return;

    // slices get sorted into groups for each task,
    //  then per tag (to go into one mapping),
    //  then per start index (to enable merging)
    assert(p->tslice);
    qsort( &(p->tslice[0]), p->count,
            sizeof(Laik_TaskSlice_Gen), tsgen_cmp);
}

static
void mergeSortedSlices(Laik_Partitioning* p)
{
    assert(p->tslice); // this is for generic slices
    if (p->count == 0) return;

    assert(p->space->dims == 1); // current algorithm only works for 1d

    // for sorted slices of same task and same mapping, we do one traversal:
    // either a slice can be merged with the previous one or it can not.
    // - if yes, the merging only can increase the slice end index, but never
    //   decrease the start index (due to sorting), thus no merging with
    //   old slices needs to be checked
    // - if not, no later slice can be mergable with the previous one, as
    //   start index is same or larger than current one

    int srcOff = 1, dstOff = 0;
    for(; srcOff < p->count; srcOff++) {
        if ((p->tslice[srcOff].task != p->tslice[dstOff].task) ||
            (p->tslice[srcOff].tag  != p->tslice[dstOff].tag) ||
            (p->tslice[srcOff].s.from.i[0] > p->tslice[dstOff].s.to.i[0])) {
            // different task/tag or not overlapping/directly after:
            //  not mergable
            dstOff++;
            if (dstOff < srcOff)
                p->tslice[dstOff] = p->tslice[srcOff];
            continue;
        }
        // merge: only may need to extend end index to include src slice
        if (p->tslice[dstOff].s.to.i[0] < p->tslice[srcOff].s.to.i[0])
            p->tslice[dstOff].s.to.i[0] = p->tslice[srcOff].s.to.i[0];
    }
    p->count = dstOff + 1;
}

// (1) update offset array from slices,
// (2) calculate map numbers from tags
static
void updatePartitioningOffsets(Laik_Partitioning* p)
{
    if (p->count > 0)
        assert(p->tslice);

    // we assume that the slices where sorted with sortSlices()

    int task, mapNo, lastTag, off;
    off = 0;
    for(task = 0; task < p->group->size; task++) {
        p->off[task] = off;
        mapNo = -1; // for numbering of mappings according to tags
        lastTag = -1;
        while(off < p->count) {
            Laik_TaskSlice_Gen* ts = &(p->tslice[off]);
            if (ts->task > task) break;
            assert(ts->task == task);
            if ((ts->tag == 0) || (ts->tag != lastTag)) {
                mapNo++;
                lastTag = ts->tag;
            }
            ts->mapNo = mapNo;
            off++;
        }
    }
    p->off[task] = off;
    assert(off == p->count);

    // there was a new partitioner run, which may change my mappings
    if (p->myMapCount >= 0) {
        free(p->myMapOff);
        p->myMapOff = 0;
        p->myMapCount = -1;
    }
}

// update offset array from slices, single index format
// also, convert to generic format
static
void updatePartitioningOffsetsSI(Laik_Partitioning* p)
{
    assert(p->tss1d);
    assert(p->count > 0);

    // make sure slices are sorted according by task IDs
    qsort( &(p->tss1d[0]), p->count,
            sizeof(Laik_TaskSlice_Single1d), tss1d_cmp);

    // count slices
    int64_t idx, idx0;
    int task;
    int count = 1;
    task = p->tss1d[0].task;
    idx = p->tss1d[0].idx;
    for(int i = 1; i < p->count; i++) {
        if (p->tss1d[i].task == task) {
            if (p->tss1d[i].idx == idx) continue;
            if (p->tss1d[i].idx == idx + 1) {
                idx++;
                continue;
            }
        }
        task = p->tss1d[i].task;
        idx = p->tss1d[i].idx;
        count++;
    }
    laik_log(1, "Merging single indexes: %d original, %d merged",
             p->count, count);

    p->tslice = malloc(sizeof(Laik_TaskSlice_Gen) * count);
    if (!p->tslice) {
        laik_panic("Out of memory allocating memory for Laik_Partitioning");
        exit(1); // not actually needed, laik_panic never returns
    }

    // convert into generic slices (already sorted)
    int off = 0, j = 0;
    task = p->tss1d[0].task;
    idx0 = idx = p->tss1d[0].idx;
    for(int i = 1; i <= p->count; i++) {
        if ((i < p->count) && (p->tss1d[i].task == task)) {
            if (p->tss1d[i].idx == idx) continue;
            if (p->tss1d[i].idx == idx + 1) {
                idx++;
                continue;
            }
        }
        laik_log(1, "  adding slice for offsets %d - %d: task %d, [%lld;%lld[",
                 j, i-1, task,
                 (long long) idx0, (long long) (idx + 1) );

        Laik_TaskSlice_Gen* ts = &(p->tslice[off]);
        ts->task = task;
        ts->tag = 0;
        ts->mapNo = 0;
        ts->data = 0;
        ts->s.space = p->space;
        ts->s.from.i[0] = idx0;
        ts->s.to.i[0] = idx + 1;
        off++;
        if (i == p->count) break;

        task = p->tss1d[i].task;
        idx0 = idx = p->tss1d[i].idx;
        j = i;
    }
    assert(count == off);
    p->count = count;
    free(p->tss1d);
    p->tss1d = 0;

    // update offsets
    off = 0;
    for(task = 0; task < p->group->size; task++) {
        p->off[task] = off;
        while(off < p->count) {
            Laik_TaskSlice_Gen* ts = &(p->tslice[off]);
            if (ts->task > task) break;
            assert(ts->task == task);
            off++;
        }
    }
    p->off[task] = off;
    assert(off == p->count);

    // there was a new partitioner run, which may change my mappings
    if (p->myMapCount >= 0) {
        free(p->myMapOff);
        p->myMapOff = 0;
        p->myMapCount = -1;
    }
}

// internal
void laik_updateMyMapOffsets(Laik_Partitioning* p)
{
    // already calculated?
    if (p->myMapCount >= 0) return;

    int myid = p->group->myid;
    assert(myid >= 0);

    int mapNo;
    int firstOff = p->off[myid];
    int lastOff = p->off[myid + 1];
    if (lastOff > firstOff)
        p->myMapCount = p->tslice[lastOff - 1].mapNo + 1;
    else {
        p->myMapCount = 0;
        return;
    }

    p->myMapOff = malloc((p->myMapCount + 1) * sizeof(int));
    if (!p->myMapOff) {
        laik_panic("Out of memory allocating memory for Laik_BorderArray");
        exit(1); // not actually needed, laik_panic never returns
    }

    // we only have generic task slices (single-1d are already converted)
    assert(p->tss1d == 0);

    int off = firstOff;
    for(mapNo = 0; mapNo < p->myMapCount; mapNo++) {
        p->myMapOff[mapNo] = off;
        while(off < lastOff) {
            Laik_TaskSlice_Gen* ts = &(p->tslice[off]);
            if (ts->mapNo > mapNo) break;
            assert(ts->mapNo == mapNo);
            off++;
        }
    }
    p->myMapOff[mapNo] = off;
    assert(off == lastOff);
}

// internal: allows to reuse a partitioning object
void laik_clear_partitioning(Laik_Partitioning* p)
{
    // to remove all entries, it's enough to set count to 0
    p->count = 0;
}

// free resources allocated for a partitioning object
void laik_free_partitioning(Laik_Partitioning* p)
{
    if (p->intersecting)
        laik_free_partitioning(p->intersecting);

    free(p->off);
    free(p->myMapOff);
    free(p->tslice);
    free(p->tss1d);
    free(p);
}


// public functions to check for assumptions an application may have
// for a partitioning as generated by an partitioner. Especially, this
// allows to check for correct implementation of self-written partitioners

// public: does this cover the full space with one slice for each process?
// such a partitioning is generated by the "laik_All" partitioner.
// (requires normalized order: task slices are sorted according to processes)
bool laik_partitioning_isAll(Laik_Partitioning* p)
{
    // no filter allowed
    assert(p->filter == 0);

    if (p->count != p->group->size) return false;
    for(int i = 0; i < p->count; i++) {
        if (p->tslice[i].task != i) return false;
        if (!laik_slice_isEqual(&(p->tslice[i].s), &(p->space->s)))
            return false;
    }
    return true;
}

// public:
// does this cover the full space with one slice in exactly one process?
// Such a partitioning is generated by the "laik_Master" partitioner
// Return -1 if no, else process rank
int laik_partitioning_isSingle(Laik_Partitioning* p)
{
    // no filter allowed
    assert(p->filter == 0);

    if (p->count != 1) return -1;
    if (!laik_slice_isEqual(&(p->tslice[0].s), &(p->space->s)))
        return -1;

    return p->tslice[0].task;
}


// internal helpers for laik_partitioning_coversSpace

// print verbose debug output?
//#define DEBUG_COVERSPACE 1

// TODO: use dynamic list
#define COVERLIST_MAX 100
static Laik_Slice notcovered[COVERLIST_MAX];
static int notcovered_count;

static void appendToNotcovered(Laik_Slice* s)
{
    assert(notcovered_count < COVERLIST_MAX);
    notcovered[notcovered_count] = *s;
    notcovered_count++;
}

#ifdef DEBUG_COVERSPACE
static void log_Notcovered(int dims, Laik_Slice* toRemove)
{
    laik_log_append("not covered: (");
    for(int j = 0; j < notcovered_count; j++) {
        if (j>0) laik_log_append(", ");
        laik_log_Slice(dims, &(notcovered[j]));
    }
    laik_log_append(")");
    if (toRemove) {
        laik_log_append("\n  removing ");
        laik_log_Slice(dims, toRemove);
    }
}
#endif

static
int tsgen_cmpfrom(const void *p1, const void *p2)
{
    const Laik_TaskSlice_Gen* ts1 = (const Laik_TaskSlice_Gen*) p1;
    const Laik_TaskSlice_Gen* ts2 = (const Laik_TaskSlice_Gen*) p2;
    return ts1->s.from.i[0] - ts2->s.from.i[0];
}

// public:
// do the slices of this partitioning cover the full space?
//
// this works for 1d/2d/3d spaces
//
// we maintain a list of slices not yet covered,
// starting with the one slice equal to full space, and then
// subtract the slices from the partitioning step-by-step
// from each of the not-yet-covered slices, creating a
// new list of not-yet-covered slices.
//
// Note: subtraction of a slice from another one may result in
// multiple smaller slices which are appended to the not-yet-covered
// list (eg. in 3d, 6 smaller slices may be created).
bool laik_partitioning_coversSpace(Laik_Partitioning* p)
{
    // no filter allowed
    assert(p->filter == 0);

    int dims = p->space->dims;
    notcovered_count = 0;

    // start with full space not-yet-covered
    appendToNotcovered(&(p->space->s));

    // use a copy of slice list which is just sorted by slice start
    Laik_TaskSlice_Gen* list;
    list = malloc(p->count * sizeof(Laik_TaskSlice_Gen));
    if (!list) {
        laik_panic("Out of memory allocating memory for coversSpace");
        exit(1); // not actually needed, laik_panic never returns
    }

    memcpy(list, p->tslice, p->count * sizeof(Laik_TaskSlice_Gen));
    qsort(list, p->count, sizeof(Laik_TaskSlice_Gen), tsgen_cmpfrom);

    // remove each slice in partitioning
    for(int i = 0; i < p->count; i++) {
        Laik_Slice* toRemove = &(list[i].s);

#ifdef DEBUG_COVERSPACE
        if (laik_log_begin(1)) {
            laik_log_append("coversSpace - ");
            log_Notcovered(dims, toRemove);
            laik_log_flush(0);
        }
#endif

        int count = notcovered_count; // number of slices to visit
        for(int j = 0; j < count; j++) {
            Laik_Slice* orig = &(notcovered[j]);

            if (laik_slice_intersect(orig, toRemove) == 0) {
                // slice to remove does not overlap with orig: keep original
                appendToNotcovered(orig);
                continue;
            }

            // subtract toRemove from orig

            // check for space not covered in orig, loop through valid dims
            for(int d = 0; d < dims; d++) {
                // space in dim <d> before <toRemove> ?
                if (orig->from.i[d] < toRemove->from.i[d]) {
                    // yes, add to not-covered
                    Laik_Slice s = *orig;
                    s.to.i[d] = toRemove->from.i[d];
                    appendToNotcovered(&s);
                    // remove appended part from <orig>
                    orig->from.i[d] = toRemove->from.i[d];
                }
                // space in dim <d> after <toRemove> ?
                if (orig->to.i[d] > toRemove->to.i[d]) {
                    Laik_Slice s = *orig;
                    s.from.i[d] = toRemove->to.i[d];
                    appendToNotcovered(&s);
                    // remove appended part from <orig>
                    orig->to.i[d] = toRemove->to.i[d];
                }
            }
        }
        if (notcovered_count == count) {
            // nothing appended, ie. nothing left?
            notcovered_count = 0;
            break;
        }
        // move appended slices to start
        for(int j = 0; j < notcovered_count - count; j++)
            notcovered[j] = notcovered[count + j];
        notcovered_count = notcovered_count - count;
    }

#ifdef DEBUG_COVERSPACE
    if (laik_log_begin(1)) {
        laik_log_append("coversSpace - remaining ");
        log_Notcovered(dims, 0);
        laik_log_flush(0);
    }
#endif

    free(list);

    // only if no slices are left, we did cover full space
    return (notcovered_count == 0);
}



// public: are the borders of two partitionings equal?
bool laik_partitioning_isEqual(Laik_Partitioning* p1, Laik_Partitioning* p2)
{
    // no filters allowed
    assert(p1->filter == 0);
    assert(p2->filter == 0);

    // partitionings needs to be valid
    assert(p1 && p1->off);
    assert(p2 && p2->off);
    if (p1->group != p2->group) return false;
    if (p1->space != p2->space) return false;
    if (p1->count != p2->count) return false;

    for(int i = 0; i < p1->group->size; i++)
        if (p1->off[i] != p2->off[i]) return false;

    for(int i = 0; i < p1->count; i++) {
        // tasks must match, as offset array matched
        assert(p1->tslice[i].task == p2->tslice[i].task);

        if (!laik_slice_isEqual(&(p1->tslice[i].s), &(p2->tslice[i].s)))
            return false;
    }
    return true;
}

// make partitioning valid after a partitioner run by freezing (immutable)
void laik_freeze_partitioning(Laik_Partitioning* p, bool doMerge)
{
    assert(p->off == 0);

    // set partitioning valid by allocating/updating offsets
    p->off = malloc(sizeof(int) * (p->group->size + 1));
    if (p->off == 0) {
        laik_panic("Out of memory allocating Laik_Partitioning object");
        exit(1); // not actually needed, laik_panic never returns
    }

    if (p->tss1d) {
        // merge and convert to generic
        updatePartitioningOffsetsSI(p);
    }
    else {
        sortSlices(p);

        // check for mergable slices if requested
        if (doMerge)
            mergeSortedSlices(p);

        updatePartitioningOffsets(p);
    }
}

// run a partitioner on a yet invalid, empty partitioning
void laik_run_partitioner(Laik_Partitioning* p)
{
    // has to be invalid yet
    assert(p->off == 0);
    Laik_Partitioner* pr = p->partitioner;

    if (pr) {
        if (p->other) {
            assert(p->other->group == p->group);
            // we do not check for same space, as there are use cases
            // where you want to derive a partitioning of one space from
            // the partitioning of another
        }
        (pr->run)(pr, p, p->other);
    }
    else if (p->other && p->other->consumer == p) {
        // we consume slices from <other>, so run its partitioner
        laik_run_partitioner(p->other);
    }

    bool doMerge = false;
    if (pr) doMerge = (pr->flags & LAIK_PF_Merge) > 0;
    laik_freeze_partitioning(p, doMerge);

    if (laik_log_begin(1)) {
        laik_log_append("run partitioner '%s' for '%s' (group %d, myid %d, space '%s'):",
                        pr ? pr->name : "(other)", p->name,
                        p->group->gid, p->group->myid, p->space->name);
        laik_log_append("\n  other: ");
        laik_log_Partitioning(p->other);
        laik_log_append("\n  new  : ");
        laik_log_Partitioning(p);
        laik_log_flush(0);
    }
    else
        laik_log(2, "run partitioner '%s' for '%s' (group %d, space '%s'): %d slices",
                 pr ? pr->name : "(other)", p->name,
                 p->group->gid, p->space->name, p->count);


    // by default, check if partitioning covers full space
    // unless partitioner has flag to not do it, or task filter is used
    bool doCoverageCheck = false;
    if (pr) doCoverageCheck = (pr->flags & LAIK_PF_NoFullCoverage) == 0;
    if (p->filter) doCoverageCheck = false;
    if (doCoverageCheck) {
        if (!laik_partitioning_coversSpace(p))
            laik_log(LAIK_LL_Panic, "partitioning borders do not cover space");
    }
}


// public: create a new partitioning by running an offline partitioner
// the partitioner may be derived from another partitioning which is
// forwarded to the partitioner algorithm
Laik_Partitioning* laik_new_partitioning(Laik_Partitioner* pr,
                                         Laik_Group* g, Laik_Space* space,
                                         Laik_Partitioning* otherP)
{
    Laik_Partitioning* p;
    p = laik_new_empty_partitioning(g, space, pr, otherP);
#ifdef USE_OWNSLICES
    // for 1d space, default to always calculate only own slices
    if (space->dims == 1)
        laik_partitioning_set_myfilter(p);
#endif
    laik_run_partitioner(p);
    return p;
}

// new partitioning taking slices from another, migrating to new group
Laik_Partitioning* laik_new_migrated_partitioning(Laik_Partitioning* other,
                                                  Laik_Group* newg)
{
    Laik_Partitioning* p;
    p = laik_new_empty_partitioning(newg, other->space, 0, other);

    // setup mapping
    if (newg->parent == other->group) {
        // new group is child of <other>
        p->fromOther = newg->fromParent;
    }
    else if (other->group->parent == newg) {
        // new group is parent of <other>
        p->fromOther = other->group->toParent;
    }
    else {
        // other cases not supported
        assert(0);
    }

    // we can clone from another partitioning only if its slices are freezed
    assert(other->off != 0);

    // keep property stating that <other> only kept own slices
    p->myfilter = other->myfilter;

    // travers slices and translate task numbers
    for(int i = 0; i < other->count; i++) {
        Laik_TaskSlice_Gen* ts = &(other->tslice[i]);
        assert((ts->task >= 0) && (ts->task < other->group->size));
        // old tasks with slices should not be removed in <newg>
        assert(p->fromOther[ts->task] >= 0);
        laik_append_slice(p, p->fromOther[ts->task],
                          &(ts->s), ts->tag, ts->data);
    }
    laik_freeze_partitioning(p, false);
    return p;
}



// get number of slices for this task
int laik_my_slicecount(Laik_Partitioning* p)
{
    // partitioning needs to be valid
    assert(p->off != 0);

    int myid = p->group->myid;
    if (myid < 0) return 0; // this task is not part of task group
    assert(myid < p->group->size);
    return p->off[myid+1] - p->off[myid];
}

// get number of mappings for this task
int laik_my_mapcount(Laik_Partitioning* p)
{
    // partitioning needs to be valid
    assert(p->off != 0);

    int myid = p->group->myid;
    if (myid < 0) return 0; // this process is not part of the process group
    assert(myid < p->group->size);
    int sCount = p->off[myid+1] - p->off[myid];
    if (sCount == 0) return 0;

    // map number of my last slice, incremented by one to get count
    return p->tslice[p->off[myid+1] - 1].mapNo + 1;
}

// get number of slices within a given mapping for this task
int laik_my_mapslicecount(Laik_Partitioning* p, int mapNo)
{
    // partitioning needs to be valid
    assert(p->off != 0);

    int myid = p->group->myid;
    if (myid < 0) return 0; // this task is not part of task group

    // lazily calculate my map offsets
    if (p->myMapCount < 0)
        laik_updateMyMapOffsets(p);

    if (mapNo >= p->myMapCount) return 0;
    return p->myMapOff[mapNo + 1] - p->myMapOff[mapNo];
}

// get slice number <n> from the slices for this task
Laik_TaskSlice* laik_my_slice(Laik_Partitioning* p, int n)
{
    // partitioning needs to be valid
    assert(p->off != 0);

    int myid = p->group->myid;
    if (myid < 0) return 0; // this task is not part of task group

    int o = p->off[myid] + n;
    if (o >= p->off[myid+1]) {
        // slice <n> is invalid
        return 0;
    }
    assert(p->tslice[o].task == myid);
    return laik_partitioning_get_tslice(p, o);
}

// get slice number <n> within mapping <mapNo> from the slices for this task
Laik_TaskSlice* laik_my_mapslice(Laik_Partitioning* p, int mapNo, int n)
{
    // partitioning needs to be valid
    assert(p->off != 0);

    int myid = p->group->myid;
    if (myid < 0) return 0; // this task is not part of task group

    // lazily calculate my map offsets
    if (p->myMapCount < 0)
        laik_updateMyMapOffsets(p);

    // does map with mapNo exist?
    if (mapNo >= p->myMapCount) return 0;

    int o = p->myMapOff[mapNo] + n;
    if (o >= p->myMapOff[mapNo + 1]) {
        // slice <n> is invalid
        return 0;
    }
    assert(p->tslice[o].task == myid);
    assert(p->tslice[o].mapNo == mapNo);
    return laik_partitioning_get_tslice(p, o);
}

// get borders of slice number <n> from the 1d slices for this task
Laik_TaskSlice* laik_my_slice_1d(Laik_Partitioning* p, int n,
                                 int64_t* from, int64_t* to)
{
    assert(p->space->dims == 1);
    Laik_TaskSlice* ts = laik_my_slice(p, n);
    const Laik_Slice* s = ts ? laik_taskslice_get_slice(ts) : 0;
    if (from) *from = s ? s->from.i[0] : 0;
    if (to)   *to   = s ? s->to.i[0] : 0;
    return ts;
}

// get borders of slice number <n> from the 2d slices for this task
Laik_TaskSlice* laik_my_slice_2d(Laik_Partitioning* p, int n,
                                 int64_t* x1, int64_t* x2,
                                 int64_t* y1, int64_t* y2)
{
    assert(p->space->dims == 2);
    Laik_TaskSlice* ts = laik_my_slice(p, n);
    const Laik_Slice* s = ts ? laik_taskslice_get_slice(ts) : 0;
    if (x1) *x1 = s ? s->from.i[0] : 0;
    if (x2) *x2 = s ? s->to.i[0] : 0;
    if (y1) *y1 = s ? s->from.i[1] : 0;
    if (y2) *y2 = s ? s->to.i[1] : 0;

    return ts;
}

// get borders of slice number <n> from the 3d slices for this task
Laik_TaskSlice* laik_my_slice_3d(Laik_Partitioning* p, int n,
                                 int64_t* x1, int64_t* x2,
                                 int64_t* y1, int64_t* y2,
                                 int64_t* z1, int64_t* z2)
{
    assert(p->space->dims == 3);
    Laik_TaskSlice* ts = laik_my_slice(p, n);
    const Laik_Slice* s = ts ? laik_taskslice_get_slice(ts) : 0;
    if (x1) *x1 = s ? s->from.i[0] : 0;
    if (x2) *x2 = s ? s->to.i[0] : 0;
    if (y1) *y1 = s ? s->from.i[1] : 0;
    if (y2) *y2 = s ? s->to.i[1] : 0;
    if (z1) *z1 = s ? s->from.i[2] : 0;
    if (z2) *z2 = s ? s->to.i[2] : 0;

    return ts;
}

// give an access phase a name, for debug output
void laik_partitioning_set_name(Laik_Partitioning* p, char* n)
{
    p->name = strdup(n);
}
