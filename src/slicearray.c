/*
 * This file is part of the LAIK library.
 * Copyright (c) 2017-2019 Josef Weidendorfer <Josef.Weidendorfer@gmx.de>
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
#include <string.h>

/// Laik_SliceArray

Laik_SliceArray* laik_slicearray_new(Laik_Space* space, unsigned int tid_count)
{
    Laik_SliceArray* sa;

    sa = malloc(sizeof(Laik_SliceArray));
    if (sa == 0) {
        laik_panic("Out of memory allocating Laik_SliceArray object");
        exit(1); // not actually needed, laik_panic never returns
    }

    sa->space = space;
    sa->tid_count = tid_count;

    sa->tslice = 0;
    sa->tss1d = 0; // TODO: move into a temporary SliceReceiver
    sa->count = 0;
    sa->capacity = 0;

    // as long as no offset array is set, this slice array is invalid
    sa->off = 0;

    // number of maps still unknown
    sa->map_tid = -1; // not used
    sa->map_off = 0;
    sa->map_count = 0;

    return sa;
}

void laik_slicearray_free(Laik_SliceArray* sa)
{
    free(sa->tslice);
    free(sa->tss1d);
    free(sa->off);
    free(sa->map_off);
}

// does this cover the full space with one slice for each process?
bool laik_slicearray_isAll(Laik_SliceArray* sa)
{
    if (sa->count != sa->tid_count) return false;
    for(unsigned int i = 0; i < sa->count; i++) {
        if (sa->tslice[i].task != (int) i) return false;
        if (!laik_slice_isEqual(&(sa->tslice[i].s), &(sa->space->s)))
            return false;
    }
    return true;
}

// does this cover the full space with one slice in exactly one task?
// return -1 if no, else process rank
int laik_slicearray_isSingle(Laik_SliceArray* sa)
{
    if (sa->count != 1) return -1;
    if (!laik_slice_isEqual(&(sa->tslice[0].s), &(sa->space->s)))
        return -1;

    return sa->tslice[0].task;
}

// are the slices of two slice arrays equal?
bool laik_slicearray_isEqual(Laik_SliceArray* sa1, Laik_SliceArray* sa2)
{
    // partitionings needs to be valid
    assert(sa1 && sa1->off);
    assert(sa2 && sa2->off);
    if (sa1->tid_count != sa2->tid_count) return false;
    if (sa1->space != sa2->space) return false;
    if (sa1->count != sa2->count) return false;

    for(unsigned int i = 0; i < sa1->tid_count; i++)
        if (sa1->off[i] != sa2->off[i]) return false;

    for(unsigned int i = 0; i < sa1->count; i++) {
        // tasks must match, as offset array matched
        assert(sa1->tslice[i].task == sa2->tslice[i].task);

        if (!laik_slice_isEqual(&(sa1->tslice[i].s), &(sa2->tslice[i].s)))
            return false;
    }
    return true;
}

// get number of slices
int laik_slicearray_slicecount(Laik_SliceArray* sa)
{
    return (int) sa->count;
}

int laik_slicearray_tidslicecount(Laik_SliceArray* sa, int tid)
{
    assert(sa->off != 0);
    assert((tid >= 0) && (tid < (int) sa->tid_count));

    return (int)(sa->off[tid+1] - sa->off[tid]);
}

// get number of mappings for this task
int laik_slicearray_tidmapcount(Laik_SliceArray* sa, int tid)
{
    assert(sa->off != 0);
    assert((tid >= 0) && (tid < (int) sa->tid_count));
    if (sa->off[tid+1] == sa->off[tid]) return 0;

    // map number of my last slice, incremented by one to get count
    return sa->tslice[sa->off[tid+1] - 1].mapNo + 1;
}

Laik_TaskSlice* laik_slicearray_tslice(Laik_SliceArray* sa, int n)
{
    static Laik_TaskSlice ts;

    if (n >= (int) sa->count) return 0;

    ts.sa = sa;
    ts.no = n;
    return &ts;
}


// get slice number <n> of slices from task id
// returns a pointer to a global instance, needs to be copied of stored
Laik_TaskSlice* laik_slicearray_tidslice(Laik_SliceArray* sa, int tid, int n)
{
    assert(sa->off != 0);
    assert((tid >= 0) && (tid < (int) sa->tid_count));
    int count = (int)(sa->off[tid + 1] - sa->off[tid]);

    // slice <n> invalid?
    if ((n < 0) || (n >= count)) return 0;
    int o = (int) sa->off[tid] + n;
    assert(sa->tslice[o].task == tid);
    return laik_slicearray_tslice(sa, o);
}


// internal helpers for laik_slicearray_coversSpace

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
    if (ts1->s.from.i[0] > ts2->s.from.i[0]) return 1;
    if (ts1->s.from.i[0] == ts2->s.from.i[0]) return 0;
    return -1;
}

// do the slices of this partitioning cover the full space?
// (currently works for 1d/2d/3d spaces)
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
bool laik_slicearray_coversSpace(Laik_SliceArray* sa)
{
    int dims = sa->space->dims;
    notcovered_count = 0;

    // start with full space not-yet-covered
    appendToNotcovered(&(sa->space->s));

    // use a copy of slice list which is just sorted by slice start
    Laik_TaskSlice_Gen* list;
    list = malloc(sa->count * sizeof(Laik_TaskSlice_Gen));
    if (!list) {
        laik_panic("Out of memory allocating memory for coversSpace");
        exit(1); // not actually needed, laik_panic never returns
    }

    memcpy(list, sa->tslice, sa->count * sizeof(Laik_TaskSlice_Gen));
    qsort(list, sa->count, sizeof(Laik_TaskSlice_Gen), tsgen_cmpfrom);

    // remove each slice in partitioning
    for(unsigned int i = 0; i < sa->count; i++) {
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


// add a slice with tag and arbitrary data to a slice array
void laik_slicearray_append(Laik_SliceArray* sa, int tid, const Laik_Slice* s,
                            int tag, void* data)
{
    assert(s->space == sa->space);

    // not allowed to add slices with different APIs
    assert(sa->tss1d == 0);

    if (sa->count == sa->capacity) {
        sa->capacity = (sa->capacity + 2) * 2;
        sa->tslice = realloc(sa->tslice,
                             sizeof(Laik_TaskSlice_Gen) * sa->capacity);
        if (!sa->tslice) {
            laik_panic("Out of memory allocating memory for Laik_SliceArray");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    assert((tid >= 0) && (tid < (int) sa->tid_count));
    assert(laik_slice_within_space(s, sa->space));
    assert(sa->tslice);

    Laik_TaskSlice_Gen* ts = &(sa->tslice[sa->count]);
    sa->count++;

    ts->task = tid;
    ts->s = *s;
    ts->tag = tag;
    ts->data = data;
    ts->mapNo = 0;
}

// add a slice with a single 1d index to a slice array (space optimized)
void laik_slicearray_append_single1d(Laik_SliceArray* sa, int tid, int64_t idx)
{
    // not allowed to add slices with different APIs
    assert(sa->tslice == 0);

    if (sa->count == sa->capacity) {
        assert(sa->tslice == 0);
        sa->capacity = (sa->capacity + 2) * 2;
        sa->tss1d = realloc(sa->tss1d,
                            sizeof(Laik_TaskSlice_Single1d) * sa->capacity);
        if (!sa->tss1d) {
            laik_panic("Out of memory allocating memory for Laik_Partitioning");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    assert(sa->tss1d);
    assert((tid >= 0) && (tid < (int) sa->tid_count));
    assert((idx >= sa->space->s.from.i[0]) && (idx < sa->space->s.to.i[0]));

    Laik_TaskSlice_Single1d* ts = &(sa->tss1d[sa->count]);
    sa->count++;

    ts->task = tid;
    ts->idx = idx;
}

// internal helpers for SliceArray

// sort function, called when freezing, after adding slices
static int tsgen_cmp(const void *p1, const void *p2)
{
    const Laik_TaskSlice_Gen* ts1 = (const Laik_TaskSlice_Gen*) p1;
    const Laik_TaskSlice_Gen* ts2 = (const Laik_TaskSlice_Gen*) p2;
    if (ts1->task == ts2->task) {
        // we want same tags in a row for processing in prepareMaps
        if (ts1->tag == ts2->tag) {
            // sort slices for same task by start index (not really needed)
            if (ts1->s.from.i[0] > ts2->s.from.i[0]) return 1;
            if (ts1->s.from.i[0] == ts2->s.from.i[0]) return 0;
            return -1;
        }
        return ts1->tag - ts2->tag;
    }
    return ts1->task - ts2->task;
}

static int tss1d_cmp(const void *p1, const void *p2)
{
    const Laik_TaskSlice_Single1d* ts1 = (const Laik_TaskSlice_Single1d*) p1;
    const Laik_TaskSlice_Single1d* ts2 = (const Laik_TaskSlice_Single1d*) p2;
    if (ts1->task == ts2->task) {
        // sort slices for same task by index
        if (ts1->idx > ts2->idx) return 1;
        if (ts1->idx == ts2->idx) return 0;
        return -1;
    }
    return ts1->task - ts2->task;
}

static void sortSlices(Laik_SliceArray* sa)
{
    // nothing to sort?
    if (sa->count == 0) return;

    // slices get sorted into groups for each task,
    //  then per tag (to go into one mapping),
    //  then per start index (to enable merging)
    assert(sa->tslice);
    qsort(&(sa->tslice[0]), sa->count,
          sizeof(Laik_TaskSlice_Gen), tsgen_cmp);
}

static void mergeSortedSlices(Laik_SliceArray* sa)
{
    assert(sa->tslice); // this is for generic slices
    if (sa->count == 0) return;

    assert(sa->space->dims == 1); // current algorithm only works for 1d

    // for sorted slices of same task and same mapping, we do one traversal:
    // either a slice can be merged with the previous one or it can not.
    // - if yes, the merging only can increase the slice end index, but never
    //   decrease the start index (due to sorting), thus no merging with
    //   old slices needs to be checked
    // - if not, no later slice can be mergable with the previous one, as
    //   start index is same or larger than current one

    unsigned int srcOff = 1, dstOff = 0;
    for(; srcOff < sa->count; srcOff++) {
        if ((sa->tslice[srcOff].task != sa->tslice[dstOff].task) ||
            (sa->tslice[srcOff].tag  != sa->tslice[dstOff].tag) ||
            (sa->tslice[srcOff].s.from.i[0] > sa->tslice[dstOff].s.to.i[0])) {
            // different task/tag or not overlapping/directly after:
            //  not mergable
            dstOff++;
            if (dstOff < srcOff)
                sa->tslice[dstOff] = sa->tslice[srcOff];
            continue;
        }
        // merge: only may need to extend end index to include src slice
        if (sa->tslice[dstOff].s.to.i[0] < sa->tslice[srcOff].s.to.i[0])
            sa->tslice[dstOff].s.to.i[0] = sa->tslice[srcOff].s.to.i[0];
    }
    sa->count = dstOff + 1;
}

// (1) update offset array from slices,
// (2) calculate map numbers from tags
static void updateOffsets(Laik_SliceArray* sa)
{
    if (sa->count > 0)
        assert(sa->tslice);

    // we assume that the slices where sorted with sortSlices()

    int task, mapNo, lastTag;
    unsigned int off = 0;
    for(task = 0; task < (int) sa->tid_count; task++) {
        sa->off[task] = off;
        mapNo = -1; // for numbering of mappings according to tags
        lastTag = -1;
        while(off < sa->count) {
            Laik_TaskSlice_Gen* ts = &(sa->tslice[off]);
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
    sa->off[task] = off;
    assert(off == sa->count);
}

// update offset array from slices, single index format
// also, convert to generic format
static void updateOffsetsSI(Laik_SliceArray* sa)
{
    assert(sa->tss1d);
    assert(sa->count > 0);

    // make sure slices are sorted according by task IDs
    qsort(&(sa->tss1d[0]), sa->count,
          sizeof(Laik_TaskSlice_Single1d), tss1d_cmp);

    // count slices
    int64_t idx, idx0;
    int task;
    unsigned int count = 1;
    task = sa->tss1d[0].task;
    idx = sa->tss1d[0].idx;
    for(unsigned int i = 1; i < sa->count; i++) {
        if (sa->tss1d[i].task == task) {
            if (sa->tss1d[i].idx == idx) continue;
            if (sa->tss1d[i].idx == idx + 1) {
                idx++;
                continue;
            }
        }
        task = sa->tss1d[i].task;
        idx = sa->tss1d[i].idx;
        count++;
    }
    laik_log(1, "Merging single indexes: %d original, %d merged",
             sa->count, count);

    sa->tslice = malloc(sizeof(Laik_TaskSlice_Gen) * count);
    if (!sa->tslice) {
        laik_panic("Out of memory allocating memory for Laik_Partitioning");
        exit(1); // not actually needed, laik_panic never returns
    }

    // convert into generic slices (already sorted)
    unsigned int off = 0, j = 0;
    task = sa->tss1d[0].task;
    idx0 = idx = sa->tss1d[0].idx;
    for(unsigned int i = 1; i <= sa->count; i++) {
        if ((i < sa->count) && (sa->tss1d[i].task == task)) {
            if (sa->tss1d[i].idx == idx) continue;
            if (sa->tss1d[i].idx == idx + 1) {
                idx++;
                continue;
            }
        }
        laik_log(1, "  adding slice for offsets %d - %d: task %d, [%lld;%lld[",
                 j, i-1, task,
                 (long long) idx0, (long long) (idx + 1) );

        Laik_TaskSlice_Gen* ts = &(sa->tslice[off]);
        ts->task = task;
        ts->tag = 0;
        ts->mapNo = 0;
        ts->data = 0;
        ts->s.space = sa->space;
        ts->s.from.i[0] = idx0;
        ts->s.to.i[0] = idx + 1;
        off++;
        if (i == sa->count) break;

        task = sa->tss1d[i].task;
        idx0 = idx = sa->tss1d[i].idx;
        j = i;
    }
    assert(count == off);
    sa->count = count;
    free(sa->tss1d);
    sa->tss1d = 0;

    // update offsets
    off = 0;
    for(task = 0; task < (int) sa->tid_count; task++) {
        sa->off[task] = off;
        while(off < sa->count) {
            Laik_TaskSlice_Gen* ts = &(sa->tslice[off]);
            if (ts->task > task) break;
            assert(ts->task == task);
            off++;
        }
    }
    sa->off[task] = off;
    assert(off == sa->count);
}

// internal
void laik_updateMapOffsets(Laik_SliceArray* sa, int tid)
{
    // already calculated?
    if (sa->map_tid == tid) return;
    assert(sa->map_tid < 0);
    sa->map_tid = tid;

    assert((tid >= 0) && (tid < (int) sa->tid_count));

    unsigned int firstOff = sa->off[tid];
    unsigned int lastOff = sa->off[tid + 1];
    if (lastOff > firstOff)
        sa->map_count = (unsigned)(sa->tslice[lastOff - 1].mapNo + 1);
    else {
        sa->map_count = 0;
        return;
    }

    sa->map_off = malloc((sa->map_count + 1) * sizeof(int));
    if (!sa->map_off) {
        laik_panic("Out of memory allocating memory for Laik_BorderArray");
        exit(1); // not actually needed, laik_panic never returns
    }

    // only works with generic task slices (other formats converted)
    assert(sa->tss1d == 0);

    int mapNo;
    unsigned int off = firstOff;
    for(mapNo = 0; mapNo < (int) sa->map_count; mapNo++) {
        sa->map_off[mapNo] = off;
        while(off < lastOff) {
            Laik_TaskSlice_Gen* ts = &(sa->tslice[off]);
            if (ts->mapNo > mapNo) break;
            assert(ts->mapNo == mapNo);
            off++;
        }
    }
    sa->map_off[mapNo] = off;
    assert(off == lastOff);
}

unsigned int laik_slicearray_tidmapslicecount(Laik_SliceArray* sa, int tid, int mapNo)
{
    assert(sa->off != 0);

    // lazily calculate my map offsets
    if (sa->map_tid != tid)
        laik_updateMapOffsets(sa, tid);

    assert((mapNo >= 0) && (mapNo < (int) sa->map_count));
    return sa->map_off[mapNo + 1] - sa->map_off[mapNo];
}

// get slice number <n> within mapping <mapNo>
// returns a pointer to a global instance, needs to be copied of stored
Laik_TaskSlice* laik_slicearray_tidmapslice(Laik_SliceArray* sa, int tid, int mapNo, int n)
{
    assert(sa->off != 0);

    // lazily calculate my map offsets
    if (sa->map_tid != tid)
        laik_updateMapOffsets(sa, tid);

    // does map with mapNo exist?
    if ((mapNo < 0) || (mapNo >= (int) sa->map_count)) return 0;

    // is slice <n> valid?
    int count = (int)(sa->map_off[mapNo + 1] - sa->map_off[mapNo]);
    if ((n < 0) || (n >= count)) return 0;

    int o = (int) sa->map_off[mapNo] + n;
    assert(sa->tslice[o].task == tid);
    assert(sa->tslice[o].mapNo == mapNo);
    return laik_slicearray_tslice(sa, o);
}


// freeze slice array
void laik_slicearray_freeze(Laik_SliceArray* sa, bool doMerge)
{
    assert(sa->off == 0);

    // set partitioning valid by allocating/updating offsets
    sa->off = malloc(sizeof(int) * (sa->tid_count + 1));
    if (sa->off == 0) {
        laik_panic("Out of memory allocating space for Laik_SliceArray object");
        exit(1); // not actually needed, laik_panic never returns
    }

    if (sa->tss1d) {
        // merge and convert to generic
        updateOffsetsSI(sa);
    }
    else {
        sortSlices(sa);

        // check for mergable slices if requested
        if (doMerge)
            mergeSortedSlices(sa);

        updateOffsets(sa);
    }
}

// translate task ids using <idmap> array: idmap[old_id] = new_id
// if idmap[id] == -1, no slice with that id is allowed to exist
void laik_slicearray_migrate(Laik_SliceArray* sa, int* idmap, unsigned int new_count)
{
    assert(sa->off != 0);

    // check that there are no slices of removed task ids
    for(unsigned int i = 0; i < sa->tid_count; i++) {
        if (idmap[i] < 0)
            assert(sa->off[i] == sa->off[i+1]);
    }

    // update slice task ids
    for(unsigned int i = 0; i < sa->count; i++) {
        int old_id = sa->tslice[i].task;
        assert((old_id >= 0) && (old_id < (int) sa->tid_count));
        int new_id = idmap[old_id];
        assert((new_id >= 0) && (new_id < (int) new_count));
        sa->tslice[i].task = new_id;
    }

    // resize offset array if needed
    if (new_count > sa->tid_count) {
        free(sa->off);
        sa->off = malloc((new_count +1) * sizeof(int));
        if (!sa->off) {
            laik_panic("Out of memory allocating space for Laik_SliceArray");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    sa->tid_count = new_count;
    sortSlices(sa);
    updateOffsets(sa);
}
