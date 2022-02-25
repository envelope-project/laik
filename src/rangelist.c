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

/// Laik_RangeList

Laik_RangeList* laik_rangelist_new(Laik_Space* space, unsigned int tid_count)
{
    Laik_RangeList* list;

    list = malloc(sizeof(Laik_RangeList));
    if (list == 0) {
        laik_panic("Out of memory allocating Laik_RangeList object");
        exit(1); // not actually needed, laik_panic never returns
    }

    list->space = space;
    list->tid_count = tid_count;

    list->trange = 0;
    list->tss1d = 0; // TODO: move into a temporary RangeReceiver
    list->count = 0;
    list->capacity = 0;

    // as long as no offset array is set, this range list is invalid
    list->off = 0;

    // number of maps still unknown
    list->map_tid = -1; // not used
    list->map_off = 0;
    list->map_count = 0;

    return list;
}

void laik_rangelist_free(Laik_RangeList* list)
{
    free(list->trange);
    free(list->tss1d);
    free(list->off);
    free(list->map_off);
}

// does this cover the full space with one range for each process?
bool laik_rangelist_isAll(Laik_RangeList* list)
{
    if (list->count != list->tid_count) return false;
    for(unsigned int i = 0; i < list->count; i++) {
        if (list->trange[i].task != (int) i) return false;
        if (!laik_range_isEqual(&(list->trange[i].range), &(list->space->range)))
            return false;
    }
    return true;
}

// does this cover the full space with one range in exactly one task?
// return -1 if no, else process rank
int laik_rangelist_isSingle(Laik_RangeList* list)
{
    if (list->count != 1) return -1;
    if (!laik_range_isEqual(&(list->trange[0].range), &(list->space->range)))
        return -1;

    return list->trange[0].task;
}

// are the ranges of two range lists equal?
bool laik_rangelist_isEqual(Laik_RangeList* r1, Laik_RangeList* r2)
{
    // partitionings needs to be valid
    assert(r1 && r1->off);
    assert(r2 && r2->off);
    if (r1->tid_count != r2->tid_count) return false;
    if (r1->space != r2->space) return false;
    if (r1->count != r2->count) return false;

    for(unsigned int i = 0; i < r1->tid_count; i++)
        if (r1->off[i] != r2->off[i]) return false;

    for(unsigned int i = 0; i < r1->count; i++) {
        // tasks must match, as offset array matched
        assert(r1->trange[i].task == r2->trange[i].task);

        if (!laik_range_isEqual(&(r1->trange[i].range), &(r2->trange[i].range)))
            return false;
    }
    return true;
}

// get number of ranges
int laik_rangelist_rangecount(Laik_RangeList* list)
{
    return (int) list->count;
}

int laik_rangelist_tidrangecount(Laik_RangeList* list, int tid)
{
    assert(list->off != 0);
    assert((tid >= 0) && (tid < (int) list->tid_count));

    return (int)(list->off[tid+1] - list->off[tid]);
}

// get number of mappings for this task
int laik_rangelist_tidmapcount(Laik_RangeList* list, int tid)
{
    assert(list->off != 0);
    assert((tid >= 0) && (tid < (int) list->tid_count));
    if (list->off[tid+1] == list->off[tid]) return 0;

    // map number of my last range, incremented by one to get count
    return list->trange[list->off[tid+1] - 1].mapNo + 1;
}

Laik_TaskRange* laik_rangelist_taskrange(Laik_RangeList* list, int n)
{
    static Laik_TaskRange ts;

    if (n >= (int) list->count) return 0;

    ts.list = list;
    ts.no = n;
    return &ts;
}


// get range number <n> of ranges from task id
// returns a pointer to a global instance, needs to be copied of stored
Laik_TaskRange* laik_rangelist_tidrange(Laik_RangeList* list, int tid, int n)
{
    assert(list->off != 0);
    assert((tid >= 0) && (tid < (int) list->tid_count));
    int count = (int)(list->off[tid + 1] - list->off[tid]);

    // range <n> invalid?
    if ((n < 0) || (n >= count)) return 0;
    int o = (int) list->off[tid] + n;
    assert(list->trange[o].task == tid);
    return laik_rangelist_taskrange(list, o);
}


// internal helpers for laik_rangelist_coversSpace

// print verbose debug output?
//#define DEBUG_COVERSPACE 1

// TODO: use dynamic list
#define COVERLIST_MAX 100
static Laik_Range notcovered[COVERLIST_MAX];
static int notcovered_count;

static void appendToNotcovered(Laik_Range* s)
{
    assert(notcovered_count < COVERLIST_MAX);
    notcovered[notcovered_count] = *s;
    notcovered_count++;
}

#ifdef DEBUG_COVERSPACE
static void log_Notcovered(int dims, Laik_Range* toRemove)
{
    laik_log_append("not covered: (");
    for(int j = 0; j < notcovered_count; j++) {
        if (j>0) laik_log_append(", ");
        laik_log_Range(dims, &(notcovered[j]));
    }
    laik_log_append(")");
    if (toRemove) {
        laik_log_append("\n  removing ");
        laik_log_Range(dims, toRemove);
    }
}
#endif

static
int trgen_cmpfrom(const void *p1, const void *p2)
{
    const Laik_TaskRange_Gen* ts1 = (const Laik_TaskRange_Gen*) p1;
    const Laik_TaskRange_Gen* ts2 = (const Laik_TaskRange_Gen*) p2;
    if (ts1->range.from.i[0] > ts2->range.from.i[0]) return 1;
    if (ts1->range.from.i[0] == ts2->range.from.i[0]) return 0;
    return -1;
}

// do the ranges of this partitioning cover the full space?
// (currently works for 1d/2d/3d spaces)
//
// we maintain a list of ranges not yet covered,
// starting with the one range equal to full space, and then
// subtract the ranges from the partitioning step-by-step
// from each of the not-yet-covered ranges, creating a
// new list of not-yet-covered ranges.
//
// Note: subtraction of a range from another one may result in
// multiple smaller ranges which are appended to the not-yet-covered
// list (eg. in 3d, 6 smaller ranges may be created).
bool laik_rangelist_coversSpace(Laik_RangeList* list)
{
    int dims = list->space->dims;
    notcovered_count = 0;

    // start with full space not-yet-covered
    appendToNotcovered(&(list->space->range));

    // use a copy of range list which is just sorted by range start
    Laik_TaskRange_Gen* trlist;
    trlist = malloc(list->count * sizeof(Laik_TaskRange_Gen));
    if (!trlist) {
        laik_panic("Out of memory allocating memory for coversSpace");
        exit(1); // not actually needed, laik_panic never returns
    }

    memcpy(trlist, list->trange, list->count * sizeof(Laik_TaskRange_Gen));
    qsort(trlist, list->count, sizeof(Laik_TaskRange_Gen), trgen_cmpfrom);

    // remove each range in partitioning
    for(unsigned int i = 0; i < list->count; i++) {
        Laik_Range* toRemove = &(trlist[i].range);

#ifdef DEBUG_COVERSPACE
        if (laik_log_begin(1)) {
            laik_log_append("coversSpace - ");
            log_Notcovered(dims, toRemove);
            laik_log_flush(0);
        }
#endif

        int count = notcovered_count; // number of ranges to visit
        for(int j = 0; j < count; j++) {
            Laik_Range* orig = &(notcovered[j]);

            if (laik_range_intersect(orig, toRemove) == 0) {
                // range to remove does not overlap with orig: keep original
                appendToNotcovered(orig);
                continue;
            }

            // subtract toRemove from orig

            // check for space not covered in orig, loop through valid dims
            for(int d = 0; d < dims; d++) {
                // space in dim <d> before <toRemove> ?
                if (orig->from.i[d] < toRemove->from.i[d]) {
                    // yes, add to not-covered
                    Laik_Range s = *orig;
                    s.to.i[d] = toRemove->from.i[d];
                    appendToNotcovered(&s);
                    // remove appended part from <orig>
                    orig->from.i[d] = toRemove->from.i[d];
                }
                // space in dim <d> after <toRemove> ?
                if (orig->to.i[d] > toRemove->to.i[d]) {
                    Laik_Range s = *orig;
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
        // move appended ranges to start
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

    free(trlist);

    // only if no ranges are left, we did cover full space
    return (notcovered_count == 0);
}


// add a range with tag and arbitrary data to a range list
void laik_rangelist_append(Laik_RangeList* list, int tid, const Laik_Range* range,
                            int tag, void* data)
{
    assert(range->space == list->space);

    // not allowed to add ranges with different APIs
    assert(list->tss1d == 0);

    if (list->count == list->capacity) {
        list->capacity = (list->capacity + 2) * 2;
        list->trange = realloc(list->trange,
                             sizeof(Laik_TaskRange_Gen) * list->capacity);
        if (!list->trange) {
            laik_panic("Out of memory allocating memory for Laik_RangeList");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    assert((tid >= 0) && (tid < (int) list->tid_count));
    assert(laik_range_within_space(range, list->space));
    assert(list->trange);

    Laik_TaskRange_Gen* tr = &(list->trange[list->count]);
    list->count++;

    tr->task = tid;
    tr->range = *range;
    tr->tag = tag;
    tr->data = data;
    tr->mapNo = 0;
}

// add a range with a single 1d index to a range list (space optimized)
void laik_rangelist_append_single1d(Laik_RangeList* list, int tid, int64_t idx)
{
    // not allowed to add ranges with different APIs
    assert(list->trange == 0);

    if (list->count == list->capacity) {
        assert(list->trange == 0);
        list->capacity = (list->capacity + 2) * 2;
        list->tss1d = realloc(list->tss1d,
                            sizeof(Laik_TaskRange_Single1d) * list->capacity);
        if (!list->tss1d) {
            laik_panic("Out of memory allocating memory for Laik_Partitioning");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    assert(list->tss1d);
    assert((tid >= 0) && (tid < (int) list->tid_count));
    assert((idx >= list->space->range.from.i[0]) && (idx < list->space->range.to.i[0]));

    Laik_TaskRange_Single1d* ts = &(list->tss1d[list->count]);
    list->count++;

    ts->task = tid;
    ts->idx = idx;
}

// internal helpers for RangeList

// sort function, called when freezing, after adding ranges
static int trgen_cmp(const void *p1, const void *p2)
{
    const Laik_TaskRange_Gen* ts1 = (const Laik_TaskRange_Gen*) p1;
    const Laik_TaskRange_Gen* ts2 = (const Laik_TaskRange_Gen*) p2;
    if (ts1->task == ts2->task) {
        // we want same tags in a row for processing in prepareMaps
        if (ts1->tag == ts2->tag) {
            // sort ranges for same task by start index (not really needed)
            if (ts1->range.from.i[0] > ts2->range.from.i[0]) return 1;
            if (ts1->range.from.i[0] == ts2->range.from.i[0]) return 0;
            return -1;
        }
        return ts1->tag - ts2->tag;
    }
    return ts1->task - ts2->task;
}

static int tss1d_cmp(const void *p1, const void *p2)
{
    const Laik_TaskRange_Single1d* ts1 = (const Laik_TaskRange_Single1d*) p1;
    const Laik_TaskRange_Single1d* ts2 = (const Laik_TaskRange_Single1d*) p2;
    if (ts1->task == ts2->task) {
        // sort ranges for same task by index
        if (ts1->idx > ts2->idx) return 1;
        if (ts1->idx == ts2->idx) return 0;
        return -1;
    }
    return ts1->task - ts2->task;
}

static void sortRanges(Laik_RangeList* list)
{
    // nothing to sort?
    if (list->count == 0) return;

    // ranges get sorted into groups for each task,
    //  then per tag (to go into one mapping),
    //  then per start index (to enable merging)
    assert(list->trange);
    qsort(&(list->trange[0]), list->count,
          sizeof(Laik_TaskRange_Gen), trgen_cmp);
}

static void mergeSortedRanges(Laik_RangeList* list)
{
    assert(list->trange); // this is for generic ranges
    if (list->count == 0) return;

    assert(list->space->dims == 1); // current algorithm only works for 1d

    // for sorted ranges of same task and same mapping, we do one traversal:
    // either a range can be merged with the previous one or it can not.
    // - if yes, the merging only can increase the range end index, but never
    //   decrease the start index (due to sorting), thus no merging with
    //   old ranges needs to be checked
    // - if not, no later range can be mergable with the previous one, as
    //   start index is same or larger than current one

    unsigned int srcOff = 1, dstOff = 0;
    for(; srcOff < list->count; srcOff++) {
        if ((list->trange[srcOff].task != list->trange[dstOff].task) ||
            (list->trange[srcOff].tag  != list->trange[dstOff].tag) ||
            (list->trange[srcOff].range.from.i[0] > list->trange[dstOff].range.to.i[0])) {
            // different task/tag or not overlapping/directly after:
            //  not mergable
            dstOff++;
            if (dstOff < srcOff)
                list->trange[dstOff] = list->trange[srcOff];
            continue;
        }
        // merge: only may need to extend end index to include src range
        if (list->trange[dstOff].range.to.i[0] < list->trange[srcOff].range.to.i[0])
            list->trange[dstOff].range.to.i[0] = list->trange[srcOff].range.to.i[0];
    }
    list->count = dstOff + 1;
}

// (1) update offset array from ranges,
// (2) calculate map numbers from tags
static void updateOffsets(Laik_RangeList* list)
{
    if (list->count > 0)
        assert(list->trange);

    // we assume that the ranges where sorted with sortRanges()

    int task, mapNo, lastTag;
    unsigned int off = 0;
    for(task = 0; task < (int) list->tid_count; task++) {
        list->off[task] = off;
        mapNo = -1; // for numbering of mappings according to tags
        lastTag = -1;
        while(off < list->count) {
            Laik_TaskRange_Gen* ts = &(list->trange[off]);
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
    list->off[task] = off;
    assert(off == list->count);
}

// update offset array from ranges, single index format
// also, convert to generic format
static void updateOffsetsSI(Laik_RangeList* list)
{
    assert(list->tss1d);
    assert(list->count > 0);

    // make sure ranges are sorted according by task IDs
    qsort(&(list->tss1d[0]), list->count,
          sizeof(Laik_TaskRange_Single1d), tss1d_cmp);

    // count ranges
    int64_t idx, idx0;
    int task;
    unsigned int count = 1;
    task = list->tss1d[0].task;
    idx = list->tss1d[0].idx;
    for(unsigned int i = 1; i < list->count; i++) {
        if (list->tss1d[i].task == task) {
            if (list->tss1d[i].idx == idx) continue;
            if (list->tss1d[i].idx == idx + 1) {
                idx++;
                continue;
            }
        }
        task = list->tss1d[i].task;
        idx = list->tss1d[i].idx;
        count++;
    }
    laik_log(1, "Merging single indexes: %d original, %d merged",
             list->count, count);

    list->trange = malloc(sizeof(Laik_TaskRange_Gen) * count);
    if (!list->trange) {
        laik_panic("Out of memory allocating memory for Laik_Partitioning");
        exit(1); // not actually needed, laik_panic never returns
    }

    // convert into generic ranges (already sorted)
    unsigned int off = 0, j = 0;
    task = list->tss1d[0].task;
    idx0 = idx = list->tss1d[0].idx;
    for(unsigned int i = 1; i <= list->count; i++) {
        if ((i < list->count) && (list->tss1d[i].task == task)) {
            if (list->tss1d[i].idx == idx) continue;
            if (list->tss1d[i].idx == idx + 1) {
                idx++;
                continue;
            }
        }
        laik_log(1, "  adding range for offsets %d - %d: task %d, [%lld;%lld[",
                 j, i-1, task,
                 (long long) idx0, (long long) (idx + 1) );

        Laik_TaskRange_Gen* ts = &(list->trange[off]);
        ts->task = task;
        ts->tag = 0;
        ts->mapNo = 0;
        ts->data = 0;
        ts->range.space = list->space;
        ts->range.from.i[0] = idx0;
        ts->range.to.i[0] = idx + 1;
        off++;
        if (i == list->count) break;

        task = list->tss1d[i].task;
        idx0 = idx = list->tss1d[i].idx;
        j = i;
    }
    assert(count == off);
    list->count = count;
    free(list->tss1d);
    list->tss1d = 0;

    // update offsets
    off = 0;
    for(task = 0; task < (int) list->tid_count; task++) {
        list->off[task] = off;
        while(off < list->count) {
            Laik_TaskRange_Gen* ts = &(list->trange[off]);
            if (ts->task > task) break;
            assert(ts->task == task);
            off++;
        }
    }
    list->off[task] = off;
    assert(off == list->count);
}

// internal
void laik_updateMapOffsets(Laik_RangeList* list, int tid)
{
    // already calculated?
    if (list->map_tid == tid) return;
    assert(list->map_tid < 0);
    list->map_tid = tid;

    assert((tid >= 0) && (tid < (int) list->tid_count));

    unsigned int firstOff = list->off[tid];
    unsigned int lastOff = list->off[tid + 1];
    if (lastOff > firstOff)
        list->map_count = (unsigned)(list->trange[lastOff - 1].mapNo + 1);
    else {
        list->map_count = 0;
        return;
    }

    list->map_off = malloc((list->map_count + 1) * sizeof(int));
    if (!list->map_off) {
        laik_panic("Out of memory allocating memory for Laik_BorderArray");
        exit(1); // not actually needed, laik_panic never returns
    }

    // only works with generic task ranges (other formats converted)
    assert(list->tss1d == 0);

    int mapNo;
    unsigned int off = firstOff;
    for(mapNo = 0; mapNo < (int) list->map_count; mapNo++) {
        list->map_off[mapNo] = off;
        while(off < lastOff) {
            Laik_TaskRange_Gen* ts = &(list->trange[off]);
            if (ts->mapNo > mapNo) break;
            assert(ts->mapNo == mapNo);
            off++;
        }
    }
    list->map_off[mapNo] = off;
    assert(off == lastOff);
}

unsigned int laik_rangelist_tidmaprangecount(Laik_RangeList* list, int tid, int mapNo)
{
    assert(list->off != 0);

    // lazily calculate my map offsets
    if (list->map_tid != tid)
        laik_updateMapOffsets(list, tid);

    assert((mapNo >= 0) && (mapNo < (int) list->map_count));
    return list->map_off[mapNo + 1] - list->map_off[mapNo];
}

// get range number <n> within mapping <mapNo>
// returns a pointer to a global instance, needs to be copied of stored
Laik_TaskRange* laik_rangelist_tidmaprange(Laik_RangeList* list, int tid, int mapNo, int n)
{
    assert(list->off != 0);

    // lazily calculate my map offsets
    if (list->map_tid != tid)
        laik_updateMapOffsets(list, tid);

    // does map with mapNo exist?
    if ((mapNo < 0) || (mapNo >= (int) list->map_count)) return 0;

    // is range <n> valid?
    int count = (int)(list->map_off[mapNo + 1] - list->map_off[mapNo]);
    if ((n < 0) || (n >= count)) return 0;

    int o = (int) list->map_off[mapNo] + n;
    assert(list->trange[o].task == tid);
    assert(list->trange[o].mapNo == mapNo);
    return laik_rangelist_taskrange(list, o);
}


// freeze range list
void laik_rangelist_freeze(Laik_RangeList* list, bool doMerge)
{
    assert(list->off == 0);

    // set partitioning valid by allocating/updating offsets
    list->off = malloc(sizeof(int) * (list->tid_count + 1));
    if (list->off == 0) {
        laik_panic("Out of memory allocating space for Laik_RangeList object");
        exit(1); // not actually needed, laik_panic never returns
    }

    if (list->tss1d) {
        // merge and convert to generic
        updateOffsetsSI(list);
    }
    else {
        sortRanges(list);

        // check for mergable ranges if requested
        if (doMerge)
            mergeSortedRanges(list);

        updateOffsets(list);
    }
}

// translate task ids using <idmap> array: idmap[old_id] = new_id
// if idmap[id] == -1, no range with that id is allowed to exist
void laik_rangelist_migrate(Laik_RangeList* list, int* idmap, unsigned int new_count)
{
    assert(list->off != 0);

    // check that there are no ranges of removed task ids
    for(unsigned int i = 0; i < list->tid_count; i++) {
        if (idmap[i] < 0)
            assert(list->off[i] == list->off[i+1]);
    }

    // update range task ids
    for(unsigned int i = 0; i < list->count; i++) {
        int old_id = list->trange[i].task;
        assert((old_id >= 0) && (old_id < (int) list->tid_count));
        int new_id = idmap[old_id];
        assert((new_id >= 0) && (new_id < (int) new_count));
        list->trange[i].task = new_id;
    }

    // resize offset array if needed
    if (new_count > list->tid_count) {
        free(list->off);
        list->off = malloc((new_count +1) * sizeof(int));
        if (!list->off) {
            laik_panic("Out of memory allocating space for Laik_RangeList");
            exit(1); // not actually needed, laik_panic never returns
        }
    }
    list->tid_count = new_count;
    sortRanges(list);
    updateOffsets(list);
}
